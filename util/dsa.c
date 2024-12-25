/*
 * Use Intel Data Streaming Accelerator to offload certain background
 * operations.
 *
 * Copyright (C) Bytedance Ltd.
 *
 * Authors:
 *  Hao Xiang <hao.xiang@bytedance.com>
 *  Bryan Zhang <bryan.zhang@bytedance.com>
 *  Yichen Wang <yichen.wang@bytedance.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/queue.h"
#include "qemu/memalign.h"
#include "qemu/lockable.h"
#include "qemu/cutils.h"
#include "qemu/dsa.h"
#include "qemu/bswap.h"
#include "qemu/error-report.h"
#include "qemu/rcu.h"

#pragma GCC push_options
#pragma GCC target("enqcmd")

#include <linux/idxd.h>
#include "x86intrin.h"

#define DSA_WQ_PORTAL_SIZE 4096
#define DSA_WQ_DEPTH 128
#define MAX_DSA_DEVICES 16
#define DSA_COMPLETION_THREAD "qemu_dsa_completion"

typedef struct {
    bool stopping;
    bool running;
    QemuThread thread;
    int thread_id;
    QemuSemaphore sem_init_done;
    QemuDsaDeviceGroup *group;
} QemuDsaCompletionThread;

uint32_t max_retry_count;
static QemuDsaDeviceGroup dsa_group;
static QemuDsaCompletionThread completion_thread;

static void buffer_zero_dsa_completion(void *context);

/**
 * @brief This function opens a DSA device's work queue and
 *        maps the DSA device memory into the current process.
 *
 * @param dsa_wq_path A pointer to the DSA device work queue's file path.
 * @return A pointer to the mapped memory, or MAP_FAILED on failure.
 */
static void *
map_dsa_device(const char *dsa_wq_path)
{
    void *dsa_device;
    int fd;

    fd = open(dsa_wq_path, O_RDWR);
    if (fd < 0) {
        error_report("Open %s failed with errno = %d.",
                dsa_wq_path, errno);
        return MAP_FAILED;
    }
    dsa_device = mmap(NULL, DSA_WQ_PORTAL_SIZE, PROT_WRITE,
                      MAP_SHARED | MAP_POPULATE, fd, 0);
    close(fd);
    if (dsa_device == MAP_FAILED) {
        error_report("mmap failed with errno = %d.", errno);
        return MAP_FAILED;
    }
    return dsa_device;
}

/**
 * @brief Initializes a DSA device structure.
 *
 * @param instance A pointer to the DSA device.
 * @param work_queue A pointer to the DSA work queue.
 */
static void
dsa_device_init(QemuDsaDevice *instance,
                void *dsa_work_queue)
{
    instance->work_queue = dsa_work_queue;
}

/**
 * @brief Cleans up a DSA device structure.
 *
 * @param instance A pointer to the DSA device to cleanup.
 */
static void
dsa_device_cleanup(QemuDsaDevice *instance)
{
    if (instance->work_queue != MAP_FAILED) {
        munmap(instance->work_queue, DSA_WQ_PORTAL_SIZE);
    }
}

/**
 * @brief Initializes a DSA device group.
 *
 * @param group A pointer to the DSA device group.
 * @param dsa_parameter A list of DSA device path from are separated by space
 * character migration parameter. Multiple DSA device path.
 *
 * @return Zero if successful, non-zero otherwise.
 */
