/*
 * QEMU model for RT500 Reset Controller
 *
 * Copyright (c) 2024 Google LLC
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_MISC_RT500_RSTCTL_H
#define HW_MISC_RT500_RSTCTL_H

#include "hw/arm/svd/rt500_rstctl0.h"
#include "hw/arm/svd/rt500_rstctl1.h"
#include "hw/sysbus.h"

#define TYPE_RT500_RSTCTL "rt500-rstctl"
#define RT500_RSTCTL(o) OBJECT_CHECK(RT500RstCtlState, o, TYPE_RT500_RSTCTL)

#define TYPE_RT500_RSTCTL0 "rt500-rstctl0"
#define TYPE_RT500_RSTCTL1 "rt500-rstctl1"

typedef struct {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    uint32_t regs[RT500_RSTCTL1_REGS_NO];
} RT500RstCtlState;

#endif /* HW_MISC_RT500_RSTCTL_H */
