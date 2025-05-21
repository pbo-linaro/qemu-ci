/*
 * QEMU LowRISC ethernet emulation
 *
 * Ben Dooks <ben.dooks@codethink.co.uk>
 * Copyright (c) 2025 Codethink Ltd,
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/irq.h"
#include "hw/registerfields.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "net/eth.h"

#include "hw/net/lowrisc_eth.h"
#include "trace.h"

/* address space is roughtly
 * 0x0000..0x07ff - nothing here
 * 0x8000..0x0880 - control and status registers
 * 0x1000..0x4000 - transmission buffer(s) ?
 * 0x4000..0x8000 - recive buffers
 *
 * Registers are bottom 32bits of each 64bit address, and the SRAMs for the
 * transmit and receive buffers seem to be 64bit capable even if the code says
 * they have 32bit data ports.
 */
REG32(MACLO, 0x800)

REG32(MACHI, 0x808)
    FIELD(MACHI, RX_LOOPBACK, 17, 1)
    FIELD(MACHI, RX_ALL, 22, 1)
    FIELD(MACHI, IRQ_EN, 23, 1)

REG32(TPLR, 0x810)
    FIELD(TPLR, FRAME_ADDR, 16, 12)
    FIELD(TPLR, PACKET_LEN, 0, 12)
    FIELD(TPLR, BUSY, 31, 1)

REG32(TFCS, 0x0818)

REG32(MDIOCTRL, 0x0820)
    FIELD(MDIOCTRL, M_CLK, 0, 1)
    FIELD(MDIOCTRL, M_DO, 1, 1)
    FIELD(MDIOCTRL, M_OE, 2, 1) /* 0 = in, 1 = out */
    FIELD(MDIOCTRL, M_DI, 3, 1)

REG32(RFCS, 0x0828)

REG32(RSR, 0x830)
    FIELD(RSR, RECV_FIRST, 0, 4)
    FIELD(RSR, RECV_NEXT, 4, 4)
    FIELD(RSR, RECV_LAST, 8, 4)
    FIELD(RSR, AVAIL, 12, 1)
    FIELD(RSR, IRQ,  13, 1)

REG32(RBAD, 0x0838)

#define R_RPLR          (0x0840)        /* array of up to 16 registers */
#define R_RPLR_END      (R_RPLR + ((NR_RPLR-1) * 8))

#define R_TXBUFF        (0x1000)
#define R_RXBUFF        (0x4000)

#define R_TXBUFF_END    (R_TXBUFF + TX_BUFF_SZ - 1)
#define R_RXBUFF_END    (R_RXBUFF + RX_BUFF_SZ - 1)

static uint32_t *find_register(LowriscEthState *s, hwaddr offset)
{
    if (offset >= R_RPLR && offset <= R_RPLR_END) {
        offset -= R_RPLR;
        offset /= 8;

        return &s->r_rplr[offset];
    }

    switch (offset >> 2) {
    case R_MACLO:
        return &s->r_maclo;
    case R_MACHI:
        return &s->r_machi;
    case R_MDIOCTRL:
        return &s->r_mdioctrl;
    case R_TPLR:
        return &s->r_tplr;
    case R_RFCS:
        return &s->r_rfcs;
    case R_RSR:
        return &s->r_rsr;

    default:
        fprintf(stderr, "failed to find register for offset 0x%04x\n",
                (unsigned)offset);
        return NULL;
    }

    return NULL;
}

static void lowrisc_eth_update_irq(LowriscEthState *s)
{
    unsigned next, first;
    uint32_t rsr = s->r_rsr;
    bool set = false;

    next = FIELD_EX32(rsr, RSR, RECV_NEXT);
    first = FIELD_EX32(rsr, RSR, RECV_FIRST);

    if (FIELD_EX32(s->r_machi, MACHI, IRQ_EN)) {
        if (next != first) {
            set = true;
        }
    }

    /* update rsr for availability and irq-signalled state */
    rsr = FIELD_DP32(rsr, RSR, AVAIL, (next != first) ? 1 : 0);
    s->r_rsr = FIELD_DP32(rsr, RSR, IRQ, set ? 1 : 0);

    trace_lowrisc_eth_irq(set, first, next,
                          FIELD_EX32(s->r_machi, MACHI, IRQ_EN));
    qemu_set_irq(s->irq, set);
}

