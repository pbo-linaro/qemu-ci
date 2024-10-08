/*
 * QEMU model for NXP's FLEXCOMM I2C
 *
 * Copyright (c) 2024 Google LLC
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "exec/address-spaces.h"
#include "qapi/error.h"
#include "trace.h"
#include "hw/i2c/flexcomm_i2c.h"
#include "hw/arm/svd/flexcomm_i2c.h"

#define REG(s, reg) (s->regs[R_FLEXCOMM_I2C_##reg])
#define RF_WR(s, reg, field, val) \
    ARRAY_FIELD_DP32(s->regs, FLEXCOMM_I2C_##reg, field, val)
#define RF_RD(s, reg, field) \
    ARRAY_FIELD_EX32(s->regs, FLEXCOMM_I2C_##reg, field)

static FLEXCOMM_I2C_REGISTER_ACCESS_INFO_ARRAY(reg_info);

static void flexcomm_i2c_reset(FlexcommFunction *f)
{
    for (int i = 0; i < FLEXCOMM_I2C_REGS_NO; i++) {
        hwaddr addr = reg_info[i].addr;

        if (addr != -1) {
            struct RegisterInfo ri = {
                .data = &f->regs[addr / 4],
                .data_size = 4,
                .access = &reg_info[i],
            };

            register_reset(&ri);
        }
    }
}

static void flexcomm_i2c_irq_update(FlexcommFunction *f)
{
    bool enabled = RF_RD(f, CFG, MSTEN);
    bool irq, per_irqs;

    REG(f, INTSTAT) = REG(f, STAT) & REG(f, INTENSET);
    per_irqs = REG(f, INTSTAT) != 0;

    irq = enabled && per_irqs;

    trace_flexcomm_i2c_irq(DEVICE(f)->id, irq, per_irqs, enabled);
    flexcomm_set_irq(f, irq);
}

static MemTxResult flexcomm_i2c_reg_read(void *opaque, hwaddr addr,
                                         uint64_t *data, unsigned size,
                                         MemTxAttrs attrs)
{
    FlexcommFunction *f = FLEXCOMM_FUNCTION(opaque);
    MemTxResult ret = MEMTX_OK;
    const struct RegisterAccessInfo *rai = &reg_info[addr / 4];

    if (size != 4) {
        ret = MEMTX_ERROR;
        goto out;
    }

    *data = f->regs[addr / 4];

    flexcomm_i2c_irq_update(f);

out:
    trace_flexcomm_i2c_reg_read(DEVICE(f)->id, rai->name, addr, *data);
    return ret;
}

static MemTxResult flexcomm_i2c_reg_write(void *opaque, hwaddr addr,
                                          uint64_t value, unsigned size,
                                          MemTxAttrs attrs)
{
    FlexcommFunction *f = FLEXCOMM_FUNCTION(opaque);
    FlexcommI2cState *s = FLEXCOMM_I2C(opaque);
    const struct RegisterAccessInfo *rai = &reg_info[addr / 4];
    struct RegisterInfo ri = {
        .data = &f->regs[addr / 4],
        .data_size = 4,
        .access = rai,
    };

    trace_flexcomm_i2c_reg_write(DEVICE(f)->id, rai->name, addr, value);

    if (size != 4) {
        return MEMTX_ERROR;
    }

    switch (addr) {
    case A_FLEXCOMM_I2C_CFG:
    {
        register_write(&ri, value, ~0, NULL, false);
        if (RF_RD(f, CFG, SLVEN)) {
            qemu_log_mask(LOG_GUEST_ERROR, "I2C slave not supported");
        }
        if (RF_RD(f, CFG, MONEN)) {
            qemu_log_mask(LOG_GUEST_ERROR, "I2C monitoring not supported");
        }
        break;
    }
    case A_FLEXCOMM_I2C_INTENCLR:
    {
        REG(f, INTENSET) &= ~value;
        break;
    }
    case A_FLEXCOMM_I2C_TIMEOUT:
    {
        register_write(&ri, value, ~0, NULL, false);
        /* The bottom 4 bits are hard-wired to 0xF */
        RF_WR(f, TIMEOUT, TOMIN, 0xf);
        break;
    }
    case A_FLEXCOMM_I2C_MSTCTL:
    {
        register_write(&ri, value, ~0, NULL, false);
        if (RF_RD(f, MSTCTL, MSTSTART)) {
            uint8_t i2c_addr = RF_RD(f, MSTDAT, DATA);
            bool recv = i2c_addr & 1;

            trace_flexcomm_i2c_start(DEVICE(s)->id, i2c_addr, recv);
            if (i2c_start_transfer(s->bus, i2c_addr, recv)) {
                RF_WR(f, STAT, MSTSTATE, MSTSTATE_NAKADR);
                trace_flexcomm_i2c_nak(DEVICE(s)->id);
            } else {
                if (recv) {
                    uint8_t data = i2c_recv(s->bus);

                    RF_WR(f, MSTDAT, DATA, data);
                    trace_flexcomm_i2c_rx(DEVICE(s)->id, data);
                    RF_WR(f, STAT, MSTSTATE, MSTSTATE_RXRDY);
                } else {
                    RF_WR(f, STAT, MSTSTATE, MSTSTATE_TXRDY);
                }
            }
        }
        if (RF_RD(f, MSTCTL, MSTSTOP)) {
            RF_WR(f, STAT, MSTSTATE, MSTSTATE_IDLE);
            i2c_end_transfer(s->bus);
        }
        if (RF_RD(f, MSTCTL, MSTCONTINUE)) {
            if (RF_RD(f, STAT, MSTSTATE) == MSTSTATE_TXRDY) {
                uint8_t data = RF_RD(f, MSTDAT, DATA);

                trace_flexcomm_i2c_tx(DEVICE(s)->id, data);
                if (i2c_send(s->bus, data)) {
                    RF_WR(f, STAT, MSTSTATE, MSTSTATE_NAKDAT);
                }
            } else if (RF_RD(f, STAT, MSTSTATE) == MSTSTATE_RXRDY) {
                uint8_t data = i2c_recv(s->bus);

                RF_WR(f, MSTDAT, DATA, data);
                trace_flexcomm_i2c_rx(DEVICE(s)->id, data);
            }
        }
        break;
    }
    case A_FLEXCOMM_I2C_STAT:
    {
        /* write 1 to clear bits */
        REG(f, STAT) &= ~value;
        break;
    }
    case A_FLEXCOMM_I2C_SLVCTL:
    case A_FLEXCOMM_I2C_SLVDAT:
    case A_FLEXCOMM_I2C_SLVADR0:
    case A_FLEXCOMM_I2C_SLVADR1:
    case A_FLEXCOMM_I2C_SLVADR2:
    case A_FLEXCOMM_I2C_SLVADR3:
    case A_FLEXCOMM_I2C_SLVQUAL0:
    {
        qemu_log_mask(LOG_GUEST_ERROR, "I2C slave not supported\n");
        break;
    }
    default:
        register_write(&ri, value, ~0, NULL, false);
        break;
    }

    flexcomm_i2c_irq_update(f);

    return MEMTX_OK;
}

