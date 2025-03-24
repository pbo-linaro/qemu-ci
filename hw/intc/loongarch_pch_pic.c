/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU Loongson 7A1000 I/O interrupt controller.
 *
 * Copyright (C) 2021 Loongson Technology Corporation Limited
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "hw/irq.h"
#include "hw/intc/loongarch_pch_pic.h"
#include "trace.h"
#include "qapi/error.h"

static void pch_pic_update_irq(LoongArchPICCommonState *s, uint64_t mask,
                               int level)
{
    uint64_t val;
    int irq;

    if (level) {
        val = mask & s->intirr & ~s->int_mask;
        if (val) {
            irq = ctz64(val);
            s->intisr |= MAKE_64BIT_MASK(irq, 1);
            qemu_set_irq(s->parent_irq[s->htmsi_vector[irq]], 1);
        }
    } else {
        /*
         * intirr means requested pending irq
         * do not clear pending irq for edge-triggered on lowering edge
         */
        val = mask & s->intisr & ~s->intirr;
        if (val) {
            irq = ctz64(val);
            s->intisr &= ~MAKE_64BIT_MASK(irq, 1);
            qemu_set_irq(s->parent_irq[s->htmsi_vector[irq]], 0);
        }
    }
}

static void pch_pic_irq_handler(void *opaque, int irq, int level)
{
    LoongArchPICCommonState *s = LOONGARCH_PIC_COMMON(opaque);
    uint64_t mask = 1ULL << irq;

    assert(irq < s->irq_num);
    trace_loongarch_pch_pic_irq_handler(irq, level);

    if (s->intedge & mask) {
        /* Edge triggered */
        if (level) {
            if ((s->last_intirr & mask) == 0) {
                /* marked pending on a rising edge */
                s->intirr |= mask;
            }
            s->last_intirr |= mask;
        } else {
            s->last_intirr &= ~mask;
        }
    } else {
        /* Level triggered */
        if (level) {
            s->intirr |= mask;
            s->last_intirr |= mask;
        } else {
            s->intirr &= ~mask;
            s->last_intirr &= ~mask;
        }
    }
    pch_pic_update_irq(s, mask, level);
}

static uint64_t pch_pic_read(void *opaque, hwaddr addr, uint64_t field_mask)
{
    LoongArchPICCommonState *s = LOONGARCH_PIC_COMMON(opaque);
    uint64_t val = 0;
    uint32_t offset = addr & 7, tmp;

    switch (addr) {
    case PCH_PIC_INT_ID ... PCH_PIC_INT_ID + 7:
        val = s->id.data;
        break;
    case PCH_PIC_INT_MASK ... PCH_PIC_INT_MASK + 7:
        val = s->int_mask;
        break;
    case PCH_PIC_INT_EDGE ... PCH_PIC_INT_EDGE + 7:
        val = s->intedge;
        break;
    case PCH_PIC_HTMSI_EN ... PCH_PIC_HTMSI_EN + 7:
        val = s->htmsi_en;
        break;
    case PCH_PIC_AUTO_CTRL0 ... PCH_PIC_AUTO_CTRL0 + 7:
    case PCH_PIC_AUTO_CTRL1 ... PCH_PIC_AUTO_CTRL1 + 7:
        /* PCH PIC connect to EXTIOI always, discard auto_ctrl access */
        break;
    case PCH_PIC_INT_STATUS ... PCH_PIC_INT_STATUS + 7:
        val = s->intisr & (~s->int_mask);
        break;
    case PCH_PIC_INT_POL ... PCH_PIC_INT_POL + 7:
        val = s->int_polarity;
        break;
    case PCH_PIC_HTMSI_VEC ... PCH_PIC_HTMSI_VEC_END:
        tmp = addr - offset - PCH_PIC_HTMSI_VEC;
        val = *(uint64_t *)(s->htmsi_vector + tmp);
        break;
    case PCH_PIC_ROUTE_ENTRY ... PCH_PIC_ROUTE_ENTRY_END:
        tmp = addr - offset - PCH_PIC_ROUTE_ENTRY;
        val = *(uint64_t *)(s->route_entry + tmp);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "pch_pic_read: Bad address 0x%"PRIx64"\n", addr);
        break;
    }

    return (val >> (offset * 8)) & field_mask;
}

