/*
 * Copyright (c) 2025 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_VFIO_VFIO_CPR_H
#define HW_VFIO_VFIO_CPR_H

#include "exec/memory.h"
#include "migration/misc.h"

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
} VFIODeviceCPR;

struct VFIOContainer;
struct VFIOGroup;
struct VFIOContainerBase;

int vfio_cpr_reboot_notifier(NotifierWithReturn *notifier, MigrationEvent *e,
                             Error **errp);

bool vfio_legacy_cpr_register_container(struct VFIOContainer *container,
                                        Error **errp);
void vfio_legacy_cpr_unregister_container(struct VFIOContainer *container);

bool vfio_cpr_container_match(struct VFIOContainer *container,
                              struct VFIOGroup *group, int *fd);

void vfio_cpr_giommu_remap(struct VFIOContainerBase *bcontainer,
                           MemoryRegionSection *section);

bool vfio_cpr_register_ram_discard_listener(
    struct VFIOContainerBase *bcontainer, MemoryRegionSection *section);

extern const VMStateDescription vfio_cpr_pci_vmstate;
#endif
