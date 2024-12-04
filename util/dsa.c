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
    max_retry_count = UINT32_MAX;
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