static ssize_t lowrisc_eth_receive(NetClientState *nc,
                                   const uint8_t *buf, size_t size)
{
    LowriscEthState *s = qemu_get_nic_opaque(nc);
    unsigned next, first, last;
    unsigned index;
    uint32_t rsr = s->r_rsr;
    uint8_t *rxb;

    last = FIELD_EX32(rsr, RSR, RECV_LAST);
    next = FIELD_EX32(rsr, RSR, RECV_NEXT);
    first = FIELD_EX32(rsr, RSR, RECV_FIRST);

    trace_lowrisc_eth_rx((unsigned)size, first, next, last);

    if (next == ((first + last) & 15)) {
        /* we should not really get here, we're already full */
        return -1;
    }

    if (is_multicast_ether_addr(buf) || is_broadcast_ether_addr(buf)) {
        /* we're good for this packet */
    } else if (FIELD_EX32(s->r_machi, MACHI, RX_ALL)) {
        /* accepting everytihng, good here */
    } else if (FIELD_EX32(s->r_machi, MACHI, RX_LOOPBACK)) {
        /* should probably accept loopback packets...? */
    } else {
        /* check for destination being our MAC */
        if (memcmp(buf, s->conf.macaddr.a, sizeof(s->conf.macaddr.a)) != 0) {
            return size;
        }
    }

    /* accepting the packet, work out which slot to put it in */
    index = next & 7;
    rxb = s->rx_buff + (index * RX_SZ);

    trace_lowrisc_eth_rx_good((unsigned)size, index);

    memcpy(rxb, buf, size);
    //todo - add an actual FCS as it expects it in the rx buffer
    s->r_rplr[index] = size + 4;

    next = (next + 1) & 15;
    s->r_rsr = FIELD_DP32(rsr, RSR, RECV_NEXT, next);

    trace_lowrisc_eth_rx_upd_rsr(s->r_rsr);
    lowrisc_eth_update_irq(s);

    return size;
}

static bool lowrisc_eth_can_receive(NetClientState *nc)
{
    LowriscEthState *s = qemu_get_nic_opaque(nc);
    unsigned next, first, last;
    uint32_t rsr = s->r_rsr;
    bool ok;

    last = FIELD_EX32(rsr, RSR, RECV_LAST);
    next = FIELD_EX32(rsr, RSR, RECV_NEXT);
    first = FIELD_EX32(rsr, RSR, RECV_FIRST);
    ok = next != ((first + last) & 15);

    trace_lowrisc_eth_rx_check(first, next, first, ok);
    return ok;
}

#define make_mac(__m, __b) (((uint32_t)(__m)) << (__b))

static void lowrisc_eth_init_registers(LowriscEthState *s)
{
    const uint8_t *mac;

    /* general register init */

    s->r_tplr = 0;
    s->r_rfcs = 0;
    s->r_tplr = 0;
    s->r_rsr = 0;
    s->r_mdioctrl = FIELD_DP32(0x0, MDIOCTRL, M_DI, 1);
    memset(&s->r_rplr, 0, sizeof(s->r_rplr));

    /* init mac registers */

    mac = &s->conf.macaddr.a[0];
    s->r_maclo = make_mac(mac[5], 0) | make_mac(mac[4], 8) | make_mac(mac[3], 16) | make_mac(mac[2], 24);
    s->r_machi = make_mac(mac[1], 0) | make_mac(mac[0], 8);

    /* init the rx and tx buffres for now */
    memset(&s->rx_buff, 0x44, sizeof(s->rx_buff));
    memset(&s->tx_buff, 0x55, sizeof(s->tx_buff));
}

static void lowrisc_eth_reset(DeviceState *d)
{
    LowriscEthState *s = LOWRISC_ETH(d);

    lowrisc_eth_init_registers(s);
    lowrisc_eth_update_irq(s);
}

