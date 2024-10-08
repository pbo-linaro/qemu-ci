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


#ifndef HW_MISC_RT500_CLKCTL1_H
#define HW_MISC_RT500_CLKCTL1_H

#include "hw/arm/svd/rt500_clkctl1.h"
#include "hw/sysbus.h"

#define TYPE_RT500_CLKCTL1 "rt500-clkctl1"
#define RT500_CLKCTL1(o) OBJECT_CHECK(RT500ClkCtl1State, o, TYPE_RT500_CLKCTL1)

#define OSEVENTTFCLKSEL_LPOSC 0
#define OSEVENTTFCLKSEL_32KHZRTC 1
#define OSEVENTTFCLKSEL_HCLK 2
#define OSEVENTTFCLKSEL_NONE 7

typedef struct {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    uint32_t regs[RT500_CLKCTL1_REGS_NO];
    Clock *sysclk;
    Clock *ostimer_clk;
} RT500ClkCtl1State;

#endif /* HW_MISC_RT500_CLKCTL1_H */