static void pch_pic_write(void *opaque, hwaddr addr, uint64_t value,
                          uint64_t field_mask)
{
    LoongArchPICCommonState *s = LOONGARCH_PIC_COMMON(opaque);
    uint32_t offset;
    uint64_t old, mask, data, *ptemp;

    offset = addr & 7;
    addr -= offset;
    mask = field_mask << (offset * 8);
    data = (value & field_mask) << (offset * 8);
    switch (addr) {
    case PCH_PIC_INT_MASK ... PCH_PIC_INT_MASK + 7:
        old = s->int_mask;
        s->int_mask = (old & ~mask) | data;
        if (old & ~data) {
            pch_pic_update_irq(s, old & ~data, 1);
        }

        if (~old & data) {
            pch_pic_update_irq(s, ~old & data, 0);
        }
        break;
    case PCH_PIC_INT_EDGE ... PCH_PIC_INT_EDGE + 7:
        s->intedge = (s->intedge & ~mask) | data;
        break;
    case PCH_PIC_INT_CLEAR ... PCH_PIC_INT_CLEAR + 7:
        if (s->intedge & data) {
            s->intirr &= ~data;
            pch_pic_update_irq(s, data, 0);
            s->intisr &= ~data;
        }
        break;
    case PCH_PIC_HTMSI_EN ... PCH_PIC_HTMSI_EN + 7:
        s->htmsi_en = (s->htmsi_en & ~mask) | data;
        break;
    case PCH_PIC_AUTO_CTRL0 ... PCH_PIC_AUTO_CTRL0 + 7:
    case PCH_PIC_AUTO_CTRL1 ... PCH_PIC_AUTO_CTRL1 + 7:
        /* Discard auto_ctrl access */
        break;
    case PCH_PIC_INT_POL ... PCH_PIC_INT_POL + 7:
        s->int_polarity = (s->int_polarity & ~mask) | data;
        break;
    case PCH_PIC_HTMSI_VEC ... PCH_PIC_HTMSI_VEC_END:
        ptemp = (uint64_t *)(s->htmsi_vector + addr - PCH_PIC_HTMSI_VEC);
        *ptemp = (*ptemp & ~mask) | data;
        break;
    case PCH_PIC_ROUTE_ENTRY ... PCH_PIC_ROUTE_ENTRY_END:
        ptemp = (uint64_t *)(s->route_entry + addr - PCH_PIC_ROUTE_ENTRY);
        *ptemp = (*ptemp & ~mask) | data;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "pch_pic_write: Bad address 0x%"PRIx64"\n", addr);
        break;
    }
}

static uint64_t loongarch_pch_pic_read(void *opaque, hwaddr addr,
                                       unsigned size)
{
    uint64_t val = 0;

    switch (size) {
    case 1:
        val = pch_pic_read(opaque, addr, 0xFF);
        break;
    case 2:
        val = pch_pic_read(opaque, addr, 0xFFFF);
        break;
    case 4:
        val = pch_pic_read(opaque, addr, UINT_MAX);
        break;
    case 8:
        val = pch_pic_read(opaque, addr, UINT64_MAX);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "loongarch_pch_pic_read: Bad size %d\n", size);
        break;
    }

    trace_loongarch_pch_pic_read(size, addr, val);
    return val;
}

static void loongarch_pch_pic_write(void *opaque, hwaddr addr,
                                    uint64_t value, unsigned size)
{
    trace_loongarch_pch_pic_write(size, addr, value);

    switch (size) {
    case 1:
        pch_pic_write(opaque, addr, value, 0xFF);
        break;
    case 2:
        pch_pic_write(opaque, addr, value, 0xFFFF);
        break;
        break;
    case 4:
        pch_pic_write(opaque, addr, value, UINT_MAX);
        break;
    case 8:
        pch_pic_write(opaque, addr, value, UINT64_MAX);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "loongarch_pch_pic_write: Bad size %d\n", size);
        break;
    }
}

static uint64_t loongarch_pch_pic_high_readw(void *opaque, hwaddr addr,
                                        unsigned size)
{
    addr += PCH_PIC_INT_STATUS;
    return loongarch_pch_pic_read(opaque, addr, size);
}

static void loongarch_pch_pic_high_writew(void *opaque, hwaddr addr,
                                     uint64_t value, unsigned size)
{
    addr += PCH_PIC_INT_STATUS;
    loongarch_pch_pic_write(opaque, addr, value, size);
}

