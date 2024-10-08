/*
 * QEMU model for FLEXSPI
 *
 * Copyright (c) 2024 Google LLC
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/mmap-alloc.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/units.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "migration/vmstate.h"
#include "exec/address-spaces.h"
#include "hw/ssi/flexspi.h"
#include "hw/arm/svd/flexspi.h"

#include "trace.h"

#define REG(s, reg) (s->regs[R_FLEXSPI_##reg])
#define RF_WR(s, reg, field, val) \
    ARRAY_FIELD_DP32(s->regs, FLEXSPI_##reg, field, val)
#define RF_RD(s, reg, field) \
    ARRAY_FIELD_EX32(s->regs, FLEXSPI_##reg, field)

static FLEXSPI_REGISTER_ACCESS_INFO_ARRAY(reg_info);

static void flexspi_reset_enter(Object *obj, ResetType type)
{
    FlexSpiState *s = FLEXSPI(obj);

    for (int i = 0; i < FLEXSPI_REGS_NO; i++) {
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

    /* idle immediately after reset */
    RF_WR(s, STS0, SEQIDLE, 1);
}

static MemTxResult flexspi_read(void *opaque, hwaddr addr,
                                     uint64_t *data, unsigned size,
                                     MemTxAttrs attrs)
{
    FlexSpiState *s = opaque;
    const struct RegisterAccessInfo *rai = &reg_info[addr / 4];
    MemTxResult ret = MEMTX_OK;

    switch (addr) {
    default:
        *data = s->regs[addr / 4];
        break;
    }

    trace_flexspi_reg_read(DEVICE(s)->id, rai->name, addr, *data);
    return ret;
}


static MemTxResult flexspi_write(void *opaque, hwaddr addr,
                                      uint64_t value, unsigned size,
                                      MemTxAttrs attrs)
{
    FlexSpiState *s = opaque;
    const struct RegisterAccessInfo *rai = &reg_info[addr / 4];
    struct RegisterInfo ri = {
        .data = &s->regs[addr / 4],
        .data_size = 4,
        .access = rai,
    };

    trace_flexspi_reg_write(DEVICE(s)->id, rai->name, addr, value);

    switch (addr) {
    case A_FLEXSPI_MCR0:
    {
        register_write(&ri, value, ~0, NULL, false);

        if (RF_RD(s, MCR0, SWRESET)) {
            RF_WR(s, MCR0, SWRESET, 0);
        }
        break;
    }
    case A_FLEXSPI_INTR:
    {
        /* fake SPI transfer completion */
        RF_WR(s, INTR, IPCMDDONE, 1);
        break;
    }
    default:
        register_write(&ri, value, ~0, NULL, false);
        break;
    }

    return MEMTX_OK;
}

static const MemoryRegionOps flexspi_ops = {
    .read_with_attrs = flexspi_read,
    .write_with_attrs = flexspi_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static Property flexspi_properties[] = {
    DEFINE_PROP_UINT64("mmap_size", FlexSpiState, mmap_size, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void flexspi_init(Object *obj)
{
    FlexSpiState *s = FLEXSPI(obj);

    memory_region_init_io(&s->mmio, obj, &flexspi_ops, s, TYPE_FLEXSPI,
                          sizeof(s->regs));
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
}

static void flexspi_realize(DeviceState *dev, Error **errp)
{
    FlexSpiState *s = FLEXSPI(dev);

    if (s->mmap_size) {
        memory_region_init_ram(&s->mem, OBJECT(s), DEVICE(s)->id, s->mmap_size,
                               NULL);
        sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->mem);
    }
}

static const VMStateDescription vmstate_flexspi = {
    .name = "flexspi",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, FlexSpiState, FLEXSPI_REGS_NO),
        VMSTATE_END_OF_LIST()
    }
};

static void flexspi_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.enter = flexspi_reset_enter;
    dc->realize = flexspi_realize;
    dc->vmsd = &vmstate_flexspi;
    device_class_set_props(dc, flexspi_properties);
}

static const TypeInfo flexspi_types[] = {
    {
        .name          = TYPE_FLEXSPI,
        .parent        = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(FlexSpiState),
        .instance_init = flexspi_init,
        .class_init    = flexspi_class_init,
    },
};

DEFINE_TYPES(flexspi_types);
