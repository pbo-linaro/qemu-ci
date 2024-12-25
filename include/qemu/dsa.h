/*
 * Interface for using Intel Data Streaming Accelerator to offload certain
 * background operations.
 *
 * Copyright (C) Bytedance Ltd.
 *
 * Authors:
 *  Hao Xiang <hao.xiang@bytedance.com>
 *  Yichen Wang <yichen.wang@bytedance.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef QEMU_DSA_H
#define QEMU_DSA_H

#include "qapi/error.h"
#include "qemu/thread.h"
#include "qemu/queue.h"

#ifdef CONFIG_DSA_OPT

#pragma GCC push_options
#pragma GCC target("enqcmd")

#include <linux/idxd.h>
#include "x86intrin.h"

typedef enum QemuDsaTaskType {
    QEMU_DSA_TASK = 0,
    QEMU_DSA_BATCH_TASK
} QemuDsaTaskType;

typedef enum QemuDsaTaskStatus {
    QEMU_DSA_TASK_READY = 0,
    QEMU_DSA_TASK_PROCESSING,
    QEMU_DSA_TASK_COMPLETION
} QemuDsaTaskStatus;

typedef struct {
    void *work_queue;
} QemuDsaDevice;

typedef QSIMPLEQ_HEAD(QemuDsaTaskQueue, QemuDsaBatchTask) QemuDsaTaskQueue;

typedef struct {
    QemuDsaDevice *dsa_devices;
    int num_dsa_devices;
    /* The index of the next DSA device to be used. */
    uint32_t device_allocator_index;
    bool running;
    QemuMutex task_queue_lock;
    QemuCond task_queue_cond;
    QemuDsaTaskQueue task_queue;
} QemuDsaDeviceGroup;

typedef void (*qemu_dsa_completion_fn)(void *);

typedef struct QemuDsaBatchTask {
    struct dsa_hw_desc batch_descriptor;
    struct dsa_hw_desc *descriptors;
    struct dsa_completion_record batch_completion __attribute__((aligned(32)));
    struct dsa_completion_record *completions;
    QemuDsaDeviceGroup *group;
    QemuDsaDevice *device;
    qemu_dsa_completion_fn completion_callback;
    QemuSemaphore sem_task_complete;
    QemuDsaTaskType task_type;
    QemuDsaTaskStatus status;
    int batch_size;
    QSIMPLEQ_ENTRY(QemuDsaBatchTask) entry;
} QemuDsaBatchTask;


/**
 * @brief Initializes DSA devices.
 *
 * @param dsa_parameter A list of DSA device path from migration parameter.
 *
 * @return int Zero if successful, otherwise non zero.
 */
int qemu_dsa_init(const strList *dsa_parameter, Error **errp);

/**
 * @brief Start logic to enable using DSA.
 */
void qemu_dsa_start(void);

/**
 * @brief Stop the device group and the completion thread.
 */
void qemu_dsa_stop(void);

/**
 * @brief Clean up system resources created for DSA offloading.
 */
void qemu_dsa_cleanup(void);

/**
 * @brief Check if DSA is running.
 *
 * @return True if DSA is running, otherwise false.
 */
bool qemu_dsa_is_running(void);

#else

static inline bool qemu_dsa_is_running(void)
{
    return false;
}

static inline int qemu_dsa_init(const strList *dsa_parameter, Error **errp)
{
    error_setg(errp, "DSA accelerator is not enabled.");
    return -1;
}

static inline void qemu_dsa_start(void) {}

static inline void qemu_dsa_stop(void) {}

static inline void qemu_dsa_cleanup(void) {}

#endif

#endif