static uint64_t lowrisc_eth_read(void *opaque, hwaddr offset, unsigned size)
{
    LowriscEthState *s = opaque;
    uint64_t retval = 0;

    if (offset >= R_TXBUFF && offset <= R_TXBUFF_END) {
        uint64_t *ptr = (uint64_t *)(s->tx_buff + offset - R_TXBUFF);
        retval = *ptr;
    } else if (offset >= R_RXBUFF && offset <= R_RXBUFF_END) {
        uint64_t *ptr = (uint64_t *)(s->rx_buff + offset - R_RXBUFF);
        retval = *ptr;
    } else {
        uint32_t *reg = find_register(s, offset);

        if (reg) {
            retval = *reg;
        } else {
            retval = ~(uint64_t)0x0;
        }
    }

    /* note, there's nothing in the read path that would need updating
     * the irq state, so no need to re-sync interrupts */

    trace_lowrisc_eth_io_read(offset, retval);
    return retval;
}


static void lowrisc_eth_update_mdioctrl(LowriscEthState *s, uint32_t val)
{
    /* since we're not implementing any sort of bit-banged MDIO, we just
     * return the data input as high, which seems to be enough to allow
     * the PHY link checks to work
     */

    s->r_mdioctrl = FIELD_DP32(s->r_mdioctrl, MDIOCTRL, M_DI, 1);
}

/* update tplr register, assume we're transmitting a packet */
static void lowrisc_eth_update_tplr(LowriscEthState *s, uint32_t val)
{
    unsigned len = FIELD_EX32(val, TPLR, PACKET_LEN);

    s->r_tplr = val | R_TPLR_BUSY_MASK;

    trace_lowrisc_eth_tx(len);

    if (FIELD_EX32(s->r_machi, MACHI, RX_LOOPBACK)) {
        lowrisc_eth_receive(qemu_get_queue(s->nic),  s->tx_buff, len);
    } else {
        qemu_send_packet(qemu_get_queue(s->nic), s->tx_buff, len);
    }

    /* clear busy as we are done now, no irq (oversight?) to be raised */
    s->r_tplr &= ~R_TPLR_BUSY_MASK;
}

static void lowrisc_eth_update_mac(LowriscEthState *s)
{
    MACAddr addr;

    /* if the maclo or machi registers change, then change qemu config */

    addr.a[5] = s->r_maclo;
    addr.a[4] = s->r_maclo >> 8;
    addr.a[3] = s->r_maclo >> 16;
    addr.a[2] = s->r_maclo >> 24;
    addr.a[1] = s->r_machi;
    addr.a[0] = s->r_machi >> 8;

    if (memcmp(&addr, &s->conf.macaddr, sizeof(s->conf.macaddr)) != 0) {
        s->conf.macaddr = addr;
        qemu_format_nic_info_str(qemu_get_queue(s->nic), s->conf.macaddr.a);
    }
}

#define update_rsr(__s, __field, __val) \
    do { __s->r_rsr= FIELD_DP32(__s->r_rsr, RSR, __field, __val); } while(0)

static void lowrisc_eth_write(void *opaque, hwaddr offset, uint64_t val,
                              unsigned size)
{
    LowriscEthState *s = (LowriscEthState *)opaque;

    trace_lowrisc_eth_io_write(offset, val);

    if (offset >= R_TXBUFF && offset <= R_TXBUFF_END) {
        uint8_t *ptr = s->tx_buff;

        offset -= R_TXBUFF;
        size = MIN(size, sizeof(s->tx_buff)-offset);
        memcpy(ptr + offset, &val, size);
    } else if (offset >= R_RXBUFF && offset <= R_RXBUFF_END) {
        uint8_t *ptr = s->rx_buff;

        offset -= R_RXBUFF;
        size = MIN(size, sizeof(s->rx_buff)-offset);
        memcpy(ptr + offset, &val, size);
    } else {
        uint32_t *reg = find_register(s, offset);

        /* the core in cva6 may not fully check byte enables
         * so just assume we're writing to the registers in full */

        if (reg) {
            switch (offset >> 2) {
            case R_MACLO:
                *reg = val;
                lowrisc_eth_update_mac(s);
                break;

            case R_MACHI:
                *reg = val;
                lowrisc_eth_update_mac(s);
                lowrisc_eth_update_irq(s);
                break;

            case R_RSR:
                /* bits 3:0 of this write to the firstbuff field */
                update_rsr(s, RECV_FIRST, val & 15);
                lowrisc_eth_update_irq(s);
                break;

            case R_RFCS:
                /* bits 3:0 of this write to the lastbuff field */
                update_rsr(s, RECV_LAST, val & 15);
                lowrisc_eth_update_irq(s);
                break;

            case R_TPLR:
                *reg = val;
                lowrisc_eth_update_tplr(s, val);
                break;

            case R_MDIOCTRL:
                *reg = val;
                lowrisc_eth_update_mdioctrl(s, val);
                break;

            default:
                /* for now just assume anything else is just writable */
                *reg = val;
            }

            return;
        }
    }
}

