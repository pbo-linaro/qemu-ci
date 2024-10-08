/*
 * QEMU model for NXP's FLEXCOMM SPI
 *
 * Copyright (c) 2024 Google LLC
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "qemu/cutils.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "exec/address-spaces.h"
#include "qapi/error.h"
#include "trace.h"
#include "migration/vmstate.h"
#include "hw/misc/flexcomm.h"
#include "hw/arm/svd/flexcomm_spi.h"

#define REG(s, reg) (s->regs[R_FLEXCOMM_SPI_##reg])
#define RF_WR(s, reg, field, val) \
    ARRAY_FIELD_DP32(s->regs, FLEXCOMM_SPI_##reg, field, val)
#define RF_RD(s, reg, field) \
    ARRAY_FIELD_EX32(s->regs, FLEXCOMM_SPI_##reg, field)

#define FLEXCOMM_SSEL_ASSERTED             (0)
#define FLEXCOMM_SSEL_DEASSERTED           (1)

#define FLEXCOMM_SPI_FIFOWR_LEN_MIN        (3)
#define FLEXCOMM_SPI_FIFOWR_LEN_MAX        (15)

static const FLEXCOMM_SPI_REGISTER_ACCESS_INFO_ARRAY(reg_info);

static void flexcomm_spi_reset(FlexcommFunction *f)
{
    for (int i = 0; i < FLEXCOMM_SPI_REGS_NO; i++) {
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

    RF_WR(f, FIFOSIZE, FIFOSIZE, 0x8);
}

static void flexcomm_spi_irq_update(FlexcommFunction *f)
{
    bool irq, per_irqs, fifo_irqs, enabled = RF_RD(f, CFG, ENABLE);

    flexcomm_update_fifostat(f);
    fifo_irqs = REG(f, FIFOINTSTAT) & REG(f, FIFOINTENSET);

    REG(f, INTSTAT) = REG(f, STAT) & REG(f, INTENSET);
    per_irqs = REG(f, INTSTAT) != 0;

    irq = enabled && (fifo_irqs || per_irqs);

    trace_flexcomm_spi_irq(DEVICE(f)->id, irq, fifo_irqs, per_irqs, enabled);
    flexcomm_set_irq(f, irq);
}

static void flexcomm_spi_select(FlexcommFunction *f, bool selected)
{
    FlexcommSpiState *s = FLEXCOMM_SPI(f);
    FlexcommSpiClass *sc = FLEXCOMM_SPI_GET_CLASS(f);

    if (selected) {
        bool spol[] = {
            RF_RD(f, CFG, SPOL0), RF_RD(f, CFG, SPOL1), RF_RD(f, CFG, SPOL2),
            RF_RD(f, CFG, SPOL3),
        };

        flexcomm_spi_reset(f);
        for (int i = 0; i < ARRAY_SIZE(s->cs); i++) {
            s->cs_asserted[i] = false;
            qemu_set_irq(s->cs[i], !spol[i]);
        }
    }
    sc->select(f, selected);
}

static MemTxResult flexcomm_spi_reg_read(void *opaque, hwaddr addr,
                                         uint64_t *data, unsigned size,
                                         MemTxAttrs attrs)
{
    FlexcommFunction *f = FLEXCOMM_FUNCTION(opaque);
    MemTxResult ret = MEMTX_OK;
    const struct RegisterAccessInfo *rai = &reg_info[addr / 4];

    /*
     * Allow 8/16 bits access to the FIFORD LSB half-word. This is supported by
     * hardware and required for 1/2 byte(s) width DMA transfers.
     */
    if (size != 4 && addr != A_FLEXCOMM_SPI_FIFORD) {
        ret = MEMTX_ERROR;
        goto out;
    }

    switch (addr) {
    case A_FLEXCOMM_SPI_FIFORD:
    {
        /* If we are running in loopback mode get the data from TX FIFO */
        if (RF_RD(f, CFG, LOOP) &&
            RF_RD(f, CFG, MASTER))
        {
            if (!fifo32_is_empty(f->tx_fifo)) {
                *data = fifo32_pop(f->tx_fifo);
            }
            break;
        }

        if (!fifo32_is_empty(f->rx_fifo)) {
            *data = fifo32_pop(f->rx_fifo);
        }
        break;
    }
    case A_FLEXCOMM_SPI_FIFORDNOPOP:
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

    flexcomm_spi_irq_update(f);

out:
    trace_flexcomm_spi_reg_read(DEVICE(f)->id, rai->name, addr, *data);
    return ret;
}

static uint32_t fifowr_len_bits(uint32_t val)
{
    int len = FIELD_EX32(val, FLEXCOMM_SPI_FIFOWR, LEN);

    if (len < FLEXCOMM_SPI_FIFOWR_LEN_MIN ||
        len > FLEXCOMM_SPI_FIFOWR_LEN_MAX) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid spi xfer len %d\n",
                      __func__, val);
        return 0;
    }

    return len + 1;
}