static int
dsa_device_group_init(QemuDsaDeviceGroup *group,
                      const strList *dsa_parameter,
                      Error **errp)
{
    if (dsa_parameter == NULL) {
        error_setg(errp, "dsa device path is not supplied.");
        return -1;
    }

    int ret = 0;
    const char *dsa_path[MAX_DSA_DEVICES];
    int num_dsa_devices = 0;

    while (dsa_parameter) {
        dsa_path[num_dsa_devices++] = dsa_parameter->value;
        if (num_dsa_devices == MAX_DSA_DEVICES) {
            break;
        }
        dsa_parameter = dsa_parameter->next;
    }

    group->dsa_devices =
        g_new0(QemuDsaDevice, num_dsa_devices);
    group->num_dsa_devices = num_dsa_devices;
    group->device_allocator_index = 0;

    group->running = false;
    qemu_mutex_init(&group->task_queue_lock);
    qemu_cond_init(&group->task_queue_cond);
    QSIMPLEQ_INIT(&group->task_queue);

    void *dsa_wq = MAP_FAILED;
    for (int i = 0; i < num_dsa_devices; i++) {
        dsa_wq = map_dsa_device(dsa_path[i]);
        if (dsa_wq == MAP_FAILED && ret != -1) {
            error_setg(errp, "map_dsa_device failed MAP_FAILED.");
            ret = -1;
        }
        dsa_device_init(&group->dsa_devices[i], dsa_wq);
    }

    return ret;
}

/**
 * @brief Starts a DSA device group.
 *
 * @param group A pointer to the DSA device group.
 */
static void
dsa_device_group_start(QemuDsaDeviceGroup *group)
{
    group->running = true;
}

/**
 * @brief Stops a DSA device group.
 *
 * @param group A pointer to the DSA device group.
 */
static void
dsa_device_group_stop(QemuDsaDeviceGroup *group)
{
    group->running = false;
}

/**
 * @brief Cleans up a DSA device group.
 *
 * @param group A pointer to the DSA device group.
 */
static void
dsa_device_group_cleanup(QemuDsaDeviceGroup *group)
{
    if (!group->dsa_devices) {
        return;
    }
    for (int i = 0; i < group->num_dsa_devices; i++) {
        dsa_device_cleanup(&group->dsa_devices[i]);
    }
    g_free(group->dsa_devices);
    group->dsa_devices = NULL;

    qemu_mutex_destroy(&group->task_queue_lock);
    qemu_cond_destroy(&group->task_queue_cond);
}

/**
 * @brief Returns the next available DSA device in the group.
 *
 * @param group A pointer to the DSA device group.
 *
 * @return struct QemuDsaDevice* A pointer to the next available DSA device
 *         in the group.
 */
static QemuDsaDevice *
dsa_device_group_get_next_device(QemuDsaDeviceGroup *group)
{
    if (group->num_dsa_devices == 0) {
        return NULL;
    }
    uint32_t current = qatomic_fetch_inc(&group->device_allocator_index);
    current %= group->num_dsa_devices;
    return &group->dsa_devices[current];
}

/**
 * @brief Empties out the DSA task queue.
 *
 * @param group A pointer to the DSA device group.
 */
static void
dsa_empty_task_queue(QemuDsaDeviceGroup *group)
{
    qemu_mutex_lock(&group->task_queue_lock);
    QemuDsaTaskQueue *task_queue = &group->task_queue;
    while (!QSIMPLEQ_EMPTY(task_queue)) {
        QSIMPLEQ_REMOVE_HEAD(task_queue, entry);
    }
    qemu_mutex_unlock(&group->task_queue_lock);
}

/**
 * @brief Adds a task to the DSA task queue.
 *
 * @param group A pointer to the DSA device group.
 * @param task A pointer to the DSA task to enqueue.
 *
 * @return int Zero if successful, otherwise a proper error code.
 */
static int
dsa_task_enqueue(QemuDsaDeviceGroup *group,
                 QemuDsaBatchTask *task)
{
    bool notify = false;

    qemu_mutex_lock(&group->task_queue_lock);

    if (!group->running) {
        error_report("DSA: Tried to queue task to stopped device queue.");
        qemu_mutex_unlock(&group->task_queue_lock);
        return -1;
    }

    /* The queue is empty. This enqueue operation is a 0->1 transition. */
    if (QSIMPLEQ_EMPTY(&group->task_queue)) {
        notify = true;
    }

    QSIMPLEQ_INSERT_TAIL(&group->task_queue, task, entry);

    /* We need to notify the waiter for 0->1 transitions. */
    if (notify) {
        qemu_cond_signal(&group->task_queue_cond);
    }

    qemu_mutex_unlock(&group->task_queue_lock);

    return 0;
}

