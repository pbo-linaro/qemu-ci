/*
 * QEMU MDIO bit-bang emulaiton
 *
 * Ben Dooks <ben.dooks@codethink.co.uk>
 * Copyright (c) 2025 Codethink Ltd,
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/net/mdio_bb.h"

#include "trace.h"

void mdio_bb_init(struct mdio_bb *s)
{
    s->mdi = true;
    s->mdo = true;
    s->mdc = true;

    s->opcode = 0;
    s->bitcount = 0;
    s->phy_reg_addr = 0;
    s->phy_data = 0;
    s->state = MDG_IDLE;
}

void mdio_bb_update(struct mdio_bb *s,
                    bool mdc, bool mdo)
{
    enum mdio_bb_state n_state = MDG_ILLEGAL;
    bool rising = (!s->mdc && mdc);

    s->mdc = mdc;
    s->mdo = mdo;

    /* work on rising edge of mdclk */
    if (!rising)
        return;

    trace_mdio_bb_update(s->name, s->state, mdc, mdo);

    switch (s->state) {
    case MDG_IDLE:
        /* if we get a '1' stick in idle,the pre-amble is 32 '1' bits */

        if (!mdo) {
            trace_mdio_bb_start(s->name);
            n_state = MDG_START0;
        } else {
            n_state = MDG_IDLE;
        }
        break;

    case MDG_START0:
        if (!mdo) {
            n_state = MDG_IDLE;
        } else {
            n_state = MDG_OP0;
        };
        break;

    case MDG_OP0:
        s->opcode = mdo << 1;
        n_state = MDG_OP1;
        break;

    case MDG_OP1:
        s->opcode |= mdo;
        s->bitcount = 0;
        s->phy_reg_addr = 0;

        if (s->opcode == OP_READ || s->opcode == OP_WRITE) {
            n_state = MDG_ADDR;
        } else {
            fprintf(stderr, "illegal MDIO op %02x\n", s->opcode);
            n_state = MDG_ILLEGAL;
        }
        break;

    case MDG_ADDR:
        s->phy_reg_addr <<= 1;
        s->phy_reg_addr |= mdo;
        s->bitcount++;

        if (s->bitcount == 10) {
            n_state = MDG_TURN1;
        } else {
            n_state = MDG_ADDR;
        }
        break;

    case MDG_TURN1:
        n_state = MDG_TURN2;
        break;

    case MDG_TURN2:
        s->bitcount = 15;

        if (s->opcode == OP_READ) {
            s->phy_data = (s->read)(s->param, s->phy_reg_addr);

            trace_mdio_bb_read(s->name, s->phy_reg_addr >> 5,
                               s->phy_reg_addr & 0x1f, s->phy_data);
            n_state = MDG_READ;
        } else {
            n_state = MDG_WRITE;
        }
        break;

    case MDG_READ:
        s->mdi = s->phy_data & (1 << s->bitcount) ? 1 : 0;

        if (s->bitcount == 0) {
            n_state = MDG_IDLE;
        } else {
            s->bitcount--;
            n_state = MDG_READ;
        }
        break;

    case MDG_WRITE:
        /* writing data to the phy, mirror the mdi as the same as mdo in case
         * it is being checked, otherwise collect bits and invoke the write when
         * all the bits are received
         */
        s->mdi = mdo;

        if (mdo) {
            s->phy_data |= 1 << s->bitcount;
        }

        if (!s->bitcount) {
            trace_mdio_bb_write(s->name, s->phy_reg_addr >> 5,
                                s->phy_reg_addr & 0x1f, s->phy_data);
            (s->write)(s->param, s->phy_reg_addr, s->phy_data);
            n_state = MDG_IDLE;
        } else {
            s->bitcount--;
            n_state = MDG_WRITE;
        }
        break;

    case MDG_ILLEGAL:
        n_state = MDG_IDLE;
        break;

    default:
        /* should not need a default state, but if so, illega. */
        n_state = MDG_ILLEGAL;
    }

    if (n_state != MDG_ILLEGAL) {
        trace_mdio_bb_new_state(s->name, s->state, n_state);
        s->state = n_state;
    } else {
        /* encountered an illegal state. not much we can do here but go back
         * into idle and hope that the reader is going to try and reset?
         */

        trace_mdio_bb_illegal_state(s->name, s->state, mdo);

        fprintf(stderr, "mdio_bb: illegal next state from current %d (mdo %u)\n", s->state, mdo);
        s->state = MDG_IDLE;
    }
}
