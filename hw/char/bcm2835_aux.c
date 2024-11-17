/*
 * BCM2835 (Raspberry Pi / Pi 2) Aux block (mini UART and SPI).
 * Copyright (c) 2015, Microsoft
 * Written by Andrew Baumann
 * Based on pl011.c, copyright terms below:
 *
 * Arm PrimeCell PL011 UART
 *
 * Copyright (c) 2006 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the GPL.
 *
 * At present only the core UART functions (data path for tx/rx) are
 * implemented. The following features/registers are unimplemented:
 *  - Line/modem control
 *  - Scratch register
 *  - Extra control
 *  - Baudrate
 *  - SPI interfaces
 */

#include "qemu/osdep.h"
#include "hw/char/bcm2835_aux.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"

/* TODO: These constants need to be unsigned */
#define AUX_IRQ         0x0U
#define AUX_ENABLES     0x4U
#define AUX_MU_IO_REG   0x40U
#define AUX_MU_IER_REG  0x44U
#define AUX_MU_IIR_REG  0x48U
#define AUX_MU_LCR_REG  0x4cU
#define AUX_MU_MCR_REG  0x50U
#define AUX_MU_LSR_REG  0x54U
#define AUX_MU_MSR_REG  0x58U
#define AUX_MU_SCRATCH  0x5cU
#define AUX_MU_CNTL_REG 0x60U
#define AUX_MU_STAT_REG 0x64U
#define AUX_MU_BAUD_REG 0x68U

/* Register masks */
#define MASK_AUX_MU_CNTL_REG 0x3U
/* Mask for TX-related bits */
#define MASK_AUX_MU_STAT_REG_TX 0xF00032AU
/*
 * Mask for RX-related bits.
 * XXX: It does not include receiver IDLE and receiver overrun for now.
 */
#define MASK_AUX_MU_STAT_REG_RX 0xF0001U

/* bits in IER register */
#define IER_RX_IRQ_ENABLE  0x1U
#define IER_TX_IRQ_ENABLE  0x2U

/* bits in IIR register */
#define IIR_IRQ_NOT_PEND 0x1U
#define IIR_TX_EMPTY 0x2U
#define IIR_RX_VALID 0x4U

/* bits in CNTL register */
#define CNTL_RX_ENABLE 0x1U
#define CNTL_TX_ENABLE 0x2U

/* bits in STAT register */
#define STAT_TRANSMITTER_DONE 0x200U

/* FIFOs length */
#define BCM2835_AUX_RX_FIFO_LEN 8U
#define BCM2835_AUX_TX_FIFO_LEN 8U

#define log_guest_error(fmt, ...) \
    qemu_log_mask(LOG_GUEST_ERROR, \
                  "bcm2835_aux:%s:%d: " fmt, \
                  __func__, \
                  __LINE__, \
                  ##__VA_ARGS__ \
                  )

static void bcm2835_aux_update_irq(BCM2835AuxState *s)
{
    s->iir |= IIR_IRQ_NOT_PEND;

    if (s->iir & IIR_RX_VALID) {
        s->iir &= ~IIR_IRQ_NOT_PEND;
    } else if (s->iir & IIR_TX_EMPTY) {
        s->iir &= ~IIR_IRQ_NOT_PEND;
    }

    /* An interrupt is raised whenever the IRQ_NOT_PEND bit is cleared */
    qemu_set_irq(s->irq, (s->iir & IIR_IRQ_NOT_PEND) == 0);
}

static void bcm2835_aux_rx_iir_update(BCM2835AuxState *s)
{
    /*
     * TODO: this should be a pointer to const data. However, the fifo8 API
     * requires a pointer to non-const data.
     */
    Fifo8 *rx_fifo = &s->rx_fifo;

    s->iir &= ~IIR_RX_VALID;
    if ((s->ier & IER_RX_IRQ_ENABLE) && fifo8_is_empty(rx_fifo) == false) {
        s->iir |= IIR_RX_VALID;
    }

    bcm2835_aux_update_irq(s);
}

static void bcm2835_aux_rx_stat_update(BCM2835AuxState *s)
{
    Fifo8 *rx_fifo = &s->rx_fifo;
    const bool rx_symbol_available = !fifo8_is_empty(rx_fifo);
    const uint32_t rx_fifo_level = fifo8_num_used(rx_fifo);

    s->stat &= ~MASK_AUX_MU_STAT_REG_RX;
    s->stat |= (rx_fifo_level << 16) |
               (rx_symbol_available << 0);
}

