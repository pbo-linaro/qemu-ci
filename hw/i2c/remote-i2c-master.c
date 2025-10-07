// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Remote I2C master
 *
 * Author:
 *   Ilya Chichkov <ilya.chichkov.dev@gmail.com>
 *
 */
#include "qemu/osdep.h"

#include "qapi/error.h"
#include "qemu/main-loop.h"
#include "hw/i2c/i2c.h"
#include "hw/qdev-properties-system.h"
#include "qemu/error-report.h"
#include "block/aio.h"
#include "qemu/log.h"
#include "trace.h"

#include "hw/i2c/remote-i2c-master.h"


#define FUSE_OPT_DUMMY "\0\0"
#define FUSE_OPT_FORE  "-f\0\0"
#define FUSE_OPT_NOMULTI "-s\0\0"
#define FUSE_OPT_DEBUG "-d\0\0"

typedef enum {
    REMOTE_I2C_START_RECV = 0,
    REMOTE_I2C_START_SEND = 1,
    REMOTE_I2C_FINISH = 2,
    REMOTE_I2C_NACK = 3,
    REMOTE_I2C_RECV = 4,
    REMOTE_I2C_SEND = 5,
} RemoteI2CCommand;

typedef enum AUXCommand {
    WRITE_I2C = 0,
    READ_I2C = 1,
    WRITE_I2C_STATUS = 2,
    WRITE_I2C_MOT = 4,
    READ_I2C_MOT = 5,
    WRITE_AUX = 8,
    READ_AUX = 9
} AUXCommand;

typedef enum AUXReply {
    AUX_I2C_ACK = 0,
    AUX_NACK = 1,
    AUX_DEFER = 2,
    AUX_I2C_NACK = 4,
    AUX_I2C_DEFER = 8
} AUXReply;

struct remote_i2c_cmd {
    uint8_t cmd;
    uint8_t addr;
    uint8_t len;
    uint8_t data[];
} __attribute__((packed));

static void i2cdev_init(void *userdata, struct fuse_conn_info *conn)
{
    (void)userdata;

    trace_remote_i2c_master_i2cdev_init();
}

static void i2cdev_open(fuse_req_t req, struct fuse_file_info *fi)
{
    RemoteI2CControllerState *s = fuse_req_userdata(req);

    fuse_reply_open(req, fi);

    s->is_open = true;

    trace_remote_i2c_master_i2cdev_open();
}

static void i2cdev_release(fuse_req_t req, struct fuse_file_info *fi)
{
    RemoteI2CControllerState *s = fuse_req_userdata(req);

    s->is_open = false;

    fuse_reply_err(req, 0);

    trace_remote_i2c_master_i2cdev_release();
}

static void i2cdev_read(fuse_req_t req, size_t size, off_t off,
                        struct fuse_file_info *fi)
{
    /* unused? */
    char *buf = NULL;
    size_t bsize = 0;

    bsize = 1;
    buf = g_realloc(buf, bsize);
    memset(buf, 44, 1);
    fuse_reply_buf(req, buf, bsize);
    g_free(buf);

    trace_remote_i2c_master_i2cdev_read();
}

static void i2cdev_functional(RemoteI2CControllerState *i2c,
                              fuse_req_t req,
                              void *arg,
                              const void *in_buf)
{
    unsigned long funcs = (I2C_FUNC_I2C | I2C_FUNC_SMBUS_QUICK |
                           I2C_FUNC_SMBUS_BYTE | I2C_FUNC_SMBUS_BYTE_DATA |
                           I2C_FUNC_SMBUS_BLOCK_DATA |
                           I2C_FUNC_SMBUS_WORD_DATA |
                           I2C_FUNC_SMBUS_I2C_BLOCK);
    struct iovec iov = {
        .iov_base = arg,
        .iov_len = sizeof(size_t)
    };

    switch (i2c->ioctl_state) {
    case I2C_IOCTL_START:
        i2c->ioctl_state = I2C_IOCTL_GET;
        /* Sending arg size of 'size_t' */
        fuse_reply_ioctl_retry(req, NULL, 0, &iov, 1);
        break;
    case I2C_IOCTL_GET:
        /* Sending I2C functional size of unsigned long */
        fuse_reply_ioctl(req, 0, &funcs, sizeof(funcs));
        i2c->ioctl_state = I2C_IOCTL_FINISHED;
        trace_remote_i2c_master_i2cdev_functional();
        break;
    default:
        /* assert */
        break;
    }
}

