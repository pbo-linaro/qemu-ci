/*
 * Copyright (c) 2025 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_VFIO_VFIO_CPR_H
#define HW_VFIO_VFIO_CPR_H

#include "migration/misc.h"

typedef struct VFIOContainerCPR {
    Error *blocker;
} VFIOContainerCPR;

struct VFIOContainer;

int vfio_cpr_reboot_notifier(NotifierWithReturn *notifier, MigrationEvent *e,
                             Error **errp);

bool vfio_legacy_cpr_register_container(struct VFIOContainer *container,
                                        Error **errp);
void vfio_legacy_cpr_unregister_container(struct VFIOContainer *container);
#endif
