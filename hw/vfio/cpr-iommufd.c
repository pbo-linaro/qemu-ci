/*
 * Copyright (c) 2024-2025 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/vfio/vfio-cpr.h"
#include "hw/vfio/vfio-device.h"
#include "migration/blocker.h"
#include "migration/cpr.h"
#include "migration/migration.h"
#include "migration/vmstate.h"
#include "system/iommufd.h"
#include "vfio-iommufd.h"

static bool vfio_cpr_supported(VFIOIOMMUFDContainer *container, Error **errp)
{
    if (!iommufd_change_process_capable(container->be)) {
        error_setg(errp,
                   "VFIO container does not support IOMMU_IOAS_CHANGE_PROCESS");
        return false;
    }
    return true;
}

static int vfio_container_post_load(void *opaque, int version_id)
{
    VFIOIOMMUFDContainer *container = opaque;
    VFIOContainerBase *bcontainer = &container->bcontainer;
    VFIODevice *vbasedev;
    Error *err = NULL;
    uint32_t ioas_id = container->ioas_id;

    if (!iommufd_cdev_get_info_iova_range(container, ioas_id, &err)) {
        error_report_err(err);
        return -1;
    }

    QLIST_FOREACH(vbasedev, &bcontainer->device_list, container_next) {
        vbasedev->cpr.reused = false;
    }
    container->be->cpr_reused = false;

    return 0;
}

static int vfio_container_pre_save(void *opaque)
{
    VFIOIOMMUFDContainer *container = opaque;
    Error *err = NULL;

    /*
     * The process has not changed yet, but proactively call the ioctl,
     * and it will fail if any DMA mappings are not supported.
     */
    if (!iommufd_change_process(container->be, &err)) {
        error_report_err(err);
        return -1;
    }
    return 0;
}

static const VMStateDescription vfio_container_vmstate = {
    .name = "vfio-iommufd-container",
    .version_id = 0,
    .minimum_version_id = 0,
    .pre_save = vfio_container_pre_save,
    .post_load = vfio_container_post_load,
    .needed = cpr_needed_for_reuse,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(ioas_id, VFIOIOMMUFDContainer),
        VMSTATE_END_OF_LIST()
    }
};

static int iommufd_cpr_post_load(void *opaque, int version_id)
{
     IOMMUFDBackend *be = opaque;
     Error *err = NULL;

     if (!iommufd_change_process(be, &err)) {
        error_report_err(err);
        return -1;
     }
     return 0;
}

static const VMStateDescription iommufd_cpr_vmstate = {
    .name = "iommufd",
    .version_id = 0,
    .minimum_version_id = 0,
    .post_load = iommufd_cpr_post_load,
    .needed = cpr_needed_for_reuse,
    .fields = (VMStateField[]) {
        VMSTATE_END_OF_LIST()
    }
};

bool vfio_iommufd_cpr_register_container(VFIOIOMMUFDContainer *container,
                                         Error **errp)
{
    VFIOContainerBase *bcontainer = &container->bcontainer;
    Error **cpr_blocker = &container->cpr_blocker;

    migration_add_notifier_mode(&bcontainer->cpr_reboot_notifier,
                                vfio_cpr_reboot_notifier,
                                MIG_MODE_CPR_REBOOT);

    if (!vfio_cpr_supported(container, cpr_blocker)) {
        return migrate_add_blocker_modes(cpr_blocker, errp,
                                         MIG_MODE_CPR_TRANSFER, -1) == 0;
    }

    vmstate_register(NULL, -1, &vfio_container_vmstate, container);
    vmstate_register(NULL, -1, &iommufd_cpr_vmstate, container->be);

    return true;
}

void vfio_iommufd_cpr_unregister_container(VFIOIOMMUFDContainer *container)
{
    VFIOContainerBase *bcontainer = &container->bcontainer;

    vmstate_unregister(NULL, &iommufd_cpr_vmstate, container->be);
    vmstate_unregister(NULL, &vfio_container_vmstate, container);
    migrate_del_blocker(&container->cpr_blocker);
    migration_remove_notifier(&bcontainer->cpr_reboot_notifier);
}

static int vfio_device_post_load(void *opaque, int version_id)
{
    VFIODevice *vbasedev = opaque;
    Error *err = NULL;

    if (!vfio_device_hiod_create_and_realize(vbasedev,
                     TYPE_HOST_IOMMU_DEVICE_IOMMUFD_VFIO, &err)) {
        error_report_err(err);
        return false;
    }
    if (!vbasedev->mdev) {
        VFIOIOMMUFDContainer *container = container_of(vbasedev->bcontainer,
                                                       VFIOIOMMUFDContainer,
                                                       bcontainer);
        iommufd_cdev_rebuild_hwpt(vbasedev, container);
    }
    return true;
}

static const VMStateDescription vfio_device_vmstate = {
    .name = "vfio-iommufd-device",
    .version_id = 0,
    .minimum_version_id = 0,
    .post_load = vfio_device_post_load,
    .needed = cpr_needed_for_reuse,
    .fields = (VMStateField[]) {
        VMSTATE_INT32(devid, VFIODevice),
        VMSTATE_UINT32(cpr.hwpt_id, VFIODevice),
        VMSTATE_END_OF_LIST()
    }
};

void vfio_iommufd_cpr_register_device(VFIODevice *vbasedev)
{
    vmstate_register(NULL, -1, &vfio_device_vmstate, vbasedev);
}

void vfio_iommufd_cpr_unregister_device(VFIODevice *vbasedev)
{
    vmstate_unregister(NULL, &vfio_device_vmstate, vbasedev);
}