static void i2cdev_address(RemoteI2CControllerState *i2c,
                           fuse_req_t req,
                           void *arg,
                           const void *in_buf)
{
    i2c->address = (long)arg;

    trace_remote_i2c_master_i2cdev_address(i2c->address);

    if (i2c->address < 0 || i2c->address > 127) {
        fuse_reply_err(req, EINVAL);
        return;
    }
    fuse_reply_ioctl(req, 0, NULL, 0);
    i2c->ioctl_state = I2C_IOCTL_FINISHED;
}

static void send_data_to_slave(RemoteI2CControllerState *i2c,
                               fuse_req_t req,
                               const struct i2c_smbus_ioctl_data *in_val,
                               const void *in_buf)
{
    union i2c_smbus_data data;
    uint8_t buf[64] = { 0 };
    size_t i = 0;
    buf[0] = in_val->read_write;
    buf[1] = (uint8_t)i2c->address;

    /* Get SMBus data structure */
    memcpy(&data,
           in_buf + sizeof(struct i2c_smbus_ioctl_data),
           sizeof(union i2c_smbus_data));

    /* Parse data from SMBus struct */
    switch (in_val->size) {
    case I2C_SMBUS_BYTE_DATA:
        buf[2] = 2;
        buf[3] = in_val->command;
        buf[4] = data.byte;
    break;
    case I2C_SMBUS_WORD_DATA:
        buf[2] = 3;
        buf[3] = in_val->command;
        buf[4] = (uint8_t)(data.word & 0xFF);
        buf[5] = (uint8_t)(data.word >> 8 & 0xFF);
    break;
    case I2C_SMBUS_I2C_BLOCK_BROKEN:
    case I2C_SMBUS_BLOCK_DATA:
    case I2C_SMBUS_I2C_BLOCK_DATA:
    {
        uint8_t len = data.block[0];
        buf[2] = len + 1;
        buf[3] = in_val->command;
        for (i = 0; i < len; i++) {
            buf[4 + i] = data.block[i + 1];
        }
    }
    break;
    }

    /* Send data to I2C bus */
    i2c_start_send(i2c->i2c_bus, i2c->address);
    for (i = 0; i < buf[2]; i++) {
        i2c_send(i2c->i2c_bus, buf[3 + i]);
    }

    i2c->address = 0x0;
    i2c->ioctl_state = I2C_IOCTL_FINISHED;
    fuse_reply_ioctl(req, 0, NULL, 0);

    trace_remote_i2c_master_i2cdev_send(in_val->size);
}

static void recv_data_from_slave(RemoteI2CControllerState *i2c,
                                 fuse_req_t req,
                                 const struct i2c_smbus_ioctl_data *in_val,
                                 const void *in_buf)
{
    union i2c_smbus_data *smbus_data = (union i2c_smbus_data *)(
        in_buf + sizeof(struct i2c_smbus_ioctl_data)
    );
    uint8_t receive_byte = 0;
    size_t i = 0;

    /* Send command to slave */
    i2c_start_send(i2c->i2c_bus, i2c->address);
    i2c_send(i2c->i2c_bus, in_val->command);
    i2c_start_recv(i2c->i2c_bus, i2c->address);

    /* Receive data from slave */
    switch (in_val->size) {
    case I2C_SMBUS_BYTE_DATA:
        smbus_data->byte = i2c_recv(i2c->i2c_bus);
    break;
    case I2C_SMBUS_WORD_DATA:
        receive_byte = i2c_recv(i2c->i2c_bus);
        smbus_data->word = ((uint16_t)receive_byte) & 0xFF;
        receive_byte = i2c_recv(i2c->i2c_bus);
        smbus_data->word |= (((uint16_t)receive_byte) << 8) & 0xFF00;
    break;
    case I2C_SMBUS_I2C_BLOCK_BROKEN:
    case I2C_SMBUS_BLOCK_DATA:
    case I2C_SMBUS_I2C_BLOCK_DATA:
    {
        uint8_t len = smbus_data->block[0];
        for (i = 0; i < len; i++) {
            smbus_data->block[1 + i] = i2c_recv(i2c->i2c_bus);
        }
    }
    break;
    }

    i2c->ioctl_state = I2C_IOCTL_FINISHED;
    fuse_reply_ioctl(req, 0, smbus_data, sizeof(union i2c_smbus_data *));

    trace_remote_i2c_master_i2cdev_receive(in_val->size);
}