/**
 * @brief Takes a DSA task out of the task queue.
 *
 * @param group A pointer to the DSA device group.
 * @return QemuDsaBatchTask* The DSA task being dequeued.
 */
static QemuDsaBatchTask *
dsa_task_dequeue(QemuDsaDeviceGroup *group)
{
    QemuDsaBatchTask *task = NULL;

    qemu_mutex_lock(&group->task_queue_lock);

    while (true) {
        if (!group->running) {
            goto exit;
        }
        task = QSIMPLEQ_FIRST(&group->task_queue);
        if (task != NULL) {
            break;
        }
        qemu_cond_wait(&group->task_queue_cond, &group->task_queue_lock);
    }

    QSIMPLEQ_REMOVE_HEAD(&group->task_queue, entry);

exit:
    qemu_mutex_unlock(&group->task_queue_lock);
    return task;
}

/**
 * @brief Submits a DSA work item to the device work queue.
 *
 * @param wq A pointer to the DSA work queue's device memory.
 * @param descriptor A pointer to the DSA work item descriptor.
 *
 * @return Zero if successful, non-zero otherwise.
 */
static int
submit_wi_int(void *wq, struct dsa_hw_desc *descriptor)
{
    uint32_t retry = 0;

    _mm_sfence();

    while (true) {
        if (_enqcmd(wq, descriptor) == 0) {
            break;
        }
        retry++;
        if (retry > max_retry_count) {
            error_report("Submit work retry %u times.", retry);
            return -1;
        }
    }

    return 0;
}

/**
 * @brief Asynchronously submits a DSA work item to the
 *        device work queue.
 *
 * @param task A pointer to the task.
 *
 * @return int Zero if successful, non-zero otherwise.
 */
static int
submit_wi_async(QemuDsaBatchTask *task)
{
    QemuDsaDeviceGroup *device_group = task->group;
    QemuDsaDevice *device_instance = task->device;
    int ret;

    assert(task->task_type == QEMU_DSA_TASK);

    task->status = QEMU_DSA_TASK_PROCESSING;

    ret = submit_wi_int(device_instance->work_queue,
                        &task->descriptors[0]);
    if (ret != 0) {
        return ret;
    }

    return dsa_task_enqueue(device_group, task);
}

/**
 * @brief Asynchronously submits a DSA batch work item to the
 *        device work queue.
 *
 * @param batch_task A pointer to the batch task.
 *
 * @return int Zero if successful, non-zero otherwise.
 */
static int
submit_batch_wi_async(QemuDsaBatchTask *batch_task)
{
    QemuDsaDeviceGroup *device_group = batch_task->group;
    QemuDsaDevice *device_instance = batch_task->device;
    int ret;

    assert(batch_task->task_type == QEMU_DSA_BATCH_TASK);
    assert(batch_task->batch_descriptor.desc_count <= batch_task->batch_size);
    assert(batch_task->status == QEMU_DSA_TASK_READY);

    batch_task->status = QEMU_DSA_TASK_PROCESSING;

    ret = submit_wi_int(device_instance->work_queue,
                        &batch_task->batch_descriptor);
    if (ret != 0) {
        return ret;
    }

    return dsa_task_enqueue(device_group, batch_task);
}

/**
 * @brief Poll for the DSA work item completion.
 *
 * @param completion A pointer to the DSA work item completion record.
 * @param opcode The DSA opcode.
 *
 * @return Zero if successful, non-zero otherwise.
 */
static int
poll_completion(struct dsa_completion_record *completion,
                enum dsa_opcode opcode)
{
    uint8_t status;
    uint64_t retry = 0;