static const MemoryRegionOps lowrisc_eth_ops = {
    .read = lowrisc_eth_read,
    .write = lowrisc_eth_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    /* set max access size to 64bit, for any register it is only 64bit
     * and tx/rx memory might be able to sub-write
     */
    .impl.max_access_size = 8,
};

static NetClientInfo net_lowrisc_eth_info = {
    .type = NET_CLIENT_DRIVER_NIC,
    .size = sizeof(NICState),
    .can_receive = lowrisc_eth_can_receive,
    .receive = lowrisc_eth_receive,
    /* note, we do not currently have any way of signaling link status */
};

static void lowrisc_eth_realize(DeviceState *dev, Error **errp)
{
    LowriscEthState *s = LOWRISC_ETH(dev);

    sysbus_init_irq(SYS_BUS_DEVICE(dev), &s->irq);

    qemu_macaddr_default_if_unset(&s->conf.macaddr);

    s->nic = qemu_new_nic(&net_lowrisc_eth_info, &s->conf,
                          object_get_typename(OBJECT(dev)), dev->id,
                          &dev->mem_reentrancy_guard, s);
}

static void lowrisc_eth_init(Object *obj)
{
    LowriscEthState *s = LOWRISC_ETH(obj);
    DeviceState *dev = DEVICE(obj);

    lowrisc_eth_init_registers(s);
    lowrisc_eth_update_irq(s);

    memory_region_init_io(&s->iomem, OBJECT(s), &lowrisc_eth_ops, s,
                          "net", R_RXBUFF_END);

    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->iomem);
}

static const VMStateDescription vmstate_lowrisc_eth = {
    .name = "lowrisc_eth",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(r_maclo, LowriscEthState),
        VMSTATE_UINT32(r_machi,  LowriscEthState),
        VMSTATE_UINT32(r_mdioctrl,  LowriscEthState),
        VMSTATE_UINT32(r_rfcs,  LowriscEthState),
        VMSTATE_UINT32(r_tplr,  LowriscEthState),
        VMSTATE_UINT32(r_rsr, LowriscEthState),
        VMSTATE_UINT32_ARRAY(r_rplr, LowriscEthState, NR_RPLR),

        /* might be overkill, but store rx and tx buffers */
        VMSTATE_UINT8_ARRAY(tx_buff, LowriscEthState, TX_BUFF_SZ),
        VMSTATE_UINT8_ARRAY(rx_buff, LowriscEthState, RX_BUFF_SZ),

        VMSTATE_END_OF_LIST(),
    },
};

static const Property lowrisc_eth_properties[] = {
    DEFINE_NIC_PROPERTIES(LowriscEthState, conf),
};

static void lowrisc_eth_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = lowrisc_eth_realize;
    device_class_set_props(dc, lowrisc_eth_properties);
    dc->vmsd = &vmstate_lowrisc_eth;
    device_class_set_legacy_reset(dc, lowrisc_eth_reset);
}

static const TypeInfo lowrisc_eth_info = {
    .name  = TYPE_LOWRISC_ETH,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size  = sizeof(LowriscEthState),
    .instance_init = lowrisc_eth_init,
    .class_init = lowrisc_eth_class_init,
};

static void lowrisc_eth_register_types(void)
{
    type_register_static(&lowrisc_eth_info);
}

type_init(lowrisc_eth_register_types)