static void i2cdev_cmd_smbus(RemoteI2CControllerState *i2c,
                             fuse_req_t req,
                             void *in_arg,
                             const void *in_buf,
                             size_t in_bufsz,
                             size_t out_bufsz)
{
    I2CBus *i2c_bus = i2c->i2c_bus;
    const struct i2c_smbus_ioctl_data *in_val;
    struct iovec in_iov[2];

    in_val = in_buf;
    in_iov[0].iov_base = in_arg;
    in_iov[0].iov_len = sizeof(struct i2c_smbus_ioctl_data);

    i2c->req = req;
    i2c->in_val = in_val;
    i2c->in_buf = in_buf;

    trace_remote_i2c_master_i2cdev_smbus((uint8_t)i2c->ioctl_state);

    switch (i2c->ioctl_state) {
    case I2C_IOCTL_START:
        if (!in_bufsz) {
            fuse_reply_ioctl_retry(req, in_iov, 1, NULL, 0);
            i2c->ioctl_state = I2C_IOCTL_GET;
            return;
        }
        break;
    case I2C_IOCTL_GET:
        /* prepare client buf */
        if (in_val->read_write) {
            struct iovec out_iov = {
                .iov_base = in_val->data,
                .iov_len = sizeof(union i2c_smbus_data *)
            };

            in_iov[1].iov_base = in_val->data;
            in_iov[1].iov_len = sizeof(union i2c_smbus_data *);

            fuse_reply_ioctl_retry(req, in_iov, 2, &out_iov, 1);
            i2c->ioctl_state = I2C_IOCTL_RECV;
        } else {
            in_iov[1].iov_base = in_val->data;
            in_iov[1].iov_len = sizeof(union i2c_smbus_data);
            fuse_reply_ioctl_retry(req, in_iov, 2, NULL, 0);
            i2c->ioctl_state = I2C_IOCTL_SEND;
        }
        break;
    case I2C_IOCTL_RECV:
    case I2C_IOCTL_SEND:
        {
            i2c->is_recv = (i2c->ioctl_state == I2C_IOCTL_RECV);
            if (i2c_bus_busy(i2c_bus)) {
                timer_mod(i2c->timer,
                            qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 5);
            } else {
                i2c_bus_master(i2c_bus, i2c->bh);
                i2c_schedule_pending_master(i2c_bus);
            }
        }
        break;
    case I2C_IOCTL_FINISHED:
        i2c->ioctl_state = I2C_IOCTL_START;
        i2c->last_ioctl = 0;
        break;
    }
}

static void i2cdev_ioctl(fuse_req_t req, int cmd, void *arg,
                          struct fuse_file_info *fi, unsigned flags,
                          const void *in_buf, size_t in_bufsz, size_t out_bufsz)
{
    RemoteI2CControllerState *s = fuse_req_userdata(req);
    unsigned int ctl = cmd;

    trace_remote_i2c_master_i2cdev_ioctl(cmd);

    if (flags & FUSE_IOCTL_COMPAT) {
        fuse_reply_err(req, ENOSYS);
        return;
    }

    if (s->ioctl_state == I2C_IOCTL_START) {
        s->last_ioctl = ctl;
    } else if (s->last_ioctl != ctl) {
        s->last_ioctl = 0;
        s->ioctl_state = I2C_IOCTL_START;
        fuse_reply_err(req, EINVAL);
        return;
    }

    switch (ctl) {
    case I2C_SLAVE_FORCE:
        fuse_reply_ioctl(req, 0, NULL, 0);
        break;
    case I2C_FUNCS:
        i2cdev_functional(s, req, arg, in_buf);
    break;
    case I2C_SLAVE:
        i2cdev_address(s, req, arg, in_buf);
        break;
    case I2C_SMBUS: {
        i2cdev_cmd_smbus(s, req, arg, in_buf, in_bufsz, out_bufsz);
    }
    break;
    default:
        fuse_reply_err(req, EINVAL);
    break;
    }

    if (s->ioctl_state == I2C_IOCTL_FINISHED) {
        s->ioctl_state = I2C_IOCTL_START;
        s->last_ioctl = 0;
        trace_remote_i2c_master_i2cdev_ioctl_finished(cmd);
    }
}

static void i2cdev_poll(fuse_req_t req, struct fuse_file_info *fi,
                         struct fuse_pollhandle *ph)
{
    RemoteI2CControllerState *s = fuse_req_userdata(req);

    s->ph = ph;
    fuse_reply_poll(req, 0);
}

static const struct cuse_lowlevel_ops i2cdev_ops = {
    .init       = i2cdev_init,
    .open       = i2cdev_open,
    .release    = i2cdev_release,
    .read       = i2cdev_read,
    .ioctl      = i2cdev_ioctl,
    .poll       = i2cdev_poll,
};