    while (true) {
        /* The DSA operation completes successfully or fails. */
        status = completion->status;
        if (status == DSA_COMP_SUCCESS ||
            status == DSA_COMP_PAGE_FAULT_NOBOF ||
            status == DSA_COMP_BATCH_PAGE_FAULT ||
            status == DSA_COMP_BATCH_FAIL) {
            break;
        } else if (status != DSA_COMP_NONE) {
            error_report("DSA opcode %d failed with status = %d.",
                    opcode, status);
            return 1;
        }
        retry++;
        if (retry > max_retry_count) {
            error_report("DSA wait for completion retry %lu times.", retry);
            return 1;
        }
        _mm_pause();
    }

    return 0;
}

/**
 * @brief Complete a single DSA task in the batch task.
 *
 * @param task A pointer to the batch task structure.
 *
 * @return Zero if successful, otherwise non-zero.
 */
static int
poll_task_completion(QemuDsaBatchTask *task)
{
    assert(task->task_type == QEMU_DSA_TASK);

    struct dsa_completion_record *completion = &task->completions[0];
    uint8_t status;
    int ret;

    ret = poll_completion(completion, task->descriptors[0].opcode);
    if (ret != 0) {
        goto exit;
    }

    status = completion->status;
    if (status == DSA_COMP_SUCCESS) {
        task->results[0] = (completion->result == 0);
        goto exit;
    }

    assert(status == DSA_COMP_PAGE_FAULT_NOBOF);

exit:
    return ret;
}

/**
 * @brief Poll a batch task status until it completes. If DSA task doesn't
 *        complete properly, use CPU to complete the task.
 *
 * @param batch_task A pointer to the DSA batch task.
 *
 * @return Zero if successful, otherwise non-zero.
 */
static int
poll_batch_task_completion(QemuDsaBatchTask *batch_task)
{
    struct dsa_completion_record *batch_completion =
        &batch_task->batch_completion;
    struct dsa_completion_record *completion;
    uint8_t batch_status;
    uint8_t status;
    bool *results = batch_task->results;
    uint32_t count = batch_task->batch_descriptor.desc_count;
    int ret;

    ret = poll_completion(batch_completion,
                          batch_task->batch_descriptor.opcode);
    if (ret != 0) {
        goto exit;
    }

    batch_status = batch_completion->status;

    if (batch_status == DSA_COMP_SUCCESS) {
        if (batch_completion->bytes_completed == count) {
            /*
             * Let's skip checking for each descriptors' completion status
             * if the batch descriptor says all succedded.
             */
            for (int i = 0; i < count; i++) {
                assert(batch_task->completions[i].status == DSA_COMP_SUCCESS);
                results[i] = (batch_task->completions[i].result == 0);
            }
            goto exit;
        }
    } else {
        assert(batch_status == DSA_COMP_BATCH_FAIL ||
            batch_status == DSA_COMP_BATCH_PAGE_FAULT);
    }

    for (int i = 0; i < count; i++) {

        completion = &batch_task->completions[i];
        status = completion->status;

        if (status == DSA_COMP_SUCCESS) {
            results[i] = (completion->result == 0);
            continue;
        }

        if (status != DSA_COMP_PAGE_FAULT_NOBOF) {
            error_report("Unexpected DSA completion status = %u.", status);
            ret = 1;
            goto exit;
        }
    }

exit:
    return ret;
}

/**
 * @brief Handles an asynchronous DSA batch task completion.
 *
 * @param task A pointer to the batch buffer zero task structure.
 */
static void
dsa_batch_task_complete(QemuDsaBatchTask *batch_task)
{
    batch_task->status = QEMU_DSA_TASK_COMPLETION;
    batch_task->completion_callback(batch_task);
}

/**
 * @brief The function entry point called by a dedicated DSA
 *        work item completion thread.
 *
 * @param opaque A pointer to the thread context.
 *
 * @return void* Not used.
 */
