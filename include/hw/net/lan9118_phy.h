/*
 * SMSC LAN9118 PHY emulation
 *
 * Copyright (c) 2009 CodeSourcery, LLC.
 * Written by Paul Brook
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_NET_LAN9118_PHY_H
#define HW_NET_LAN9118_PHY_H

#include "qom/object.h"
#include "hw/sysbus.h"

#define TYPE_LAN9118_PHY "lan9118-phy"
OBJECT_DECLARE_SIMPLE_TYPE(Lan9118PhyState, LAN9118_PHY)

typedef struct Lan9118PhyState {
    SysBusDevice parent_obj;

    uint32_t status;
    uint32_t control;
    uint32_t advertise;
    uint32_t ints;
    uint32_t int_mask;
    qemu_irq irq;
    bool link_down;
} Lan9118PhyState;

void lan9118_phy_update_link(Lan9118PhyState *s, bool link_down);
uint32_t lan9118_phy_read(Lan9118PhyState *s, int reg);
void lan9118_phy_write(Lan9118PhyState *s, int reg, uint32_t val);

#endif
