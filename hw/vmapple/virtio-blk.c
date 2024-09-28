/*
 * VMApple specific VirtIO Block implementation
 *
 * Copyright © 2023 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * VMApple uses almost standard VirtIO Block, but with a few key differences:
 *
 *  - Different PCI device/vendor ID
 *  - An additional "type" identifier to differentiate AUX and Root volumes
 *  - An additional BARRIER command
 */

#include "qemu/osdep.h"
#include "hw/vmapple/virtio-blk.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qapi/error.h"

#define VIRTIO_BLK_T_APPLE_BARRIER     0x10000

#define VIRTIO_APPLE_TYPE_ROOT 1
#define VIRTIO_APPLE_TYPE_AUX  2

static bool vmapple_virtio_blk_handle_unknown_request(VirtIOBlockReq *req,
                                                      MultiReqBuffer *mrb,
                                                      uint32_t type)
{
    switch (type) {
    case VIRTIO_BLK_T_APPLE_BARRIER:
        /* We ignore barriers for now. YOLO. */
        virtio_blk_req_complete(req, VIRTIO_BLK_S_OK);
        virtio_blk_free_request(req);
        return true;
    default:
        return false;
    }
}

/*
 * VMApple virtio-blk uses the same config format as normal virtio, with one
 * exception: It adds an "apple type" specififer at the same location that
 * the spec reserves for max_secure_erase_sectors. Let's hook into the
 * get_config code path here, run it as usual and then patch in the apple type.
 */
static void vmapple_virtio_blk_get_config(VirtIODevice *vdev, uint8_t *config)
{
    VMAppleVirtIOBlk *dev = VMAPPLE_VIRTIO_BLK(vdev);
    VMAppleVirtIOBlkClass *vvbk = VMAPPLE_VIRTIO_BLK_GET_CLASS(dev);
    struct virtio_blk_config *blkcfg = (struct virtio_blk_config *)config;

    vvbk->get_config(vdev, config);

    g_assert(dev->parent_obj.config_size >= endof(struct virtio_blk_config, zoned));

    /* Apple abuses the field for max_secure_erase_sectors as type id */
    blkcfg->max_secure_erase_sectors = dev->apple_type;
}

static Property vmapple_virtio_blk_properties[] = {
    DEFINE_PROP_UINT32("apple-type", VMAppleVirtIOBlk, apple_type, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void vmapple_virtio_blk_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtIOBlkClass *vbk = VIRTIO_BLK_CLASS(klass);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_CLASS(klass);
    VMAppleVirtIOBlkClass *vvbk = VMAPPLE_VIRTIO_BLK_CLASS(klass);

    vbk->handle_unknown_request = vmapple_virtio_blk_handle_unknown_request;
    vvbk->get_config = vdc->get_config;
    vdc->get_config = vmapple_virtio_blk_get_config;
    device_class_set_props(dc, vmapple_virtio_blk_properties);
}

static const TypeInfo vmapple_virtio_blk_info = {
    .name          = TYPE_VMAPPLE_VIRTIO_BLK,
    .parent        = TYPE_VIRTIO_BLK,
    .instance_size = sizeof(VMAppleVirtIOBlk),
    .class_init    = vmapple_virtio_blk_class_init,
};

/* PCI Devices */

typedef struct VMAppleVirtIOBlkPCI {
    VirtIOPCIProxy parent_obj;
    VMAppleVirtIOBlk vdev;
    uint32_t apple_type;
} VMAppleVirtIOBlkPCI;

/*
 * vmapple-virtio-blk-pci: This extends VirtioPCIProxy.
 */
#define TYPE_VMAPPLE_VIRTIO_BLK_PCI "vmapple-virtio-blk-pci-base"
DECLARE_INSTANCE_CHECKER(VMAppleVirtIOBlkPCI, VMAPPLE_VIRTIO_BLK_PCI,
                         TYPE_VMAPPLE_VIRTIO_BLK_PCI)

static Property vmapple_virtio_blk_pci_properties[] = {
    DEFINE_PROP_UINT32("class", VirtIOPCIProxy, class_code, 0),
    DEFINE_PROP_BIT("ioeventfd", VirtIOPCIProxy, flags,
                    VIRTIO_PCI_FLAG_USE_IOEVENTFD_BIT, true),
    DEFINE_PROP_UINT32("vectors", VirtIOPCIProxy, nvectors,
                       DEV_NVECTORS_UNSPECIFIED),
    DEFINE_PROP_END_OF_LIST(),
};

static void vmapple_virtio_blk_pci_realize(VirtIOPCIProxy *vpci_dev, Error **errp)
{
    VMAppleVirtIOBlkPCI *dev = VMAPPLE_VIRTIO_BLK_PCI(vpci_dev);
    DeviceState *vdev = DEVICE(&dev->vdev);
    VirtIOBlkConf *conf = &dev->vdev.parent_obj.conf;

    if (conf->num_queues == VIRTIO_BLK_AUTO_NUM_QUEUES) {
        conf->num_queues = virtio_pci_optimal_num_queues(0);
    }

    if (vpci_dev->nvectors == DEV_NVECTORS_UNSPECIFIED) {
        vpci_dev->nvectors = conf->num_queues + 1;
    }

    /*
     * We don't support zones, but we need the additional config space size.
     * Let's just expose the feature so the rest of the virtio-blk logic
     * allocates enough space for us. The guest will ignore zones anyway.
     */
    virtio_add_feature(&dev->vdev.parent_obj.host_features, VIRTIO_BLK_F_ZONED);
    /* Propagate the apple type down to the virtio-blk device */
    qdev_prop_set_uint32(DEVICE(&dev->vdev), "apple-type", dev->apple_type);
    /* and spawn the virtio-blk device */
    qdev_realize(vdev, BUS(&vpci_dev->bus), errp);

    /*
     * The virtio-pci machinery adjusts its vendor/device ID based on whether
     * we support modern or legacy virtio. Let's patch it back to the Apple
     * identifiers here.
     */
    pci_config_set_vendor_id(vpci_dev->pci_dev.config, PCI_VENDOR_ID_APPLE);
    pci_config_set_device_id(vpci_dev->pci_dev.config, PCI_DEVICE_ID_APPLE_VIRTIO_BLK);
}

static void vmapple_virtio_blk_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioPCIClass *k = VIRTIO_PCI_CLASS(klass);
    PCIDeviceClass *pcidev_k = PCI_DEVICE_CLASS(klass);

    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
    device_class_set_props(dc, vmapple_virtio_blk_pci_properties);
    k->realize = vmapple_virtio_blk_pci_realize;
    pcidev_k->vendor_id = PCI_VENDOR_ID_APPLE;
    pcidev_k->device_id = PCI_DEVICE_ID_APPLE_VIRTIO_BLK;
    pcidev_k->revision = VIRTIO_PCI_ABI_VERSION;
    pcidev_k->class_id = PCI_CLASS_STORAGE_SCSI;
}