static inline uint32_t fifowr_len_bytes(uint32_t val)
{
    return fifowr_len_bits(val) > 8 ? 2 : 1;
}

static uint32_t flexcomm_spi_xfer_word(FlexcommSpiState *s, uint32_t out_data,
                                       int bytes, bool be)
{
    uint32_t word = 0;
    int out = 0;

    for (int i = 0; i < bytes; i++) {
        if (be) {
            int byte_offset = bytes - i - 1;
            out = (out_data & (0xFF << byte_offset * 8)) >> byte_offset * 8;
            word |= ssi_transfer(s->bus, out) << byte_offset * 8;
        } else {
            out = (out_data & (0xFF << i * 8)) >> i * 8;
            word |= ssi_transfer(s->bus, out) << i * 8;
        }
    }

    return word;
}

static uint32_t flexcomm_spi_get_ss_mask(FlexcommSpiState *s,
                                        uint32_t txfifo_val)
{
    FlexcommFunction *f = FLEXCOMM_FUNCTION(s);

    uint32_t slave_select_mask = 0;
    bool ss[] = {
        FIELD_EX32(txfifo_val, FLEXCOMM_SPI_FIFOWR, TXSSEL0_N),
        FIELD_EX32(txfifo_val, FLEXCOMM_SPI_FIFOWR, TXSSEL1_N),
        FIELD_EX32(txfifo_val, FLEXCOMM_SPI_FIFOWR, TXSSEL2_N),
        FIELD_EX32(txfifo_val, FLEXCOMM_SPI_FIFOWR, TXSSEL3_N),
    };
    bool spol[] = {
        RF_RD(f, CFG, SPOL0), RF_RD(f, CFG, SPOL1), RF_RD(f, CFG, SPOL2),
        RF_RD(f, CFG, SPOL3),
    };

    for (int i = 0; i < ARRAY_SIZE(s->cs); i++) {
        int irq_level = ss[i] ? spol[i] : !spol[i];

        slave_select_mask |= (ss[i] << i);
        s->cs_asserted[i] = ss[i];
        qemu_set_irq(s->cs[i], irq_level);
    }

    return slave_select_mask;
}