static void bcm2835_aux_rx_update(BCM2835AuxState *s)
{
    bcm2835_aux_rx_stat_update(s);
    bcm2835_aux_rx_iir_update(s);
}

static void bcm2835_aux_tx_iir_update(BCM2835AuxState *s)
{
    /*
     * TODO: this should be a pointer to const data. However, the fifo8 API
     * requires a pointer to non-const data.
     */
    Fifo8 *tx_fifo = &s->tx_fifo;

    s->iir &= ~IIR_TX_EMPTY;
    if (s->ier & IER_TX_IRQ_ENABLE && fifo8_is_empty(tx_fifo)) {
        s->iir |= IIR_TX_EMPTY | IIR_IRQ_NOT_PEND;
    }

    bcm2835_aux_update_irq(s);
}

static void bcm2835_aux_tx_stat_update(BCM2835AuxState *s, bool busy)
{
    Fifo8 *tx_fifo = &s->tx_fifo;
    const uint32_t tx_fifo_level = fifo8_num_used(tx_fifo);
    const uint32_t tx_fifo_empty = !tx_fifo_level;
    const uint32_t tx_fifo_full =
        tx_fifo_level == BCM2835_AUX_TX_FIFO_LEN ? 1 : 0;
    const uint32_t tx_done = !busy && tx_fifo_empty;
    const uint32_t tx_space_available = !tx_fifo_full;

    s->stat &= ~MASK_AUX_MU_STAT_REG_TX;
    s->stat = (tx_fifo_level << 24) |
               (tx_done << 9) |
               (tx_fifo_empty << 8) |
               (tx_fifo_full << 5) |
               (busy << 3) |
               (tx_space_available << 1);
}

static void bcm2835_aux_tx_update(BCM2835AuxState *s, bool busy)
{
    bcm2835_aux_tx_stat_update(s, busy);
    bcm2835_aux_tx_iir_update(s);
}

static bool bcm2835_aux_is_tx_enabled(const BCM2835AuxState *s)
{
    return (s->cntl & CNTL_TX_ENABLE) != 0;
}

static bool bcm2835_aux_is_rx_enabled(BCM2835AuxState *s)
{
    return (s->cntl & CNTL_RX_ENABLE) != 0;
}

static bool bcm2835_aux_put_tx_fifo(BCM2835AuxState *s, char ch)
{
    Fifo8 *tx_fifo = &s->tx_fifo;

    if (fifo8_is_full(tx_fifo)) {
        log_guest_error("TX buffer overflow");

        return false;
    }

    fifo8_push(tx_fifo, ch);

    return true;
}

static gboolean bcm2835_aux_xmit_handler(void *do_not_use, GIOCondition cond,
                                         void *opaque)
{
    BCM2835AuxState *s = opaque;
    Fifo8 *tx_fifo = &s->tx_fifo;

    if (!fifo8_is_empty(tx_fifo)) {
        const uint8_t ch = fifo8_pop(&s->tx_fifo);
        qemu_chr_fe_write(&s->chr, &ch, 1);
        bcm2835_aux_tx_update(s, true);

        return G_SOURCE_CONTINUE;
    } else {
        bcm2835_aux_tx_update(s, false);

        return G_SOURCE_REMOVE;
    }
}

static bool bcm2835_aux_is_tx_busy(const BCM2835AuxState *s)
{
    return !(s->stat & STAT_TRANSMITTER_DONE);
}

static bool bcm2835_aux_can_send(const BCM2835AuxState *s)
{
    return bcm2835_aux_is_tx_enabled(s) && !bcm2835_aux_is_tx_busy(s);
}

static void bcm2835_aux_try_send(BCM2835AuxState *s)
{
    bool busy = false;

    if (bcm2835_aux_can_send(s)) {
        const uint8_t ch = fifo8_pop(&s->tx_fifo);
        qemu_chr_fe_write(&s->chr, &ch, 1);
        qemu_chr_fe_add_watch(&s->chr, G_IO_OUT | G_IO_HUP,
                              bcm2835_aux_xmit_handler, s);
        busy = true;
    }

    bcm2835_aux_tx_update(s, busy);
}

