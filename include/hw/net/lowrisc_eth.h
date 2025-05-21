/*
 * QEMU LowRISC ethernet emulation
 *
  * Ben Dooks <ben.dooks@codethink.co.uk>
  * Copyright (c) 2025 Codethink Ltd,
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */


#ifndef LOWRISC_ETH_H
#define LOWRISC_ETH_H
#include "qom/object.h"

#define TYPE_LOWRISC_ETH "lowrisc_eth"
OBJECT_DECLARE_SIMPLE_TYPE(LowriscEthState, LOWRISC_ETH)

#include "net/net.h"
#include "hw/sysbus.h"
#include "hw/net/mdio_bb.h"

#define RX_SZ           (2048)
#define NR_RX_BUFFS     (8)

#define RX_BUFF_SZ (NR_RX_BUFFS * RX_SZ)
#define TX_BUFF_SZ (0x1000)

/* whilst the rx pointers are all 4 bit, the core only uses 3 bits for buffer index */
#define NR_RPLR (8)

struct LowriscEthState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;
    NICState *nic;
    NICConf conf;
    qemu_irq irq;

    /* the mdio bus */
    struct mdio_bb mdio_bb;

    /* register states */
    uint32_t r_maclo;
    uint32_t r_machi;
    uint32_t r_mdioctrl;
    uint32_t r_rfcs;
    uint32_t r_tplr;
    uint32_t r_rsr;
    uint32_t r_rplr[NR_RPLR];

    /* packet buffers */
    uint8_t rx_buff[RX_BUFF_SZ];
    uint8_t tx_buff[TX_BUFF_SZ];
};

#endif /* LOWRISC_ETH_H */