static uint64_t loongarch_pch_pic_readb(void *opaque, hwaddr addr,
                                        unsigned size)
{
    addr += PCH_PIC_ROUTE_ENTRY;
    return loongarch_pch_pic_read(opaque, addr, size);
}

static void loongarch_pch_pic_writeb(void *opaque, hwaddr addr,
                                     uint64_t data, unsigned size)
{
    addr += PCH_PIC_ROUTE_ENTRY;
    loongarch_pch_pic_write(opaque, addr, data, size);
}

static const MemoryRegionOps loongarch_pch_pic_ops = {
    .read = loongarch_pch_pic_read,
    .write = loongarch_pch_pic_write,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 8,
        /*
         * PCH PIC device would not work correctly if the guest was doing
         * unaligned access. This might not be a limitation on the real
         * device but in practice there is no reason for a guest to access
         * this device unaligned.
         */
        .unaligned = false,
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 8,
    },
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static const MemoryRegionOps loongarch_pch_pic_reg32_high_ops = {
    .read = loongarch_pch_pic_high_readw,
    .write = loongarch_pch_pic_high_writew,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static const MemoryRegionOps loongarch_pch_pic_reg8_ops = {
    .read = loongarch_pch_pic_readb,
    .write = loongarch_pch_pic_writeb,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void loongarch_pch_pic_reset(DeviceState *d)
{
    LoongArchPICCommonState *s = LOONGARCH_PIC_COMMON(d);
    int i;

    /*
     * With 7A1000 manual
     *   bit  0-15 pch irqchip version
     *   bit 16-31 irq number supported with pch irqchip
     */
    s->id.desc.id = PCH_PIC_INT_ID_VAL;
    s->id.desc.version = PCH_PIC_INT_ID_VER;
    s->id.desc.irq_num = s->irq_num - 1;
    s->int_mask = -1;
    s->htmsi_en = 0x0;
    s->intedge  = 0x0;
    s->intclr   = 0x0;
    s->auto_crtl0 = 0x0;
    s->auto_crtl1 = 0x0;
    for (i = 0; i < 64; i++) {
        s->route_entry[i] = 0x1;
        s->htmsi_vector[i] = 0x0;
    }
    s->intirr = 0x0;
    s->intisr = 0x0;
    s->last_intirr = 0x0;
    s->int_polarity = 0x0;
}

static void loongarch_pic_realize(DeviceState *dev, Error **errp)
{
    LoongArchPICCommonState *s = LOONGARCH_PIC_COMMON(dev);
    LoongarchPICClass *lpc = LOONGARCH_PIC_GET_CLASS(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    Error *local_err = NULL;

    lpc->parent_realize(dev, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    qdev_init_gpio_out(dev, s->parent_irq, s->irq_num);
    qdev_init_gpio_in(dev, pch_pic_irq_handler, s->irq_num);
    memory_region_init_io(&s->iomem, OBJECT(dev),
                          &loongarch_pch_pic_ops,
                          s, TYPE_LOONGARCH_PIC, 0x100);
    memory_region_init_io(&s->iomem8, OBJECT(dev), &loongarch_pch_pic_reg8_ops,
                          s, PCH_PIC_NAME(.reg8), 0x2a0);
    memory_region_init_io(&s->iomem32_high, OBJECT(dev),
                          &loongarch_pch_pic_reg32_high_ops,
                          s, PCH_PIC_NAME(.reg32_part2), 0xc60);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_mmio(sbd, &s->iomem8);
    sysbus_init_mmio(sbd, &s->iomem32_high);

}

static void loongarch_pic_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    LoongarchPICClass *lpc = LOONGARCH_PIC_CLASS(klass);

    device_class_set_legacy_reset(dc, loongarch_pch_pic_reset);
    device_class_set_parent_realize(dc, loongarch_pic_realize,
                                    &lpc->parent_realize);
}

static const TypeInfo loongarch_pic_types[] = {
   {
        .name               = TYPE_LOONGARCH_PIC,
        .parent             = TYPE_LOONGARCH_PIC_COMMON,
        .instance_size      = sizeof(LoongarchPICState),
        .class_size         = sizeof(LoongarchPICClass),
        .class_init         = loongarch_pic_class_init,
    }
};

DEFINE_TYPES(loongarch_pic_types)
