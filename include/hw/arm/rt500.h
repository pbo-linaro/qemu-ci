/*
 * i.MX RT500 platforms.
 *
 * Copyright (c) 2024 Google LLC
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_ARM_RT500_H
#define HW_ARM_RT500_H

#include "hw/arm/armv7m.h"
#include "hw/misc/flexcomm.h"
#include "hw/misc/rt500_clkctl0.h"
#include "hw/misc/rt500_clkctl1.h"
#include "hw/ssi/flexspi.h"
#include "hw/misc/rt500_rstctl.h"

#define TYPE_RT500 "rt500"
#define RT500(obj) OBJECT_CHECK(RT500State, (obj), TYPE_RT500)

#define RT500_FLEXCOMM_NUM (17)
#define RT500_FLEXSPI_NUM (2)
#define RT500_RSTCTL_NUM (2)

typedef struct RT500State {
    SysBusDevice parent_obj;

    ARMv7MState armv7m;
    MemoryRegion *mem;
    FlexcommState flexcomm[RT500_FLEXCOMM_NUM];
    RT500ClkCtl0State clkctl0;
    RT500ClkCtl1State clkctl1;
    FlexSpiState flexspi[RT500_FLEXSPI_NUM];
    RT500RstCtlState rstctl[RT500_RSTCTL_NUM];

    Clock *sysclk;
    Clock *refclk;
} RT500State;

#endif /* HW_ARM_RT500_H */