static void *
dsa_completion_loop(void *opaque)
{
    QemuDsaCompletionThread *thread_context =
        (QemuDsaCompletionThread *)opaque;
    QemuDsaBatchTask *batch_task;
    QemuDsaDeviceGroup *group = thread_context->group;
    int ret;

    rcu_register_thread();

    thread_context->thread_id = qemu_get_thread_id();
    qemu_sem_post(&thread_context->sem_init_done);

    while (thread_context->running) {
        batch_task = dsa_task_dequeue(group);
        assert(batch_task != NULL || !group->running);
        if (!group->running) {
            assert(!thread_context->running);
            break;
        }
        if (batch_task->task_type == QEMU_DSA_TASK) {
            ret = poll_task_completion(batch_task);
        } else {
            assert(batch_task->task_type == QEMU_DSA_BATCH_TASK);
            ret = poll_batch_task_completion(batch_task);
        }

        if (ret != 0) {
            goto exit;
        }

        dsa_batch_task_complete(batch_task);
    }

exit:
    if (ret != 0) {
        error_report("DSA completion thread exited due to internal error.");
    }
    rcu_unregister_thread();
    return NULL;
}

/**
 * @brief Initializes a DSA completion thread.
 *
 * @param completion_thread A pointer to the completion thread context.
 * @param group A pointer to the DSA device group.
 */
static void
dsa_completion_thread_init(
    QemuDsaCompletionThread *completion_thread,
    QemuDsaDeviceGroup *group)
{
    completion_thread->stopping = false;
    completion_thread->running = true;
    completion_thread->thread_id = -1;
    qemu_sem_init(&completion_thread->sem_init_done, 0);
    completion_thread->group = group;

    qemu_thread_create(&completion_thread->thread,
                       DSA_COMPLETION_THREAD,
                       dsa_completion_loop,
                       completion_thread,
                       QEMU_THREAD_JOINABLE);

    /* Wait for initialization to complete */
    qemu_sem_wait(&completion_thread->sem_init_done);
}

/**
 * @brief Stops the completion thread (and implicitly, the device group).
 *
 * @param opaque A pointer to the completion thread.
 */
static void dsa_completion_thread_stop(void *opaque)
{
    QemuDsaCompletionThread *thread_context =
        (QemuDsaCompletionThread *)opaque;

    QemuDsaDeviceGroup *group = thread_context->group;

    qemu_mutex_lock(&group->task_queue_lock);

    thread_context->stopping = true;
    thread_context->running = false;

    /* Prevent the compiler from setting group->running first. */
    barrier();
    dsa_device_group_stop(group);

    qemu_cond_signal(&group->task_queue_cond);
    qemu_mutex_unlock(&group->task_queue_lock);

    qemu_thread_join(&thread_context->thread);

    qemu_sem_destroy(&thread_context->sem_init_done);
}

/**
 * @brief Check if DSA is running.
 *
 * @return True if DSA is running, otherwise false.
 */
bool qemu_dsa_is_running(void)
{
    return completion_thread.running;
}

static void
dsa_globals_init(void)
{
    /*
     * This value follows a reference example by Intel. The POLL_RETRY_MAX is
     * defined to 10000, so here we used the max WQ depth * 100 for the the max
     * polling retry count.
     */
    max_retry_count = DSA_WQ_DEPTH * 100;
}

/**
 * @brief Initializes DSA devices.
 *
 * @param dsa_parameter A list of DSA device path from migration parameter.
 *
 * @return int Zero if successful, otherwise non zero.
 */
int qemu_dsa_init(const strList *dsa_parameter, Error **errp)
{
    dsa_globals_init();

    return dsa_device_group_init(&dsa_group, dsa_parameter, errp);
}

/**
 * @brief Start logic to enable using DSA.
 *
 */
void qemu_dsa_start(void)
{
    if (dsa_group.num_dsa_devices == 0) {
        return;
    }
    if (dsa_group.running) {
        return;
    }
    dsa_device_group_start(&dsa_group);
    dsa_completion_thread_init(&completion_thread, &dsa_group);
}

/**
 * @brief Stop the device group and the completion thread.
 *
 */
void qemu_dsa_stop(void)
{
    QemuDsaDeviceGroup *group = &dsa_group;

    if (!group->running) {
        return;
    }

    dsa_completion_thread_stop(&completion_thread);
    dsa_empty_task_queue(group);
}

