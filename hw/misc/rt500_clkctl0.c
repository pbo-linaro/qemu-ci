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

#include "qemu/osdep.h"
#include "hw/clock.h"
#include "hw/irq.h"
#include "hw/qdev-clock.h"
#include "hw/qdev-properties.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "exec/address-spaces.h"
#include "migration/vmstate.h"
#include "hw/misc/rt500_clkctl0.h"
#include "hw/misc/rt500_clk_freqs.h"

#include "trace.h"

#define REG(s, reg) (s->regs[R_RT500_CLKCTL0_##reg])
#define RF_RD(s, reg, field) \
    ARRAY_FIELD_EX32(s->regs, RT500_CLKCTL0_##reg, field)
#define RF_WR(s, reg, field, val) \
    ARRAY_FIELD_DP32(s->regs, RT500_CLKCTL0_##reg, field, val)

static const RT500_CLKCTL0_REGISTER_ACCESS_INFO_ARRAY(reg_info);

static MemTxResult rt500_clkctl0_read(void *opaque, hwaddr addr,
                                      uint64_t *data, unsigned size,
                                      MemTxAttrs attrs)
{
    RT500ClkCtl0State *s = opaque;
    const struct RegisterAccessInfo *rai = &reg_info[addr / 4];

    switch (addr) {
    case A_RT500_CLKCTL0_PSCCTL0_SET:
    case A_RT500_CLKCTL0_PSCCTL1_SET:
    case A_RT500_CLKCTL0_PSCCTL2_SET:
    case A_RT500_CLKCTL0_PSCCTL0_CLR:
    case A_RT500_CLKCTL0_PSCCTL1_CLR:
    case A_RT500_CLKCTL0_PSCCTL2_CLR:
        /* write only registers */
        return MEMTX_ERROR;
    default:
        *data = s->regs[addr / 4];
        break;
    }

    trace_rt500_clkctl0_reg_read(rai->name, addr, *data);
    return MEMTX_OK;
}

static inline void set_systick_clk_from_div(RT500ClkCtl0State *s)
{
    uint32_t div = RF_RD(s, SYSTICKFCLKDIV, DIV) + 1;
    uint32_t rate = clock_get_hz(s->sysclk);

    clock_set_hz(s->systick_clk, rate / div);
}

static MemTxResult rt500_clkctl0_write(void *opaque, hwaddr addr,
                                       uint64_t value, unsigned size,
                                       MemTxAttrs attrs)
{
    RT500ClkCtl0State *s = opaque;
    const struct RegisterAccessInfo *rai = &reg_info[addr / 4];
    struct RegisterInfo ri = {
        .data = &s->regs[addr / 4],
        .data_size = 4,
        .access = rai,
    };

    trace_rt500_clkctl0_reg_write(rai->name, addr, value);

    switch (addr) {
    case A_RT500_CLKCTL0_PSCCTL0:
    case A_RT500_CLKCTL0_PSCCTL1:
    case A_RT500_CLKCTL0_PSCCTL2:
    {
        register_write(&ri, value, ~0, NULL, false);
        break;
    }
    case A_RT500_CLKCTL0_PSCCTL0_SET:
    case A_RT500_CLKCTL0_PSCCTL1_SET:
    case A_RT500_CLKCTL0_PSCCTL2_SET:
    {
        uint32_t tmp;

        tmp = A_RT500_CLKCTL0_PSCCTL0 + (addr - A_RT500_CLKCTL0_PSCCTL0_SET);
        s->regs[tmp / 4] |= value;
        break;
    }
    case A_RT500_CLKCTL0_PSCCTL0_CLR:
    case A_RT500_CLKCTL0_PSCCTL1_CLR:
    case A_RT500_CLKCTL0_PSCCTL2_CLR:
    {
        uint32_t tmp;

        tmp = A_RT500_CLKCTL0_PSCCTL0 + (addr - A_RT500_CLKCTL0_PSCCTL0_CLR);
        s->regs[tmp / 4] &= ~value;
        break;
    }
    default:
        register_write(&ri, value, ~0, NULL, false);
    }

    switch (addr) {
    case A_RT500_CLKCTL0_SYSPLL0PFD:
    {
        if (!RF_RD(s, SYSPLL0PFD, PFD0_CLKGATE)) {
            RF_WR(s, SYSPLL0PFD, PFD0_CLKRDY, 1);
        } else {
            RF_WR(s, SYSPLL0PFD, PFD0_CLKRDY, 0);
        }
        if (!RF_RD(s, SYSPLL0PFD, PFD1_CLKGATE)) {
            RF_WR(s, SYSPLL0PFD, PFD1_CLKRDY, 1);
        } else {
            RF_WR(s, SYSPLL0PFD, PFD1_CLKRDY, 0);
        }
        if (!RF_RD(s, SYSPLL0PFD, PFD2_CLKGATE)) {
            RF_WR(s, SYSPLL0PFD, PFD2_CLKRDY, 1);
        } else {
            RF_WR(s, SYSPLL0PFD, PFD2_CLKRDY, 0);
        }
        if (!RF_RD(s, SYSPLL0PFD, PFD3_CLKGATE)) {
            RF_WR(s, SYSPLL0PFD, PFD3_CLKRDY, 1);
        } else {
            RF_WR(s, SYSPLL0PFD, PFD3_CLKRDY, 0);
        }
        break;
    }
    case A_RT500_CLKCTL0_SYSTICKFCLKSEL:
    {
        switch (RF_RD(s, SYSTICKFCLKSEL, SEL)) {
        case SYSTICKFCLKSEL_DIVOUT:
        {
            set_systick_clk_from_div(s);
            break;
        }
        case SYSTICKFCLKSEL_LPOSC:
        {
            clock_set_hz(s->systick_clk, LPOSC_CLK_HZ);
            break;
        }
        case SYSTICKFCLKSEL_32KHZRTC:
        {
            clock_set_hz(s->systick_clk, RTC32KHZ_CLK_HZ);
            break;
        }
        case SYSTICKFCLKSEL_NONE:
        {
            clock_set_hz(s->systick_clk, 0);
            break;
        }
        }
        clock_propagate(s->systick_clk);
        break;
    }
    case A_RT500_CLKCTL0_SYSTICKFCLKDIV:
    {
        if (RF_RD(s, SYSTICKFCLKSEL, SEL) == SYSTICKFCLKSEL_DIVOUT) {
            set_systick_clk_from_div(s);
            clock_propagate(s->systick_clk);
        }
        break;
    }
    }

    return MEMTX_OK;
}

static const MemoryRegionOps rt500_clkctl0_ops = {
    .read_with_attrs = rt500_clkctl0_read,
    .write_with_attrs = rt500_clkctl0_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static void rt500_clkctl0_reset_enter(Object *obj, ResetType type)
{
    RT500ClkCtl0State *s = RT500_CLKCTL0(obj);

    for (int i = 0; i < RT500_CLKCTL0_REGS_NO; i++) {
        hwaddr addr = reg_info[i].addr;

        if (addr != -1) {
            struct RegisterInfo ri = {
                .data = &s->regs[addr / 4],
                .data_size = 4,
                .access = &reg_info[i],
            };

            register_reset(&ri);
        }
    }

    /* clock OK immediately after reset */
    REG(s, FROCLKSTATUS) = 0x00000001;
}

static void rt500_clkctl0_init(Object *obj)
{
    RT500ClkCtl0State *s = RT500_CLKCTL0(obj);

    memory_region_init_io(&s->mmio, obj, &rt500_clkctl0_ops, s,
                          TYPE_RT500_CLKCTL0, sizeof(s->regs));
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
    s->sysclk = qdev_init_clock_in(DEVICE(s), "sysclk", NULL, NULL, 0);
    s->systick_clk = qdev_init_clock_out(DEVICE(s), "systick_clk");
}

static const VMStateDescription vmstate_rt500_clkctl0 = {
    .name = "rt500-clkctl0",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, RT500ClkCtl0State, RT500_CLKCTL0_REGS_NO),
        VMSTATE_CLOCK(systick_clk, RT500ClkCtl0State),
        VMSTATE_END_OF_LIST()
    }
};

static void rt500_clkctl0_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.enter = rt500_clkctl0_reset_enter;
    dc->vmsd = &vmstate_rt500_clkctl0;
}

static const TypeInfo rt500_clkctl0_types[] = {
    {
        .name          = TYPE_RT500_CLKCTL0,
        .parent        = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(RT500ClkCtl0State),
        .instance_init = rt500_clkctl0_init,
        .class_init    = rt500_clkctl0_class_init,
    },
};

DEFINE_TYPES(rt500_clkctl0_types);

