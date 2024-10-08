/*
 * QEMU model for RT500 Clock Controller
 *
 * Copyright (c) 2024 Google LLC
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_MISC_RT500_CLKCTL0_H
#define HW_MISC_RT500_CLKCTL0_H

#include "hw/arm/svd/rt500_clkctl0.h"
#include "hw/sysbus.h"

#define TYPE_RT500_CLKCTL0 "rt500-clkctl0"
#define RT500_CLKCTL0(o) OBJECT_CHECK(RT500ClkCtl0State, o, TYPE_RT500_CLKCTL0)

#define SYSTICKFCLKSEL_DIVOUT 0
#define SYSTICKFCLKSEL_LPOSC 1
#define SYSTICKFCLKSEL_32KHZRTC 2
#define SYSTICKFCLKSEL_NONE 7

typedef struct {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    uint32_t regs[RT500_CLKCTL0_REGS_NO];
    Clock *systick_clk;
    Clock *sysclk;
} RT500ClkCtl0State;

#endif /* HW_MISC_RT500_CLKCTL0_H */