static void bcm2835_aux_try_transmit(BCM2835AuxState *s, uint8_t ch)
{
    const bool result = bcm2835_aux_put_tx_fifo(s, ch);

    if (result) {
        bcm2835_aux_try_send(s);
    }
}

static uint64_t bcm2835_aux_read(void *opaque, hwaddr offset, unsigned size)
{
    BCM2835AuxState *s = opaque;
    Fifo8 *rx_fifo = &s->rx_fifo;
    const bool is_rx_fifo_not_empty = !fifo8_is_empty(rx_fifo);
    /*
     * 0xFF trashes terminal output, so device driver bugs can be found quickly
     * in case the RX_FIFO is read while empty
     */
    uint32_t c = 0xFF, res;

    switch (offset) {
    case AUX_IRQ:
        return s->iir != 0;

    case AUX_ENABLES:
        return 1; /* mini UART permanently enabled */

    case AUX_MU_IO_REG:
        /* "DLAB bit set means access baudrate register" is NYI */
        if (is_rx_fifo_not_empty) {
            c = fifo8_pop(rx_fifo);
        }
        qemu_chr_fe_accept_input(&s->chr);
        bcm2835_aux_rx_update(s);
        return c;

    case AUX_MU_IER_REG:
        /* "DLAB bit set means access baudrate register" is NYI */
        return 0xc0 | s->ier; /* FIFO enables always read 1 */

    case AUX_MU_IIR_REG:
        return s->iir;

    case AUX_MU_LCR_REG:
        qemu_log_mask(LOG_UNIMP, "%s: AUX_MU_LCR_REG unsupported\n", __func__);
        return 0;

    case AUX_MU_MCR_REG:
        qemu_log_mask(LOG_UNIMP, "%s: AUX_MU_MCR_REG unsupported\n", __func__);
        return 0;

    case AUX_MU_LSR_REG:
        res = 0x60; /* tx idle, empty */
        if (is_rx_fifo_not_empty) {
            res |= 0x1;
        }
        return res;

    case AUX_MU_MSR_REG:
        qemu_log_mask(LOG_UNIMP, "%s: AUX_MU_MSR_REG unsupported\n", __func__);
        return 0;

    case AUX_MU_SCRATCH:
        qemu_log_mask(LOG_UNIMP, "%s: AUX_MU_SCRATCH unsupported\n", __func__);
        return 0;

    case AUX_MU_CNTL_REG:
        return s->cntl;

    case AUX_MU_STAT_REG:
        return s->stat;

    case AUX_MU_BAUD_REG:
        qemu_log_mask(LOG_UNIMP, "%s: AUX_MU_BAUD_REG unsupported\n", __func__);
        return 0;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset %"HWADDR_PRIx"\n",
                      __func__, offset);
        return 0;
    }
}

static void bcm2835_aux_write(void *opaque, hwaddr offset, uint64_t value,
                              unsigned size)
{
    BCM2835AuxState *s = opaque;
    Fifo8 *rx_fifo = &s->rx_fifo;
    unsigned char ch;

    switch (offset) {
    case AUX_ENABLES:
        if (value != 1) {
            qemu_log_mask(LOG_UNIMP, "%s: unsupported attempt to enable SPI"
                                     " or disable UART: 0x%"PRIx64"\n",
                          __func__, value);
        }
        break;

    case AUX_MU_IO_REG:
        /* "DLAB bit set means access baudrate register" is NYI */
        ch = value;
        bcm2835_aux_try_transmit(s, ch);
        break;

    case AUX_MU_IER_REG:
        /* "DLAB bit set means access baudrate register" is NYI */
        s->ier = value & (IER_TX_IRQ_ENABLE | IER_RX_IRQ_ENABLE);
        bcm2835_aux_rx_update(s);
        bcm2835_aux_tx_update(s, false);
        break;

    case AUX_MU_IIR_REG:
        if (value & 0x2) {
            fifo8_reset(rx_fifo);
        }
        break;

    case AUX_MU_LCR_REG:
        qemu_log_mask(LOG_UNIMP, "%s: AUX_MU_LCR_REG unsupported\n", __func__);
        break;

    case AUX_MU_MCR_REG:
        qemu_log_mask(LOG_UNIMP, "%s: AUX_MU_MCR_REG unsupported\n", __func__);
        break;

    case AUX_MU_SCRATCH:
        qemu_log_mask(LOG_UNIMP, "%s: AUX_MU_SCRATCH unsupported\n", __func__);
        break;

    case AUX_MU_CNTL_REG:
        if (value & ~MASK_AUX_MU_CNTL_REG) {
            qemu_log_mask(LOG_UNIMP,
                          "%s: auto flow control not supported\n",
                          __func__);
        }
        s->cntl = value & MASK_AUX_MU_CNTL_REG;
        break;

    case AUX_MU_BAUD_REG:
        qemu_log_mask(LOG_UNIMP, "%s: AUX_MU_BAUD_REG unsupported\n", __func__);
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset %"HWADDR_PRIx"\n",
                      __func__, offset);
    }
}

