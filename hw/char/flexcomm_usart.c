/*
 * QEMU model for NXP's FLEXCOMM USART
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
#include "hw/qdev-properties-system.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "exec/address-spaces.h"
#include "qapi/error.h"
#include "trace.h"
#include "hw/misc/flexcomm.h"
#include "hw/char/flexcomm_usart.h"
#include "hw/arm/svd/flexcomm_usart.h"

#define REG(s, reg) (s->regs[R_FLEXCOMM_USART_##reg])
/* register field write helper macro */
#define RF_RD(s, reg, field, val) \
    ARRAY_FIELD_DP32(s->regs, FLEXCOMM_USART_##reg, field, val)
/* register field read helper macro */
#define RF_WR(s, reg, field) \
    ARRAY_FIELD_EX32(s->regs, FLEXCOMM_USART_##reg, field)

static FLEXCOMM_USART_REGISTER_ACCESS_INFO_ARRAY(reg_info);

static void flexcomm_usart_reset(FlexcommFunction *f)
{
    for (int i = 0; i < FLEXCOMM_USART_REGS_NO; i++) {
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

static void flexcomm_usart_irq_update(FlexcommFunction *f)
{
    bool irq, per_irqs, fifo_irqs, enabled = RF_WR(f, CFG, ENABLE);

    flexcomm_update_fifostat(f);
    fifo_irqs = REG(f, FIFOINTSTAT) & REG(f, FIFOINTENSET);

    REG(f, INTSTAT) = REG(f, STAT) & REG(f, INTENSET);
    per_irqs = REG(f, INTSTAT) != 0;

    irq = enabled && (fifo_irqs || per_irqs);

    trace_flexcomm_usart_irq(DEVICE(f)->id, irq, fifo_irqs, per_irqs, enabled);
    flexcomm_set_irq(f, irq);
}

static int flexcomm_usart_rx_space(void *opaque)
{
    FlexcommUsartState *s = FLEXCOMM_USART(opaque);
    FlexcommFunction *f = FLEXCOMM_FUNCTION(opaque);

    uint32_t ret = fifo32_num_free(f->rx_fifo);

    if (!RF_WR(f, CFG, ENABLE) || !RF_WR(f, FIFOCFG, ENABLERX)) {
        ret = 0;
    }

    trace_flexcomm_usart_rx_space(DEVICE(s)->id, ret);

    return ret;
}

static void flexcomm_usart_rx(void *opaque, const uint8_t *buf, int size)
{
    FlexcommFunction *f = FLEXCOMM_FUNCTION(opaque);

    if (!RF_WR(f, CFG, ENABLE) || !RF_WR(f, FIFOCFG, ENABLERX)) {
        return;
    }

    trace_flexcomm_usart_rx(DEVICE(f)->id);

    while (!fifo32_is_full(f->rx_fifo) && size) {
        fifo32_push(f->rx_fifo, *buf++);
        size--;
    }

    flexcomm_usart_irq_update(f);
}

static MemTxResult flexcomm_usart_reg_read(void *opaque, hwaddr addr,
                                           uint64_t *data, unsigned size,
                                           MemTxAttrs attrs)
{
    FlexcommFunction *f = FLEXCOMM_FUNCTION(opaque);
    FlexcommUsartState *s = FLEXCOMM_USART(opaque);
    const struct RegisterAccessInfo *rai = &reg_info[addr / 4];
    MemTxResult ret = MEMTX_OK;

    if (size != 4) {
        ret = MEMTX_ERROR;
        goto out;
    }

    switch (addr) {
    case A_FLEXCOMM_USART_FIFORD:
    {
        if (!fifo32_is_empty(f->rx_fifo)) {
            *data = fifo32_pop(f->rx_fifo);
            qemu_chr_fe_accept_input(&s->chr);
        }
        break;
    }
    case A_FLEXCOMM_USART_FIFORDNOPOP:
    {
        if (!fifo32_is_empty(f->rx_fifo)) {
            *data = fifo32_peek(f->rx_fifo);
        }
        break;
    }
    default:
        *data = f->regs[addr / 4];
        break;
    }

    flexcomm_usart_irq_update(f);

out:
    trace_flexcomm_usart_reg_read(DEVICE(f)->id, rai->name, addr, *data);
    return ret;
}

static MemTxResult flexcomm_usart_reg_write(void *opaque, hwaddr addr,
                                            uint64_t value, unsigned size,
                                            MemTxAttrs attrs)
{
    FlexcommFunction *f = FLEXCOMM_FUNCTION(opaque);
    FlexcommUsartState *s = FLEXCOMM_USART(opaque);
    const struct RegisterAccessInfo *rai = &reg_info[addr / 4];
    struct RegisterInfo ri = {
        .data = &f->regs[addr / 4],
        .data_size = 4,
        .access = rai,
    };

    trace_flexcomm_usart_reg_write(DEVICE(f)->id, rai->name, addr, value);

    switch (addr) {
    case A_FLEXCOMM_USART_INTENCLR:
    {
        register_write(&ri, value, ~0, NULL, false);
        REG(f, INTENSET) &= ~REG(f, INTENCLR);
        break;
    }
    case A_FLEXCOMM_USART_FIFOCFG:
    {
        register_write(&ri, value, ~0, NULL, false);
        flexcomm_reset_fifos(f);
        break;
    }
    case A_FLEXCOMM_USART_FIFOSTAT:
    {
        flexcomm_clear_fifostat(f, value);
        break;
    }
    case A_FLEXCOMM_USART_FIFOINTENSET:
    {
        REG(f, FIFOINTENSET) |= value;
        break;
    }
    case A_FLEXCOMM_USART_FIFOINTENCLR:
    {
        register_write(&ri, value, ~0, NULL, false);
        REG(f, FIFOINTENSET) &= ~value;
        break;
    }
    case A_FLEXCOMM_USART_FIFOWR:
    {
        register_write(&ri, value, ~0, NULL, false);

        if (!fifo32_is_full(f->tx_fifo)) {
            fifo32_push(f->tx_fifo, REG(f, FIFOWR));
        }

        if (!RF_WR(f, CFG, ENABLE) || !RF_WR(f, FIFOCFG, ENABLETX)) {
            break;
        }

        while (!fifo32_is_empty(f->tx_fifo)) {
            uint32_t val32 = fifo32_pop(f->tx_fifo);
            uint8_t val8 = val32 & 0xff;

            trace_flexcomm_usart_tx(DEVICE(f)->id);
            qemu_chr_fe_write_all(&s->chr, &val8, sizeof(val8));
        }
        break;
    }
    case A_FLEXCOMM_USART_CFG:
    {
        register_write(&ri, value, ~0, NULL, false);
        break;
    }
    default:
        register_write(&ri, value, ~0, NULL, false);
        break;
    }

    flexcomm_usart_irq_update(f);

    return MEMTX_OK;
}

static void flexcomm_usart_select(FlexcommFunction *f, bool selected)
{
    FlexcommUsartState *s = FLEXCOMM_USART(f);
    FlexcommUsartClass *uc = FLEXCOMM_USART_GET_CLASS(f);

    if (selected) {
        qemu_chr_fe_set_handlers(&s->chr, flexcomm_usart_rx_space,
                             flexcomm_usart_rx, NULL, NULL,
                             s, NULL, true);
        flexcomm_usart_reset(f);
    } else {
        qemu_chr_fe_set_handlers(&s->chr, NULL, NULL, NULL, NULL, NULL, NULL,
                                 false);
    }
    uc->select(f, selected);
}

static const MemoryRegionOps flexcomm_usart_ops = {
    .read_with_attrs = flexcomm_usart_reg_read,
    .write_with_attrs = flexcomm_usart_reg_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static Property flexcomm_usart_properties[] = {
    DEFINE_PROP_CHR("chardev", FlexcommUsartState, chr),
    DEFINE_PROP_END_OF_LIST(),
};

static void flexcomm_usart_realize(DeviceState *dev, Error **errp)
{
    qdev_prop_set_chr(dev, "chardev", qemu_chr_find(dev->id));
}

static void flexcomm_usart_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    FlexcommFunctionClass *fc = FLEXCOMM_FUNCTION_CLASS(klass);
    FlexcommUsartClass *uc = FLEXCOMM_USART_CLASS(klass);

    device_class_set_props(dc, flexcomm_usart_properties);
    dc->realize = flexcomm_usart_realize;
    uc->select = fc->select;
    fc->select = flexcomm_usart_select;
    fc->name = "usart";
    fc->has_fifos = true;
    fc->mmio_ops = &flexcomm_usart_ops;
}

static const TypeInfo flexcomm_usart_types[] = {
    {
        .name          = TYPE_FLEXCOMM_USART,
        .parent        = TYPE_FLEXCOMM_FUNCTION,
        .instance_size = sizeof(FlexcommUsartState),
        .class_init    = flexcomm_usart_class_init,
        .class_size    = sizeof(FlexcommUsartClass),
    },
};

DEFINE_TYPES(flexcomm_usart_types);
