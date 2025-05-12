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

    QLIST_FOREACH(vbasedev, &bcontainer->device_list, container_next) {
        vbasedev->cpr.reused = false;
    }
    container->be->cpr_reused = false;

    return 0;
}

static const VMStateDescription vfio_container_vmstate = {
    .name = "vfio-iommufd-container",
    .version_id = 0,
    .minimum_version_id = 0,
    .post_load = vfio_container_post_load,
    .needed = cpr_needed_for_reuse,
    .fields = (VMStateField[]) {
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription iommufd_cpr_vmstate = {
    .name = "iommufd",
    .version_id = 0,
    .minimum_version_id = 0,
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

static const VMStateDescription vfio_device_vmstate = {
    .name = "vfio-iommufd-device",
    .version_id = 0,
    .minimum_version_id = 0,
    .needed = cpr_needed_for_reuse,
    .fields = (VMStateField[]) {
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