static void read_from_fuse_export(void *opaque)
{
    RemoteI2CControllerState *s = opaque;
    int ret;

    do {
        ret = fuse_session_receive_buf(s->fuse_session, &s->fuse_buf);
    } while (ret == -EINTR);

    if (ret < 0) {
        return;
    }

    fuse_session_process_buf(s->fuse_session, &s->fuse_buf);

    trace_remote_i2c_master_fuse_io_read();
}

static int i2c_fuse_export(RemoteI2CControllerState *i2c, Error **errp)
{
    struct fuse_session *session = NULL;
    char fuse_opt_dummy[] = FUSE_OPT_DUMMY;
    char fuse_opt_fore[] = FUSE_OPT_FORE;
    char fuse_opt_debug[] = FUSE_OPT_DEBUG;
    char *fuse_argv[] = { fuse_opt_dummy, fuse_opt_fore, fuse_opt_debug };
    char dev_name[128];
    struct cuse_info ci = { 0 };
    char *curdir = get_current_dir_name();
    int ret;

    /* Set device name for CUSE dev info */
    sprintf(dev_name, "DEVNAME=%s", i2c->devname);
    const char *dev_info_argv[] = { dev_name };

    memset(&ci, 0, sizeof(ci));
    ci.dev_major = 0;
    ci.dev_minor = 0;
    ci.dev_info_argc = 1;
    ci.dev_info_argv = dev_info_argv;
    ci.flags = CUSE_UNRESTRICTED_IOCTL;

    int multithreaded;
    session = cuse_lowlevel_setup(ARRAY_SIZE(fuse_argv), fuse_argv, &ci,
                                  &i2cdev_ops, &multithreaded, i2c);
    if (session == NULL) {
        error_setg(errp, "cuse_lowlevel_setup() failed");
        errno = EINVAL;
        return -1;
    }

    /* FIXME: fuse_daemonize() calls chdir("/") */
    ret = chdir(curdir);
    if (ret == -1) {
        error_setg(errp, "chdir() failed");
        return -1;
    }

    i2c->ctx = iohandler_get_aio_context();

    aio_set_fd_handler(i2c->ctx, fuse_session_fd(session),
                       read_from_fuse_export, NULL,
                       NULL, NULL, i2c);

    i2c->fuse_session = session;

    trace_remote_i2c_master_fuse_export();
    return 0;
}

static void remote_i2c_timer_cb(void *opaque)
{
    RemoteI2CControllerState *s = opaque;
    s->is_recv = (s->ioctl_state == I2C_IOCTL_RECV);
    if (i2c_bus_busy(s->i2c_bus)) {
        timer_mod(s->timer,
                  qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 5);
    } else {
        i2c_bus_master(s->i2c_bus, s->bh);
        i2c_schedule_pending_master(s->i2c_bus);
    }
}


static void remote_i2c_bh(void *opaque)
{
    RemoteI2CControllerState *s = opaque;

    if (s->is_recv) {
        recv_data_from_slave(s, s->req, s->in_val, s->in_buf);
    } else {
        send_data_to_slave(s, s->req, s->in_val, s->in_buf);
    }
    i2c_end_transfer(s->i2c_bus);
    i2c_bus_release(s->i2c_bus);

    if (s->ioctl_state == I2C_IOCTL_FINISHED) {
        s->ioctl_state = I2C_IOCTL_START;
        s->last_ioctl = 0;
    }
}

static void remote_i2c_realize(DeviceState *dev, Error **errp)
{
    RemoteI2CControllerState *s = REMOTE_I2C_MASTER(dev);

    s->bh = qemu_bh_new(remote_i2c_bh, s);

    s->timer = timer_new(QEMU_CLOCK_VIRTUAL, SCALE_MS,
                         &remote_i2c_timer_cb, s);

    s->is_open = false;
    i2c_fuse_export(s, errp);
}

static const Property remote_i2c_props[] = {
    DEFINE_PROP_LINK("i2cbus", RemoteI2CControllerState, i2c_bus,
                     TYPE_I2C_BUS, I2CBus *),
    DEFINE_PROP_STRING("devname", RemoteI2CControllerState, devname),
};

static void remote_i2c_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_props(dc, remote_i2c_props);
    dc->realize = remote_i2c_realize;
    dc->desc = "Remote I2C Controller";
}

static const TypeInfo remote_i2c_type = {
    .name = TYPE_REMOTE_I2C_MASTER,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(RemoteI2CControllerState),
    .class_init = remote_i2c_class_init
};

static void remote_i2c_register(void)
{
    type_register_static(&remote_i2c_type);
}

type_init(remote_i2c_register)
