/*
 * SMSC LAN9118 PHY emulation
 *
 * Copyright (c) 2009 CodeSourcery, LLC.
 * Written by Paul Brook
 *
 * Copyright (c) 2013 Jean-Christophe Dubois. <jcd@tribudubois.net>
 *
 * This code is licensed under the GNU GPL v2
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "hw/net/lan9118_phy.h"
#include "hw/irq.h"
#include "hw/resettable.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "trace.h"

#define PHY_INT_ENERGYON            (1 << 7)
#define PHY_INT_AUTONEG_COMPLETE    (1 << 6)
#define PHY_INT_FAULT               (1 << 5)
#define PHY_INT_DOWN                (1 << 4)
#define PHY_INT_AUTONEG_LP          (1 << 3)
#define PHY_INT_PARFAULT            (1 << 2)
#define PHY_INT_AUTONEG_PAGE        (1 << 1)

static void lan9118_phy_update_irq(Lan9118PhyState *s)
{
    qemu_set_irq(s->irq, !!(s->ints & s->int_mask));
}

void lan9118_phy_update_link(Lan9118PhyState *s, bool link_down)
{
    s->link_down = link_down;

    /* Autonegotiation status mirrors link status. */
    if (link_down) {
        trace_lan9118_phy_update_link("down");
        s->status &= ~0x0024;
        s->ints |= PHY_INT_DOWN;
    } else {
        trace_lan9118_phy_update_link("up");
        s->status |= 0x0024;
        s->ints |= PHY_INT_ENERGYON;
        s->ints |= PHY_INT_AUTONEG_COMPLETE;
    }
    lan9118_phy_update_irq(s);
}

static void lan9118_phy_reset(Object *obj, ResetType type)
{
    Lan9118PhyState *s = LAN9118_PHY(obj);

    trace_lan9118_phy_reset();

    s->status = 0x7809;
    s->control = 0x3000;
    s->advertise = 0x01e1;
    s->int_mask = 0;
    s->ints = 0;
    lan9118_phy_update_link(s, s->link_down);
}

uint32_t lan9118_phy_read(Lan9118PhyState *s, int reg)
{
    uint32_t val;

    switch (reg) {
    case 0: /* Basic Control */
        val = s->control;
        break;
    case 1: /* Basic Status */
        val = s->status;
        break;
    case 2: /* ID1 */
        val = 0x0007;
        break;
    case 3: /* ID2 */
        val = 0xc0d1;
        break;
    case 4: /* Auto-neg advertisement */
        val = s->advertise;
        break;
    case 5: /* Auto-neg Link Partner Ability */
        val = 0x0f71;
        break;
    case 6: /* Auto-neg Expansion */
        val = 1;
        break;
    case 29: /* Interrupt source. */
        val = s->ints;
        s->ints = 0;
        lan9118_phy_update_irq(s);
        break;
    case 30: /* Interrupt mask */
        val = s->int_mask;
        break;
    case 17:
    case 18:
    case 27:
    case 31:
        qemu_log_mask(LOG_UNIMP, "%s: reg %d not implemented\n",
                      __func__, reg);
        val = 0;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad address at offset %d\n",
                      __func__, reg);
        val = 0;
        break;
    }

    trace_lan9118_phy_read(val, reg);

    return val;
}

void lan9118_phy_write(Lan9118PhyState *s, int reg, uint32_t val)
{
    trace_lan9118_phy_write(val, reg);

    switch (reg) {
    case 0: /* Basic Control */
        if (val & 0x8000) {
            lan9118_phy_reset(OBJECT(s), RESET_TYPE_SOFT);
        } else {
            s->control = val & 0x7980;
            /* Complete autonegotiation immediately. */
            if (val & 0x1000) {
                s->status |= 0x0020;
            }
        }
        break;
    case 4: /* Auto-neg advertisement */
        s->advertise = (val & 0x2d7f) | 0x80;
        break;
    case 30: /* Interrupt mask */
        s->int_mask = val & 0xff;
        lan9118_phy_update_irq(s);
        break;
    case 17:
    case 18:
    case 27:
    case 31:
        qemu_log_mask(LOG_UNIMP, "%s: reg %d not implemented\n",
                      __func__, reg);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad address at offset %d\n",
                      __func__, reg);
        break;
    }
}

static void lan9118_phy_init(Object *obj)
{
    Lan9118PhyState *s = LAN9118_PHY(obj);

    qdev_init_gpio_out(DEVICE(s), &s->irq, 1);
}

static const VMStateDescription vmstate_lan9118_phy = {
    .name = "lan9118-phy",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(status, Lan9118PhyState),
        VMSTATE_UINT32(control, Lan9118PhyState),
        VMSTATE_UINT32(advertise, Lan9118PhyState),
        VMSTATE_UINT32(ints, Lan9118PhyState),
        VMSTATE_UINT32(int_mask, Lan9118PhyState),
        VMSTATE_BOOL(link_down, Lan9118PhyState),
        VMSTATE_END_OF_LIST()
    }
};

static void lan9118_phy_class_init(ObjectClass *klass, void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    rc->phases.hold = lan9118_phy_reset;
    dc->vmsd = &vmstate_lan9118_phy;
}

static const TypeInfo types[] = {
    {
        .name          = TYPE_LAN9118_PHY,
        .parent        = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(Lan9118PhyState),
        .instance_init = lan9118_phy_init,
        .class_init    = lan9118_phy_class_init,
    }
};

DEFINE_TYPES(types)