/**
 * @brief Clean up system resources created for DSA offloading.
 *
 */
void qemu_dsa_cleanup(void)
{
    qemu_dsa_stop();
    dsa_device_group_cleanup(&dsa_group);
}


/* Buffer zero comparison DSA task implementations */
/* =============================================== */

/**
 * @brief Sets a buffer zero comparison DSA task.
 *
 * @param descriptor A pointer to the DSA task descriptor.
 * @param buf A pointer to the memory buffer.
 * @param len The length of the buffer.
 */
static void
buffer_zero_task_set_int(struct dsa_hw_desc *descriptor,
                         const void *buf,
                         size_t len)
{
    struct dsa_completion_record *completion =
        (struct dsa_completion_record *)descriptor->completion_addr;

    descriptor->xfer_size = len;
    descriptor->src_addr = (uintptr_t)buf;
    completion->status = 0;
    completion->result = 0;
}

/**
 * @brief Resets a buffer zero comparison DSA batch task.
 *
 * @param task A pointer to the DSA batch task.
 */
static void
buffer_zero_task_reset(QemuDsaBatchTask *task)
{
    task->completions[0].status = DSA_COMP_NONE;
    task->task_type = QEMU_DSA_TASK;
    task->status = QEMU_DSA_TASK_READY;
}

/**
 * @brief Resets a buffer zero comparison DSA batch task.
 *
 * @param task A pointer to the batch task.
 * @param count The number of DSA tasks this batch task will contain.
 */
static void
buffer_zero_batch_task_reset(QemuDsaBatchTask *task, size_t count)
{
    task->batch_completion.status = DSA_COMP_NONE;
    task->batch_descriptor.desc_count = count;
    task->task_type = QEMU_DSA_BATCH_TASK;
    task->status = QEMU_DSA_TASK_READY;
}

/**
 * @brief Sets a buffer zero comparison DSA task.
 *
 * @param task A pointer to the DSA task.
 * @param buf A pointer to the memory buffer.
 * @param len The buffer length.
 */
static void
buffer_zero_task_set(QemuDsaBatchTask *task,
                     const void *buf,
                     size_t len)
{
    buffer_zero_task_reset(task);
    buffer_zero_task_set_int(&task->descriptors[0], buf, len);
}

/**
 * @brief Sets a buffer zero comparison batch task.
 *
 * @param batch_task A pointer to the batch task.
 * @param buf An array of memory buffers.
 * @param count The number of buffers in the array.
 * @param len The length of the buffers.
 */
static void
buffer_zero_batch_task_set(QemuDsaBatchTask *batch_task,
                           const void **buf, size_t count, size_t len)
{
    assert(count > 0);
    assert(count <= batch_task->batch_size);

    buffer_zero_batch_task_reset(batch_task, count);
    for (int i = 0; i < count; i++) {
        buffer_zero_task_set_int(&batch_task->descriptors[i], buf[i], len);
    }
}

/**
 * @brief Asychronously perform a buffer zero DSA operation.
 *
 * @param task A pointer to the batch task structure.
 * @param buf A pointer to the memory buffer.
 * @param len The length of the memory buffer.
 *
 * @return int Zero if successful, otherwise an appropriate error code.
 */
__attribute__((unused))
static int
buffer_zero_dsa_async(QemuDsaBatchTask *task,
                      const void *buf, size_t len)
{
    buffer_zero_task_set(task, buf, len);

    return submit_wi_async(task);
}

/**
 * @brief Sends a memory comparison batch task to a DSA device and wait
 *        for completion.
 *
 * @param batch_task The batch task to be submitted to DSA device.
 * @param buf An array of memory buffers to check for zero.
 * @param count The number of buffers.
 * @param len The buffer length.
 */
__attribute__((unused))
static int
buffer_zero_dsa_batch_async(QemuDsaBatchTask *batch_task,
                            const void **buf, size_t count, size_t len)
{
    assert(count <= batch_task->batch_size);
    buffer_zero_batch_task_set(batch_task, buf, count, len);

    return submit_batch_wi_async(batch_task);
}

