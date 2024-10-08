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

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "exec/address-spaces.h"
#include "migration/vmstate.h"
#include "hw/misc/rt500_rstctl.h"

#include "trace.h"

/*
 * There are two intances for RSTCTL with the same register names and layout but
 * with different fields.
 */
#define BUILD_BUG_REG_ADDR(reg) \
    QEMU_BUILD_BUG_ON((int)A_RT500_RSTCTL0_##reg != (int)A_RT500_RSTCTL1_##reg)

#define REG(s, reg) (s->regs[R_RT500_RSTCTL0_##reg])
#define RF_WR(s, reg, field, val) \
    ARRAY_FIELD_DP32(s->regs, RT500_RSTCTL0_##reg, field, val)
#define RF_RD(s, reg, field) \
    ARRAY_FIELD_EX32(s->regs, RT500_RSTCTL0_##reg, field)

#define RSTCTL_SYSRSTSTAT_WMASK (BITS(7, 4) | BIT(0))
#define RSTCL0_PRSCTL0_WMASK (BITS(30, 26) | BITS(24, 20) | BIT(18) | \
                              BIT(16) | BITS(12, 8) | BIT(3) | BIT(1))
#define RSTCL0_PRSCTL1_WMASK (BIT(24) | BITS(16, 15) | BITS(3, 2))
#define RSTCL0_PRSCTL2_WMASK (BITS(1, 0))
#define RSTCL1_PRSCTL0_WMASK (BIT(29) | BIT(27) |  BITS(25, 8))
#define RSTCL1_PRSCTL1_WMASK (BIT(31) | BITS(29, 28) | BITS(24, 23) | \
                              BIT(16) | BITS(7, 0))
#define RSTCL1_PRSCTL2_WMASK (BITS(31, 30) | BITS(17, 16) | BIT(10) | \
                              BIT(8) | BITS(4, 0))


/*
 * The two RSTCLK modules have different write register masks.
 */
typedef struct {
    SysBusDeviceClass parent;
    const struct RegisterAccessInfo *reg_info;
    int reg_info_num;
} RT500RstCtlClass;

#define RT500_RSTCTL_CLASS(klass) \
    OBJECT_CLASS_CHECK(RT500RstCtlClass, (klass), TYPE_RT500_RSTCTL)
#define RT500_RSTCTL_GET_CLASS(obj) \
    OBJECT_GET_CLASS(RT500RstCtlClass, (obj), TYPE_RT500_RSTCTL)

BUILD_BUG_REG_ADDR(SYSRSTSTAT);
BUILD_BUG_REG_ADDR(PRSTCTL0);
BUILD_BUG_REG_ADDR(PRSTCTL1);
BUILD_BUG_REG_ADDR(PRSTCTL2);
BUILD_BUG_REG_ADDR(PRSTCTL0_SET);
BUILD_BUG_REG_ADDR(PRSTCTL1_SET);
BUILD_BUG_REG_ADDR(PRSTCTL2_SET);
BUILD_BUG_REG_ADDR(PRSTCTL0_CLR);
BUILD_BUG_REG_ADDR(PRSTCTL1_CLR);
BUILD_BUG_REG_ADDR(PRSTCTL2_CLR);

static MemTxResult rt500_rstctl_read(void *opaque, hwaddr addr,
                                     uint64_t *data, unsigned size,
                                     MemTxAttrs attrs)
{
    RT500RstCtlState *s = opaque;
    RT500RstCtlClass *c = RT500_RSTCTL_GET_CLASS(s);
    const struct RegisterAccessInfo *rai = &c->reg_info[addr / 4];
    MemTxResult ret = MEMTX_OK;

    switch (addr) {
    case A_RT500_RSTCTL0_SYSRSTSTAT:
    case A_RT500_RSTCTL0_PRSTCTL0:
    case A_RT500_RSTCTL0_PRSTCTL1:
    case A_RT500_RSTCTL0_PRSTCTL2:
        *data = s->regs[addr / 4];
        break;
    default:
        ret = MEMTX_ERROR;
    }

    trace_rt500_rstctl_reg_read(DEVICE(s)->id, rai->name, addr, *data);
    return ret;
}