static void vmapple_virtio_blk_pci_instance_init(Object *obj)
{
    VMAppleVirtIOBlkPCI *dev = VMAPPLE_VIRTIO_BLK_PCI(obj);

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VMAPPLE_VIRTIO_BLK);
}

static const VirtioPCIDeviceTypeInfo vmapple_virtio_blk_pci_info = {
    .base_name     = TYPE_VMAPPLE_VIRTIO_BLK_PCI,
    .generic_name  = "vmapple-virtio-blk-pci",
    .instance_size = sizeof(VMAppleVirtIOBlkPCI),
    .instance_init = vmapple_virtio_blk_pci_instance_init,
    .class_init    = vmapple_virtio_blk_pci_class_init,
};

static void vmapple_virtio_root_instance_init(Object *obj)
{
    VMAppleVirtIOBlkPCI *dev = VMAPPLE_VIRTIO_BLK_PCI(obj);

    dev->apple_type = VIRTIO_APPLE_TYPE_ROOT;
}

static const TypeInfo vmapple_virtio_root_info = {
    .name          = TYPE_VMAPPLE_VIRTIO_ROOT,
    .parent        = "vmapple-virtio-blk-pci",
    .instance_size = sizeof(VMAppleVirtIOBlkPCI),
    .instance_init = vmapple_virtio_root_instance_init,
};

static void vmapple_virtio_aux_instance_init(Object *obj)
{
    VMAppleVirtIOBlkPCI *dev = VMAPPLE_VIRTIO_BLK_PCI(obj);

    dev->apple_type = VIRTIO_APPLE_TYPE_AUX;
}

static const TypeInfo vmapple_virtio_aux_info = {
    .name          = TYPE_VMAPPLE_VIRTIO_AUX,
    .parent        = "vmapple-virtio-blk-pci",
    .instance_size = sizeof(VMAppleVirtIOBlkPCI),
    .instance_init = vmapple_virtio_aux_instance_init,
};

static void vmapple_virtio_blk_register_types(void)
{
    type_register_static(&vmapple_virtio_blk_info);
    virtio_pci_types_register(&vmapple_virtio_blk_pci_info);
    type_register_static(&vmapple_virtio_root_info);
    type_register_static(&vmapple_virtio_aux_info);
}

type_init(vmapple_virtio_blk_register_types)