static MemTxResult flexcomm_spi_reg_write(void *opaque, hwaddr addr,
                                          uint64_t value, unsigned size,
                                          MemTxAttrs attrs)
{
    FlexcommFunction *f = FLEXCOMM_FUNCTION(opaque);
    FlexcommSpiState *s = FLEXCOMM_SPI(opaque);
    const struct RegisterAccessInfo *rai = &reg_info[addr / 4];
    struct RegisterInfo ri = {
        .data = &f->regs[addr / 4],
        .data_size = 4,
        .access = rai,
    };
    MemTxResult ret = MEMTX_OK;

    trace_flexcomm_spi_reg_write(DEVICE(f)->id, rai->name, addr, value);

    /*
     * Allow 8/16 bits access to both the FIFOWR MSB and LSB half-words. The
     * former is required for updating the control bits while the latter for DMA
     * transfers of 1/2 byte(s) width.
     */
    if (size != 4 && (addr / 4 != R_FLEXCOMM_SPI_FIFOWR)) {
        return MEMTX_ERROR;
    }

    switch (addr) {
    case A_FLEXCOMM_SPI_CFG:
    {
        register_write(&ri, value, ~0, NULL, false);
        break;
    }
    case A_FLEXCOMM_SPI_INTENCLR:
    {
        register_write(&ri, value, ~0, NULL, false);
        REG(f, INTENSET) &= ~REG(f, INTENCLR);
        break;
    }
    case A_FLEXCOMM_SPI_FIFOCFG:
    {
        register_write(&ri, value, ~0, NULL, false);
        flexcomm_reset_fifos(f);
        break;
    }
    case A_FLEXCOMM_SPI_FIFOSTAT:
    {
        flexcomm_clear_fifostat(f, value);
        break;
    }
    case A_FLEXCOMM_SPI_FIFOINTENSET:
    {
        REG(f, FIFOINTENSET) |= value;
        break;
    }
    case A_FLEXCOMM_SPI_FIFOINTENCLR:
    {
        register_write(&ri, value, ~0, NULL, false);
        REG(f, FIFOINTENSET) &= ~REG(f, FIFOINTENCLR);
        break;
    }
    /* update control bits but don't push into the FIFO */
    case A_FLEXCOMM_SPI_FIFOWR + 2:
    {
        if (value != 0) {
            s->tx_ctrl = value << 16;
        }
        break;
    }
    /* update control bits but don't push into the FIFO */
    case A_FLEXCOMM_SPI_FIFOWR + 3:
    {
        if (value != 0) {
            s->tx_ctrl = value << 24;
        }
        break;
    }
    case A_FLEXCOMM_SPI_FIFOWR:
    {
        /* fifo value contains both data and control bits */
        uint32_t txfifo_val;
        uint16_t tx_data = FIELD_EX32(value, FLEXCOMM_SPI_FIFOWR, TXDATA);
        uint32_t tx_ctrl = value & 0xffff0000;

        if (size > 2 && tx_ctrl != 0) {
            /* non-zero writes to control bits updates them */
            s->tx_ctrl = tx_ctrl;
        }

        /* push the data and control bits into the FIFO */
        txfifo_val = tx_data | s->tx_ctrl;

        if (!fifo32_is_full(f->tx_fifo)) {
            fifo32_push(f->tx_fifo, txfifo_val);
        }

        if (!RF_RD(f, CFG, ENABLE) || !RF_RD(f, FIFOCFG, ENABLETX)) {
            break;
        }

        /*
         * On loopback mode we just insert the values in the TX FIFO. On slave
         * mode master needs to initiate the SPI transfer.
         */
        if (RF_RD(f, CFG, LOOP) || !RF_RD(f, CFG, MASTER)) {
            break;
        }

        while (!fifo32_is_empty(f->tx_fifo)) {
            txfifo_val = fifo32_pop(f->tx_fifo);

            uint32_t ss_mask = flexcomm_spi_get_ss_mask(s, txfifo_val);
            uint32_t data = FIELD_EX32(txfifo_val, FLEXCOMM_SPI_FIFOWR, TXDATA);
            uint8_t bytes = fifowr_len_bytes(txfifo_val);
            bool msb = !RF_RD(f, CFG, LSBF);
            uint32_t val32;

            val32 = flexcomm_spi_xfer_word(s, data, bytes, msb);

            if (!fifo32_is_full(f->rx_fifo)) {
                /* Append the mask that informs which client is active */
                val32 |= (ss_mask << R_FLEXCOMM_SPI_FIFORD_RXSSEL0_N_SHIFT);
                fifo32_push(f->rx_fifo, val32);
            }

            /* If this is the end of the transfer raise the CS line */
            if (FIELD_EX32(txfifo_val, FLEXCOMM_SPI_FIFOWR, EOT)) {
                bool spol[ARRAY_SIZE(s->cs)] = {
                    RF_RD(f, CFG, SPOL0),
                    RF_RD(f, CFG, SPOL1),
                    RF_RD(f, CFG, SPOL2),
                    RF_RD(f, CFG, SPOL3),
                };

                for (int i = 0; i < ARRAY_SIZE(s->cs); i++) {
                    if (s->cs_asserted[i]) {
                        s->cs_asserted[i] = false;
                        qemu_set_irq(s->cs[i], !spol[i]);
                    }
                }
            }
        }
        break;
    }
    default:
        register_write(&ri, value, ~0, NULL, false);
        break;
    }

    flexcomm_spi_irq_update(f);

    return ret;
}

static const MemoryRegionOps flexcomm_spi_ops = {
    .read_with_attrs = flexcomm_spi_reg_read,
    .write_with_attrs = flexcomm_spi_reg_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static void flexcomm_spi_realize(DeviceState *dev, Error **error)
{
    FlexcommSpiState *s = FLEXCOMM_SPI(dev);

    s->bus = ssi_create_bus(DEVICE(s), "bus");
    qdev_init_gpio_out_named(DEVICE(s), s->cs, "cs", ARRAY_SIZE(s->cs));
}

static const VMStateDescription vmstate_flexcomm_spi = {
    .name = "flexcomm-spi",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_BOOL_ARRAY(cs_asserted, FlexcommSpiState, 4),
        VMSTATE_UINT32(tx_ctrl, FlexcommSpiState),
        VMSTATE_END_OF_LIST()
    }
};

static void flexcomm_spi_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    FlexcommFunctionClass *fc = FLEXCOMM_FUNCTION_CLASS(klass);
    FlexcommSpiClass *sc = FLEXCOMM_SPI_CLASS(klass);

    dc->realize = flexcomm_spi_realize;
    dc->vmsd = &vmstate_flexcomm_spi;
    sc->select = fc->select;
    fc->select = flexcomm_spi_select;
    fc->name = "spi";
    fc->has_fifos = true;
    fc->mmio_ops = &flexcomm_spi_ops;
}

static const TypeInfo flexcomm_spi_types[] = {
    {
        .name          = TYPE_FLEXCOMM_SPI,
        .parent        = TYPE_FLEXCOMM_FUNCTION,
        .instance_size = sizeof(FlexcommSpiState),
        .class_init    = flexcomm_spi_class_init,
        .class_size    = sizeof(FlexcommSpiClass),
    },
};

DEFINE_TYPES(flexcomm_spi_types);
