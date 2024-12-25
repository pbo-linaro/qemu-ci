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

uint32_t max_retry_count;
static QemuDsaDeviceGroup dsa_group;


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
__attribute__((unused))
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
__attribute__((unused))
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
__attribute__((unused))
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
__attribute__((unused))
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
__attribute__((unused))
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
 * @brief Check if DSA is running.
 *
 * @return True if DSA is running, otherwise false.
 */
bool qemu_dsa_is_running(void)
{
    return false;
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

