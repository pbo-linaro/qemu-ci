/*
 * QEMU model for NXP's FLEXCOMM
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
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "migration/vmstate.h"
#include "exec/address-spaces.h"
#include "qapi/error.h"
#include "trace.h"
#include "migration/vmstate.h"
#include "hw/misc/flexcomm.h"
#include "hw/arm/svd/flexcomm_usart.h"

#define REG(s, reg) (s->regs[R_FLEXCOMM_##reg])
#define RF_WR(s, reg, field, val) \
    ARRAY_FIELD_DP32(s->regs, FLEXCOMM_##reg, field, val)
#define RF_RD(s, reg, field) \
    ARRAY_FIELD_EX32(s->regs, FLEXCOMM_##reg, field)

#define modname "FLEXCOMM"

#define FLEXCOMM_FUNC_MMIO_SIZE \
    ((FLEXCOMM_REGS_NO - 2) * sizeof(uint32_t))

static const FLEXCOMM_REGISTER_ACCESS_INFO_ARRAY(reg_info);

static inline bool has_function(FlexcommState *s, int function)
{
    return s->functions & (1 << function);
}

static inline int persel_to_function(FlexcommState *s)
{
    switch (RF_RD(s, PSELID, PERSEL)) {
    case FLEXCOMM_PERSEL_USART:
        return FLEXCOMM_FUNC_USART;
    case FLEXCOMM_PERSEL_SPI:
        return FLEXCOMM_FUNC_SPI;
    case FLEXCOMM_PERSEL_I2C:
        return FLEXCOMM_FUNC_I2C;
    case FLEXCOMM_PERSEL_I2S_TX:
    case FLEXCOMM_PERSEL_I2S_RX:
        return FLEXCOMM_FUNC_I2S;
    default:
        return -1;
    }
}

static void flexcomm_func_select(FlexcommState *s, bool selected)
{
    FlexcommFunction *obj[] = {
        FLEXCOMM_FUNCTION(&s->usart),
        FLEXCOMM_FUNCTION(&s->spi),
        FLEXCOMM_FUNCTION(&s->i2c),
    };
    int f = persel_to_function(s);

    if (f >= 0 && f < ARRAY_SIZE(obj)) {
        flexcomm_select(obj[f], selected);
    }
}

static void flexcomm_reset_enter(Object *o, ResetType type)
{
    FlexcommState *s = FLEXCOMM(o);

    trace_flexcomm_reset();

    flexcomm_func_select(s, false);

    for (int i = 0; i < FLEXCOMM_REGS_NO; i++) {
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

    RF_WR(s, PSELID, USARTPRESENT, has_function(s, FLEXCOMM_FUNC_USART));
    RF_WR(s, PSELID, SPIPRESENT, has_function(s, FLEXCOMM_FUNC_SPI));
    RF_WR(s, PSELID, I2CPRESENT, has_function(s, FLEXCOMM_FUNC_I2C));
    RF_WR(s, PSELID, I2SPRESENT, has_function(s, FLEXCOMM_FUNC_I2S));

    s->irq_state = false;
}

static void flexcomm_reset_exit(Object *o, ResetType type)
{
    FlexcommState *s = FLEXCOMM(o);

    qemu_set_irq(s->irq, s->irq_state);
}


static MemTxResult flexcomm_reg_read(void *opaque, hwaddr addr,
                                    uint64_t *data, unsigned size,
                                    MemTxAttrs attrs)
{
    FlexcommState *s = opaque;
    MemTxResult ret = MEMTX_OK;
    const struct RegisterAccessInfo *rai = &reg_info[addr / 4];

    switch (addr) {
    case A_FLEXCOMM_PSELID:
    case A_FLEXCOMM_PID:
    {
        *data = s->regs[addr / 4];
        break;
    }
    default:
        return MEMTX_ERROR;
    }

    trace_flexcomm_reg_read(DEVICE(s)->id, rai->name, addr, *data);
    return ret;
}

static int flexcomm_check_function(FlexcommState *s)
{
    int f = persel_to_function(s);

    if (f < 0 || !has_function(s, f)) {
        return -1;
    }

    return f;
}

static MemTxResult flexcomm_reg_write(void *opaque, hwaddr addr,
                                     uint64_t value, unsigned size,
                                     MemTxAttrs attrs)
{
    FlexcommState *s = opaque;
    MemTxResult ret = MEMTX_OK;
    const struct RegisterAccessInfo *rai = &reg_info[addr / 4];
    struct RegisterInfo ri = {
        .data = &s->regs[addr / 4],
        .data_size = 4,
        .access = rai,
    };

    trace_flexcomm_reg_write(DEVICE(s)->id, rai->name, addr, value);

    switch (addr) {
    case A_FLEXCOMM_PID:
        /* RO register, nothing do to */
        break;
    case A_FLEXCOMM_PSELID:
    {
        if (RF_RD(s, PSELID, LOCK)) {
            break;
        }

        flexcomm_func_select(s, false);

        register_write(&ri, value, ~0, modname, false);

        if (flexcomm_check_function(s) < 0) {
            RF_WR(s, PSELID, PERSEL, 0);
            break;
        }

        flexcomm_func_select(s, true);
        break;
    }
    default:
        return MEMTX_ERROR;
    }

    return ret;
}


