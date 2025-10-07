// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Remote I2C master
 *
 * Author:
 *   Ilya Chichkov <ilya.chichkov.dev@gmail.com>
 *
 */
#ifndef HW_REMOTE_I2C_MASTER_H
#define HW_REMOTE_I2C_MASTER_H

#include "hw/sysbus.h"

#define FUSE_USE_VERSION 31
#include <fuse3/fuse.h>
#include <fuse3/cuse_lowlevel.h>
#undef FUSE_USE_VERSION

#include <linux/i2c.h>
#include <linux/i2c-dev.h>

#define TYPE_REMOTE_I2C_MASTER "remote-i2c-master"

#define REMOTE_I2C_MASTER(obj) \
    OBJECT_CHECK(RemoteI2CControllerState, (obj), TYPE_REMOTE_I2C_MASTER)

#define REMOTE_I2C_MASTER_BUF_LEN   256

typedef enum i2c_ioctl_state {
    I2C_IOCTL_START,
    I2C_IOCTL_GET,
    I2C_IOCTL_RECV,
    I2C_IOCTL_SEND,
    I2C_IOCTL_FINISHED,
} i2c_ioctl_state;

typedef struct RemoteI2CControllerState {
    DeviceState parent_obj;

    I2CBus *i2c_bus;

    long address;
    QEMUTimer *timer;
    QEMUBH *bh;

    char *name;
    char *devname;

    struct fuse_session *fuse_session;
    struct fuse_buf fuse_buf;
    struct fuse_pollhandle *ph;
    bool is_open;

    /* specific CUSE helpers */
    i2c_ioctl_state ioctl_state;
    uint32_t last_ioctl;

    fuse_req_t req;
    const struct i2c_smbus_ioctl_data *in_val;
    const void *in_buf;
    bool is_recv;

    AioContext *ctx;
} RemoteI2CControllerState;

#endif /* HW_GD32_I2C_H */
