/* MDIO GPIO based code
 *
 *  * Ben Dooks <ben.dooks@codethink.co.uk>
 * Copyright (c) 2025 Codethink Ltd,
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef MDIO_BB_H
#define MDIO_BB_H

 enum mdio_bb_state {
    MDG_IDLE,
    MDG_START0,
    MDG_OP0,
    MDG_OP1,
    MDG_ADDR,
    MDG_TURN1,
    MDG_TURN2,
    MDG_READ,
    MDG_WRITE,
    MDG_ILLEGAL,
};

#define OP_READ  (2)
#define OP_WRITE (1)

struct mdio_bb {
    const char *name;
    void *param;
    bool mdc, mdo, mdi;
    unsigned opcode;
    unsigned bitcount;
    unsigned phy_reg_addr;
    unsigned phy_data;
    enum mdio_bb_state state;

    /* addresses are supplied as addr[4:0] = reg, addr[10:5] = phy-addr */
    /* read called to read from phy, so supply data to the MDIO bus */
    unsigned (*read)(void *opaque, unsigned addr);
    /* write called when data written to phy */
    void (*write)(void *opaque, unsigned addr, unsigned data);
};

extern void mdio_bb_init(struct mdio_bb *bb);

extern void mdio_bb_update(struct mdio_bb *s,
                           bool mdc, bool mdo);
    
#endif /* MDIO_BB_H */