static const MemoryRegionOps flexcomm_ops = {
    .read_with_attrs = flexcomm_reg_read,
    .write_with_attrs = flexcomm_reg_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static Property flexcomm_properties[] = {
    DEFINE_PROP_UINT32("functions", FlexcommState, functions,
                       FLEXCOMM_FULL),
    DEFINE_PROP_END_OF_LIST(),
};

static void flexcomm_init(Object *obj)
{
    FlexcommState *s = FLEXCOMM(obj);
    DeviceState *dev = DEVICE(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    memory_region_init(&s->container, obj, "container", sizeof(s->regs));
    memory_region_init_io(&s->mmio, obj, &flexcomm_ops, s,
                          TYPE_FLEXCOMM, sizeof(s->regs));
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->container);
    sysbus_init_irq(sbd, &s->irq);
}

static void flexcomm_finalize(Object *obj)
{
    FlexcommState *s = FLEXCOMM(obj);

    /* release resources allocated by the function select (e.g. fifos) */
    flexcomm_func_select(s, false);
}

static void flexcomm_func_realize_and_unref(FlexcommFunction *f, Error **errp)
{
    FlexcommState *s = FLEXCOMM(OBJECT(f)->parent);
    FlexcommFunctionClass *fc = FLEXCOMM_FUNCTION_GET_CLASS(f);

    f->regs = s->regs;
    f->tx_fifo = &s->tx_fifo;
    f->rx_fifo = &s->rx_fifo;
    memory_region_add_subregion_overlap(&s->container, 0, &f->mmio, 0);
    DEVICE(f)->id = g_strdup_printf("%s-%s", DEVICE(s)->id, fc->name);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(f), errp);
    memory_region_set_enabled(&f->mmio, false);
}

static void flexcomm_realize(DeviceState *dev, Error **errp)
{
    FlexcommState *s = FLEXCOMM(dev);

    memory_region_add_subregion_overlap(&s->container, 0, &s->mmio, -1);
}

static const VMStateDescription vmstate_flexcomm = {
    .name = "flexcomm",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, FlexcommState, FLEXCOMM_REGS_NO),
        VMSTATE_BOOL(irq_state, FlexcommState),
        VMSTATE_FIFO32(rx_fifo, FlexcommState),
        VMSTATE_FIFO32(tx_fifo, FlexcommState),
        VMSTATE_END_OF_LIST()
    }
};

static void flexcomm_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.enter = flexcomm_reset_enter;
    rc->phases.exit = flexcomm_reset_exit;
    device_class_set_props(dc, flexcomm_properties);
    dc->realize = flexcomm_realize;
    dc->vmsd = &vmstate_flexcomm;
}

void flexcomm_set_irq(FlexcommFunction *f, bool irq)
{
    FlexcommState *s = FLEXCOMM(OBJECT(f)->parent);

    if (s->irq_state != irq) {
        trace_flexcomm_irq(DEVICE(s)->id, irq);
        qemu_set_irq(s->irq, irq);
        s->irq_state = irq;
    }
}

