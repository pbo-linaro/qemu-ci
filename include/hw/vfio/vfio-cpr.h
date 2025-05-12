/*
 * VFIO CPR
 *
 * Copyright (c) 2025 Oracle and/or its affiliates.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_VFIO_VFIO_CPR_H
#define HW_VFIO_VFIO_CPR_H

#include "migration/misc.h"
#include "system/memory.h"

typedef struct VFIOContainerCPR {
    Error *blocker;
    bool reused;
    bool vaddr_unmapped;
    NotifierWithReturn transfer_notifier;
    MemoryListener remap_listener;
} VFIOContainerCPR;

typedef struct VFIODeviceCPR {
    bool reused;
    Error *mdev_blocker;
    Error *id_blocker;
} VFIODeviceCPR;

struct VFIOContainer;
struct VFIOContainerBase;
struct VFIOGroup;
struct VFIOPCIDevice;
struct VFIODevice;
struct VFIOIOMMUFDContainer;

bool vfio_legacy_cpr_register_container(struct VFIOContainer *container,
                                        Error **errp);
void vfio_legacy_cpr_unregister_container(struct VFIOContainer *container);

int vfio_cpr_reboot_notifier(NotifierWithReturn *notifier, MigrationEvent *e,
                             Error **errp);

bool vfio_cpr_register_container(struct VFIOContainerBase *bcontainer,
                                 Error **errp);
void vfio_cpr_unregister_container(struct VFIOContainerBase *bcontainer);

bool vfio_iommufd_cpr_register_container(struct VFIOIOMMUFDContainer *container,
                                         Error **errp);
void vfio_iommufd_cpr_unregister_container(
    struct VFIOIOMMUFDContainer *container);
void vfio_iommufd_cpr_register_device(struct VFIODevice *vbasedev);
void vfio_iommufd_cpr_unregister_device(struct VFIODevice *vbasedev);

bool vfio_cpr_container_match(struct VFIOContainer *container,
                              struct VFIOGroup *group, int *fd);

void vfio_cpr_giommu_remap(struct VFIOContainerBase *bcontainer,
                           MemoryRegionSection *section);

bool vfio_cpr_ram_discard_register_listener(
    struct VFIOContainerBase *bcontainer, MemoryRegionSection *section);

void vfio_cpr_save_vector_fd(struct VFIOPCIDevice *vdev, const char *name,
                             int nr, int fd);
int vfio_cpr_load_vector_fd(struct VFIOPCIDevice *vdev, const char *name,
                            int nr);
void vfio_cpr_delete_vector_fd(struct VFIOPCIDevice *vdev, const char *name,
                               int nr);

extern const VMStateDescription vfio_cpr_pci_vmstate;

bool vfio_cpr_set_device_name(struct VFIODevice *vbasedev, Error **errp);

#endif /* HW_VFIO_VFIO_CPR_H */