/**
 * @brief The completion callback function for buffer zero
 *        comparison DSA task completion.
 *
 * @param context A pointer to the callback context.
 */
static void
buffer_zero_dsa_completion(void *context)
{
    assert(context != NULL);

    QemuDsaBatchTask *task = (QemuDsaBatchTask *)context;
    qemu_sem_post(&task->sem_task_complete);
}

/**
 * @brief Wait for the asynchronous DSA task to complete.
 *
 * @param batch_task A pointer to the buffer zero comparison batch task.
 */
__attribute__((unused))
static void
buffer_zero_dsa_wait(QemuDsaBatchTask *batch_task)
{
    qemu_sem_wait(&batch_task->sem_task_complete);
}

/**
 * @brief Initializes a buffer zero comparison DSA task.
 *
 * @param descriptor A pointer to the DSA task descriptor.
 * @param completion A pointer to the DSA task completion record.
 */
static void
buffer_zero_task_init_int(struct dsa_hw_desc *descriptor,
                          struct dsa_completion_record *completion)
{
    descriptor->opcode = DSA_OPCODE_COMPVAL;
    descriptor->flags = IDXD_OP_FLAG_RCR | IDXD_OP_FLAG_CRAV;
    descriptor->comp_pattern = (uint64_t)0;
    descriptor->completion_addr = (uint64_t)completion;
}

/**
 * @brief Initializes a buffer zero DSA batch task.
 *
 * @param batch_size The number of zero page checking tasks in the batch.
 * @return A pointer to the zero page checking tasks initialized.
 */
QemuDsaBatchTask *
buffer_zero_batch_task_init(int batch_size)
{
    QemuDsaBatchTask *task = qemu_memalign(64, sizeof(QemuDsaBatchTask));
    int descriptors_size = sizeof(*task->descriptors) * batch_size;

    memset(task, 0, sizeof(*task));
    task->addr = g_new0(ram_addr_t, batch_size);
    task->results = g_new0(bool, batch_size);
    task->batch_size = batch_size;
    task->descriptors =
        (struct dsa_hw_desc *)qemu_memalign(64, descriptors_size);
    memset(task->descriptors, 0, descriptors_size);
    task->completions = (struct dsa_completion_record *)qemu_memalign(
        32, sizeof(*task->completions) * batch_size);

    task->batch_completion.status = DSA_COMP_NONE;
    task->batch_descriptor.completion_addr = (uint64_t)&task->batch_completion;
    /* TODO: Ensure that we never send a batch with count <= 1 */
    task->batch_descriptor.desc_count = 0;
    task->batch_descriptor.opcode = DSA_OPCODE_BATCH;
    task->batch_descriptor.flags = IDXD_OP_FLAG_RCR | IDXD_OP_FLAG_CRAV;
    task->batch_descriptor.desc_list_addr = (uintptr_t)task->descriptors;
    task->status = QEMU_DSA_TASK_READY;
    task->group = &dsa_group;
    task->device = dsa_device_group_get_next_device(&dsa_group);

    for (int i = 0; i < task->batch_size; i++) {
        buffer_zero_task_init_int(&task->descriptors[i],
                                  &task->completions[i]);
    }

    qemu_sem_init(&task->sem_task_complete, 0);
    task->completion_callback = buffer_zero_dsa_completion;

    return task;
}

/**
 * @brief Performs the proper cleanup on a DSA batch task.
 *
 * @param task A pointer to the batch task to cleanup.
 */
void
buffer_zero_batch_task_destroy(QemuDsaBatchTask *task)
{
    if (task) {
        g_free(task->addr);
        g_free(task->results);
        qemu_vfree(task->descriptors);
        qemu_vfree(task->completions);
        task->results = NULL;
        qemu_sem_destroy(&task->sem_task_complete);
        qemu_vfree(task);
    }
}