static void flexcomm_i2c_select(FlexcommFunction *f, bool selected)
{
    FlexcommI2cClass *ic = FLEXCOMM_I2C_GET_CLASS(f);

    if (selected) {
        flexcomm_i2c_reset(f);
    }
    ic->select(f, selected);
}

static const MemoryRegionOps flexcomm_i2c_ops = {
    .read_with_attrs = flexcomm_i2c_reg_read,
    .write_with_attrs = flexcomm_i2c_reg_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static void flexcomm_i2c_realize(DeviceState *dev, Error **errp)
{
    FlexcommI2cState *s = FLEXCOMM_I2C(dev);

    s->bus = i2c_init_bus(DEVICE(s), "bus");
}

static void flexcomm_i2c_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    FlexcommFunctionClass *fc = FLEXCOMM_FUNCTION_CLASS(klass);
    FlexcommI2cClass *ic = FLEXCOMM_I2C_CLASS(klass);

    dc->realize = flexcomm_i2c_realize;
    ic->select = fc->select;
    fc->select = flexcomm_i2c_select;
    fc->name = "i2c";
    fc->mmio_ops = &flexcomm_i2c_ops;
}

static const TypeInfo flexcomm_i2c_types[] = {
    {
        .name          = TYPE_FLEXCOMM_I2C,
        .parent        = TYPE_FLEXCOMM_FUNCTION,
        .instance_size = sizeof(FlexcommI2cState),
        .class_init    = flexcomm_i2c_class_init,
        .class_size    = sizeof(FlexcommI2cClass),
    },
};

DEFINE_TYPES(flexcomm_i2c_types);