static int bcm2835_aux_can_receive(void *opaque)
{
    BCM2835AuxState *s = opaque;

    return !fifo8_is_full(&s->rx_fifo);
}

static void bcm2835_aux_put_fifo(BCM2835AuxState *s, uint8_t value)
{
    Fifo8 *rx_fifo = &s->rx_fifo;

    if (fifo8_is_full(rx_fifo) == false) {
        fifo8_push(rx_fifo, value);
        bcm2835_aux_rx_update(s);
    }
}

static void bcm2835_aux_receive(void *opaque, const uint8_t *buf, int size)
{
    BCM2835AuxState *s = opaque;

    if (bcm2835_aux_is_rx_enabled(s)) {
        bcm2835_aux_put_fifo(opaque, *buf);
    }
}

static const MemoryRegionOps bcm2835_aux_ops = {
    .read = bcm2835_aux_read,
    .write = bcm2835_aux_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
};

static const VMStateDescription vmstate_bcm2835_aux = {
    .name = TYPE_BCM2835_AUX,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_FIFO8(rx_fifo, BCM2835AuxState),
        VMSTATE_FIFO8(tx_fifo, BCM2835AuxState),
        VMSTATE_UINT32(ier, BCM2835AuxState),
        VMSTATE_UINT32(iir, BCM2835AuxState),
        VMSTATE_UINT32(cntl, BCM2835AuxState),
        VMSTATE_UINT32(stat, BCM2835AuxState),
        VMSTATE_END_OF_LIST()
    }
};

static void bcm2835_aux_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    BCM2835AuxState *s = BCM2835_AUX(obj);

    memory_region_init_io(&s->iomem, OBJECT(s), &bcm2835_aux_ops, s,
                          TYPE_BCM2835_AUX, 0x100);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
}

static void bcm2835_aux_realize(DeviceState *dev, Error **errp)
{
    BCM2835AuxState *s = BCM2835_AUX(dev);

    fifo8_create(&s->rx_fifo, BCM2835_AUX_RX_FIFO_LEN);
    fifo8_create(&s->tx_fifo, BCM2835_AUX_TX_FIFO_LEN);
    s->ier = 0x0;
    /* FIFOs enabled and interrupt pending */
    s->iir = 0xC1;
    /* Both transmitter and receiver are initially enabled */
    s->cntl = 0x3;
    /* Transmitter done and FIFO empty */
    s->stat = 0x300;

    qemu_chr_fe_set_handlers(&s->chr, bcm2835_aux_can_receive,
                             bcm2835_aux_receive, NULL, NULL, s, NULL, true);
}

static Property bcm2835_aux_props[] = {
    DEFINE_PROP_CHR("chardev", BCM2835AuxState, chr),
    DEFINE_PROP_END_OF_LIST(),
};

static void bcm2835_aux_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = bcm2835_aux_realize;
    dc->vmsd = &vmstate_bcm2835_aux;
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
    device_class_set_props(dc, bcm2835_aux_props);
}

static const TypeInfo bcm2835_aux_info = {
    .name          = TYPE_BCM2835_AUX,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(BCM2835AuxState),
    .instance_init = bcm2835_aux_init,
    .class_init    = bcm2835_aux_class_init,
};

static void bcm2835_aux_register_types(void)
{
    type_register_static(&bcm2835_aux_info);
}

type_init(bcm2835_aux_register_types)