/* FIFO is shared between USART and SPI, provide common functions here */
#define FIFO_REG(s, reg) (s->regs[R_FLEXCOMM_USART_FIFO##reg])
#define FIFO_WR(s, reg, field, val) \
    ARRAY_FIELD_DP32(s->regs, FLEXCOMM_USART_FIFO##reg, field, val)
#define FIFO_RD(s, reg, field) \
    ARRAY_FIELD_EX32(s->regs, FLEXCOMM_USART_FIFO##reg, field)

void flexcomm_update_fifostat(FlexcommFunction *f)
{
    int rxlvl = fifo32_num_used(f->rx_fifo);
    int txlvl = fifo32_num_used(f->tx_fifo);

    FIFO_WR(f, STAT, RXLVL, rxlvl);
    FIFO_WR(f, STAT, TXLVL, txlvl);
    FIFO_WR(f, STAT, RXFULL, fifo32_is_full(f->rx_fifo) ? 1 : 0);
    FIFO_WR(f, STAT, RXNOTEMPTY, !fifo32_is_empty(f->rx_fifo) ? 1 : 0);
    FIFO_WR(f, STAT, TXNOTFULL, !fifo32_is_full(f->tx_fifo) ? 1 : 0);
    FIFO_WR(f, STAT, TXEMPTY, fifo32_is_empty(f->tx_fifo) ? 1 : 0);

    if (FIFO_RD(f, TRIG, RXLVLENA) && (rxlvl > FIFO_RD(f, TRIG, RXLVL))) {
        FIFO_WR(f, INTSTAT, RXLVL, 1);
    } else {
        FIFO_WR(f, INTSTAT, RXLVL, 0);
    }

    if (FIFO_RD(f, TRIG, TXLVLENA) && (txlvl <= FIFO_RD(f, TRIG, TXLVL))) {
        FIFO_WR(f, INTSTAT, TXLVL, 1);
    } else {
        FIFO_WR(f, INTSTAT, TXLVL, 0);
    }

    trace_flexcomm_fifostat(DEVICE(f)->id, FIFO_REG(f, STAT),
                            FIFO_REG(f, INTSTAT));
}

void flexcomm_reset_fifos(FlexcommFunction *f)
{
    if (FIFO_RD(f, CFG, EMPTYRX)) {
        FIFO_WR(f, CFG, EMPTYRX, 0);
        fifo32_reset(f->rx_fifo);
    }
    if (FIFO_RD(f, CFG, EMPTYTX)) {
        FIFO_WR(f, CFG, EMPTYTX, 0);
        fifo32_reset(f->tx_fifo);
    }
}

void flexcomm_clear_fifostat(FlexcommFunction *f, uint64_t value)
{
    bool rxerr = FIELD_EX32(value, FLEXCOMM_USART_FIFOSTAT, RXERR);
    bool txerr = FIELD_EX32(value, FLEXCOMM_USART_FIFOSTAT, TXERR);

    if (rxerr) {
        FIFO_WR(f, STAT, RXERR, 0);
    }
    if (txerr) {
        FIFO_WR(f, STAT, TXERR, 0);
    }
}

static void flexcomm_function_select(FlexcommFunction *f, bool selected)
{
    FlexcommFunctionClass *fc = FLEXCOMM_FUNCTION_GET_CLASS(f);

    memory_region_set_enabled(&f->mmio, selected);
    if (fc->has_fifos) {
        if (selected) {
            unsigned num = FIFO_RD(f, SIZE, FIFOSIZE);

            fifo32_create(f->tx_fifo, num);
            fifo32_create(f->rx_fifo, num);
        } else {
            fifo32_destroy(f->tx_fifo);
            fifo32_destroy(f->rx_fifo);
        }
    }
}

static void flexcomm_function_init(Object *obj)
{
    FlexcommFunctionClass *fc = FLEXCOMM_FUNCTION_GET_CLASS(obj);
    FlexcommFunction *f = FLEXCOMM_FUNCTION(obj);

    memory_region_init_io(&f->mmio, obj, fc->mmio_ops, obj, fc->name,
                          FLEXCOMM_FUNC_MMIO_SIZE);
}

static void flexcomm_function_class_init(ObjectClass *klass, void *data)
{
    FlexcommFunctionClass *fc = FLEXCOMM_FUNCTION_CLASS(klass);

    fc->select = flexcomm_function_select;
}

static const TypeInfo flexcomm_types[] = {
    {
        .name          = TYPE_FLEXCOMM,
        .parent        = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(FlexcommState),
        .instance_init = flexcomm_init,
        .instance_finalize = flexcomm_finalize,
        .class_init    = flexcomm_class_init,
    },
    {
        .name          = TYPE_FLEXCOMM_FUNCTION,
        .parent        = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(FlexcommFunction),
        .abstract      = true,
        .class_size    = sizeof(FlexcommFunctionClass),
        .instance_init = flexcomm_function_init,
        .class_init    = flexcomm_function_class_init,
    },
};

DEFINE_TYPES(flexcomm_types);
