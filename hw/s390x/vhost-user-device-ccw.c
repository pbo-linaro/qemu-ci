/*
 * Vhost-user generic device virtio-ccw glue
 *
 * Copyright (c) 2025 Linaro Ltd.
 * Author: Alex Bennée <alex.bennee@linaro.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/qdev-properties.h"
#include "hw/virtio/vhost-user-base.h"
#include "virtio-ccw.h"

struct VHostUserDeviceCCW {
    VirtioCcwDevice parent_obj;

    VHostUserBase vub;
};

static const Property vhost_user_ccw_properties[] = {
    DEFINE_PROP_BIT("ioeventfd", VirtioCcwDevice, flags,
                    VIRTIO_CCW_FLAG_USE_IOEVENTFD_BIT, true),
    DEFINE_PROP_UINT32("max_revision", VirtioCcwDevice, max_rev,
                       VIRTIO_CCW_MAX_REV),
};

#define TYPE_VHOST_USER_DEVICE_CCW "vhost-user-device-ccw"

OBJECT_DECLARE_SIMPLE_TYPE(VHostUserDeviceCCW, VHOST_USER_DEVICE_CCW)

static void vhost_user_device_ccw_realize(VirtioCcwDevice *ccw_dev, Error **errp)
{
    VHostUserDeviceCCW *dev = VHOST_USER_DEVICE_CCW(ccw_dev);
    DeviceState *vdev = DEVICE(&dev->vub);

    qdev_realize(vdev, BUS(&ccw_dev->bus), errp);
}

static void vhost_user_device_ccw_instance_init(Object *obj)
{
    VHostUserDeviceCCW *dev = VHOST_USER_DEVICE_CCW(obj);
    VirtioCcwDevice *ccw_dev = VIRTIO_CCW_DEVICE(obj);

    ccw_dev->force_revision_1 = true;
    virtio_instance_init_common(obj, &dev->vub, sizeof(dev->vub),
                                TYPE_VHOST_USER_DEVICE);
}

static void vhost_user_device_ccw_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtIOCCWDeviceClass *k = VIRTIO_CCW_DEVICE_CLASS(klass);

    /* Reason: stop users confusing themselves */
    /* dc->user_creatable = false; */

    k->realize = vhost_user_device_ccw_realize;
    device_class_set_props(dc, vhost_user_ccw_properties);
}

static const TypeInfo vhost_user_device_ccw = {
    .name          = TYPE_VHOST_USER_DEVICE_CCW,
    .parent        = TYPE_VIRTIO_CCW_DEVICE,
    .instance_size = sizeof(VHostUserDeviceCCW),
    .instance_init = vhost_user_device_ccw_instance_init,
    .class_init    = vhost_user_device_ccw_class_init,
};

static void vhost_user_device_ccw_register(void)
{
    type_register_static(&vhost_user_device_ccw);
}

type_init(vhost_user_device_ccw_register)
