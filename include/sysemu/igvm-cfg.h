/*
 * QEMU IGVM interface
 *
 * Copyright (C) 2024 SUSE
 *
 * Authors:
 *  Roy Hopkins <roy.hopkins@suse.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef QEMU_IGVM_CFG_H
#define QEMU_IGVM_CFG_H

#include "qom/object.h"

typedef struct IgvmCfg {
    ObjectClass parent_class;

    /*
     * filename: Filename that specifies a file that contains the configuration
     *           of the guest in Independent Guest Virtual Machine (IGVM)
     *           format.
     */
    char *filename;
} IgvmCfg;

typedef struct IgvmCfgClass {
    ObjectClass parent_class;

    /*
     * If an IGVM filename has been specified then process the IGVM file.
     * Performs a no-op if no filename has been specified.
     *
     * Returns 0 for ok and -1 on error.
     */
    int (*process)(IgvmCfg *cfg, ConfidentialGuestSupport *cgs,
                   Error **errp);

} IgvmCfgClass;

#define TYPE_IGVM_CFG "igvm-cfg"

OBJECT_DECLARE_TYPE(IgvmCfg, IgvmCfgClass, IGVM_CFG)

#endif