static MemTxResult rt500_rstctl_write(void *opaque, hwaddr addr,
                                      uint64_t value, unsigned size,
                                      MemTxAttrs attrs)
{
    RT500RstCtlState *s = opaque;
    RT500RstCtlClass *c = RT500_RSTCTL_GET_CLASS(s);
    const struct RegisterAccessInfo *rai = &c->reg_info[addr / 4];
    struct RegisterInfo ri = {
        .data = &s->regs[addr / 4],
        .data_size = 4,
        .access = rai,
    };

    trace_rt500_rstctl_reg_write(DEVICE(s)->id, rai->name, addr, value);

    switch (addr) {
    case A_RT500_RSTCTL0_SYSRSTSTAT:
    {
        /* write 1 to clear bits */
        REG(s, SYSRSTSTAT) &= ~value;
        break;
    }
    case A_RT500_RSTCTL0_PRSTCTL0:
    case A_RT500_RSTCTL0_PRSTCTL1:
    case A_RT500_RSTCTL0_PRSTCTL2:
    {
        register_write(&ri, value, ~0, NULL, false);
        break;
    }
    case A_RT500_RSTCTL0_PRSTCTL0_SET:
    case A_RT500_RSTCTL0_PRSTCTL1_SET:
    case A_RT500_RSTCTL0_PRSTCTL2_SET:
    {
        uint32_t tmp;

        tmp = A_RT500_RSTCTL0_PRSTCTL0 + (addr - A_RT500_RSTCTL0_PRSTCTL0_SET);
        s->regs[tmp / 4] |= value;
        break;
    }
    case A_RT500_RSTCTL0_PRSTCTL0_CLR:
    case A_RT500_RSTCTL0_PRSTCTL1_CLR:
    case A_RT500_RSTCTL0_PRSTCTL2_CLR:
    {
        uint32_t tmp;

        tmp = A_RT500_RSTCTL0_PRSTCTL0 + (addr - A_RT500_RSTCTL0_PRSTCTL0_CLR);
        s->regs[tmp / 4] &= ~value;
        break;
    }
    }

    return MEMTX_OK;
}

static const MemoryRegionOps rt500_rstctl_ops = {
    .read_with_attrs = rt500_rstctl_read,
    .write_with_attrs = rt500_rstctl_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static void rt500_rstctl_reset_enter(Object *obj, ResetType type)
{
    RT500RstCtlState *s = RT500_RSTCTL(obj);
    RT500RstCtlClass *c = RT500_RSTCTL_GET_CLASS(s);

    for (int i = 0; i < c->reg_info_num; i++) {
        hwaddr addr = c->reg_info[i].addr;

        if (addr != -1) {
            struct RegisterInfo ri = {
                .data = &s->regs[addr / 4],
                .data_size = 4,
                .access = &c->reg_info[i],
            };

            register_reset(&ri);
        }
    }
}

static void rt500_rstctl_init(Object *obj)
{
    RT500RstCtlState *s = RT500_RSTCTL(obj);

    memory_region_init_io(&s->mmio, obj, &rt500_rstctl_ops, s,
                          TYPE_RT500_RSTCTL, sizeof(s->regs));
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
}

static const VMStateDescription vmstate_rt500_rstcl0 = {
    .name = "rt500-rstctl0",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, RT500RstCtlState, RT500_RSTCTL0_REGS_NO),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_rt500_rstcl1 = {
    .name = "rt500-rstctl1",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, RT500RstCtlState, RT500_RSTCTL1_REGS_NO),
        VMSTATE_END_OF_LIST()
    }
};

static void rt500_rstctl0_class_init(ObjectClass *klass, void *data)
{
    RT500RstCtlClass *rc = RT500_RSTCTL_CLASS(klass);
    static const RT500_RSTCTL0_REGISTER_ACCESS_INFO_ARRAY(reg_info);
    DeviceClass *dc = DEVICE_CLASS(klass);

    RESETTABLE_CLASS(klass)->phases.enter = rt500_rstctl_reset_enter;
    dc->vmsd = &vmstate_rt500_rstcl0;
    rc->reg_info = reg_info;
    rc->reg_info_num = ARRAY_SIZE(reg_info);
}

static void rt500_rstctl1_class_init(ObjectClass *klass, void *data)
{
    RT500RstCtlClass *rc = RT500_RSTCTL_CLASS(klass);
    static const RT500_RSTCTL1_REGISTER_ACCESS_INFO_ARRAY(reg_info);
    DeviceClass *dc = DEVICE_CLASS(klass);

    RESETTABLE_CLASS(klass)->phases.enter = rt500_rstctl_reset_enter;
    dc->vmsd = &vmstate_rt500_rstcl1;
    rc->reg_info = reg_info;
    rc->reg_info_num = ARRAY_SIZE(reg_info);
}

static const TypeInfo rt500_rstctl_types[] = {
    {
        .name          = TYPE_RT500_RSTCTL,
        .parent        = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(RT500RstCtlState),
        .instance_init = rt500_rstctl_init,
        .abstract      = true,
    },
    {
        .name          = TYPE_RT500_RSTCTL0,
        .parent        = TYPE_RT500_RSTCTL,
        .class_init    = rt500_rstctl0_class_init,
        .class_size    = sizeof(RT500RstCtlClass),
    },
    {
        .name          = TYPE_RT500_RSTCTL1,
        .parent        = TYPE_RT500_RSTCTL,
        .class_init    = rt500_rstctl1_class_init,
        .class_size    = sizeof(RT500RstCtlClass),
    },
};

DEFINE_TYPES(rt500_rstctl_types);
