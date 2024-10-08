/*
 * Copyright 2016-2023 NXP SPDX-License-Identifier: BSD-3-Clause
 *
 * Automatically generated by svd-gen-header.py from MIMXRT595S_cm33.xml
 */
#pragma once

#include "hw/register.h"

/* Flexcomm USART */
#define FLEXCOMM_USART_REGS_NO (1024)

/* USART Configuration */
REG32(FLEXCOMM_USART_CFG, 0x0);
/* USART Enable */
FIELD(FLEXCOMM_USART_CFG, ENABLE, 0, 1);

/* USART Control */
REG32(FLEXCOMM_USART_CTL, 0x4);

/* USART Status */
REG32(FLEXCOMM_USART_STAT, 0x8);

/* Interrupt Enable Read and Set for USART (not FIFO) Status */
REG32(FLEXCOMM_USART_INTENSET, 0xC);

/* Interrupt Enable Clear */
REG32(FLEXCOMM_USART_INTENCLR, 0x10);

/* Baud Rate Generator */
REG32(FLEXCOMM_USART_BRG, 0x20);

/* Interrupt Status */
REG32(FLEXCOMM_USART_INTSTAT, 0x24);

/* Oversample Selection Register for Asynchronous Communication */
REG32(FLEXCOMM_USART_OSR, 0x28);

/* Address Register for Automatic Address Matching */
REG32(FLEXCOMM_USART_ADDR, 0x2C);

/* FIFO Configuration */
REG32(FLEXCOMM_USART_FIFOCFG, 0xE00);
/* Enable the Transmit FIFO. */
FIELD(FLEXCOMM_USART_FIFOCFG, ENABLETX, 0, 1);
/* Enable the Receive FIFO */
FIELD(FLEXCOMM_USART_FIFOCFG, ENABLERX, 1, 1);
/* Empty Command for the Transmit FIFO */
FIELD(FLEXCOMM_USART_FIFOCFG, EMPTYTX, 16, 1);
/* Empty Command for the Receive FIFO */
FIELD(FLEXCOMM_USART_FIFOCFG, EMPTYRX, 17, 1);

/* FIFO Status */
REG32(FLEXCOMM_USART_FIFOSTAT, 0xE04);
/* TX FIFO Error */
FIELD(FLEXCOMM_USART_FIFOSTAT, TXERR, 0, 1);
/* RX FIFO Error */
FIELD(FLEXCOMM_USART_FIFOSTAT, RXERR, 1, 1);
/* Peripheral Interrupt */
FIELD(FLEXCOMM_USART_FIFOSTAT, PERINT, 3, 1);
/* Transmit FIFO Empty */
FIELD(FLEXCOMM_USART_FIFOSTAT, TXEMPTY, 4, 1);
/* Transmit FIFO is Not Full */
FIELD(FLEXCOMM_USART_FIFOSTAT, TXNOTFULL, 5, 1);
/* Receive FIFO is Not Empty */
FIELD(FLEXCOMM_USART_FIFOSTAT, RXNOTEMPTY, 6, 1);
/* Receive FIFO is Full */
FIELD(FLEXCOMM_USART_FIFOSTAT, RXFULL, 7, 1);
/* Transmit FIFO Current Level */
FIELD(FLEXCOMM_USART_FIFOSTAT, TXLVL, 8, 5);
/* Receive FIFO Current Level */
FIELD(FLEXCOMM_USART_FIFOSTAT, RXLVL, 16, 5);

/* FIFO Trigger Settings for Interrupt and DMA Request */
REG32(FLEXCOMM_USART_FIFOTRIG, 0xE08);
/* Transmit FIFO Level Trigger Enable. */
FIELD(FLEXCOMM_USART_FIFOTRIG, TXLVLENA, 0, 1);
/* Receive FIFO Level Trigger Enable */
FIELD(FLEXCOMM_USART_FIFOTRIG, RXLVLENA, 1, 1);
/* Transmit FIFO Level Trigger Point */
FIELD(FLEXCOMM_USART_FIFOTRIG, TXLVL, 8, 4);
/* Trigger when the TX FIFO becomes empty */
#define FLEXCOMM_USART_FIFOTRIG_TXLVL_TXLVL0 0
/* Trigger when the TX FIFO level decreases to 1 entry */
#define FLEXCOMM_USART_FIFOTRIG_TXLVL_TXLVL1 1
/* Trigger when the TX FIFO level decreases to 15 entries (is no longer full) */
#define FLEXCOMM_USART_FIFOTRIG_TXLVL_TXLVL15 15
/* Receive FIFO Level Trigger Point */
FIELD(FLEXCOMM_USART_FIFOTRIG, RXLVL, 16, 4);
/* Trigger when the RX FIFO has received 1 entry (is no longer empty) */
#define FLEXCOMM_USART_FIFOTRIG_RXLVL_RXLVL1 0
/* Trigger when the RX FIFO has received 2 entries */
#define FLEXCOMM_USART_FIFOTRIG_RXLVL_RXLVL2 1
/* Trigger when the RX FIFO has received 16 entries (has become full) */
#define FLEXCOMM_USART_FIFOTRIG_RXLVL_RXLVL15 15

/* FIFO Interrupt Enable */
REG32(FLEXCOMM_USART_FIFOINTENSET, 0xE10);
/* Transmit Error Interrupt Enable */
FIELD(FLEXCOMM_USART_FIFOINTENSET, TXERR, 0, 1);
/* Receive Error Interrupt Enable */
FIELD(FLEXCOMM_USART_FIFOINTENSET, RXERR, 1, 1);
/* Transmit FIFO Level Interrupt Enable */
FIELD(FLEXCOMM_USART_FIFOINTENSET, TXLVL, 2, 1);
/* Receive FIFO Level Interrupt Enable */
FIELD(FLEXCOMM_USART_FIFOINTENSET, RXLVL, 3, 1);

/* FIFO Interrupt Enable Clear */
REG32(FLEXCOMM_USART_FIFOINTENCLR, 0xE14);
/* Transmit Error Interrupt Enable */
FIELD(FLEXCOMM_USART_FIFOINTENCLR, TXERR, 0, 1);
/* Receive Error Interrupt Enable */
FIELD(FLEXCOMM_USART_FIFOINTENCLR, RXERR, 1, 1);
/* Transmit FIFO Level Interrupt Enable */
FIELD(FLEXCOMM_USART_FIFOINTENCLR, TXLVL, 2, 1);
/* Receive FIFO Level Interrupt Enable */
FIELD(FLEXCOMM_USART_FIFOINTENCLR, RXLVL, 3, 1);

/* FIFO Interrupt Status */
REG32(FLEXCOMM_USART_FIFOINTSTAT, 0xE18);
/* TX FIFO Error Interrupt Status */
FIELD(FLEXCOMM_USART_FIFOINTSTAT, TXERR, 0, 1);
/* RX FIFO Error Interrupt Status */
FIELD(FLEXCOMM_USART_FIFOINTSTAT, RXERR, 1, 1);
/* Transmit FIFO Level Interrupt Status */
FIELD(FLEXCOMM_USART_FIFOINTSTAT, TXLVL, 2, 1);
/* Receive FIFO Level Interrupt Status */
FIELD(FLEXCOMM_USART_FIFOINTSTAT, RXLVL, 3, 1);
/* Peripheral Interrupt Status */
FIELD(FLEXCOMM_USART_FIFOINTSTAT, PERINT, 4, 1);

/* FIFO Write Data */
REG32(FLEXCOMM_USART_FIFOWR, 0xE20);
/* Transmit data to the FIFO */
FIELD(FLEXCOMM_USART_FIFOWR, TXDATA, 0, 9);

/* FIFO Read Data */
REG32(FLEXCOMM_USART_FIFORD, 0xE30);
/* Received Data from the FIFO */
FIELD(FLEXCOMM_USART_FIFORD, RXDATA, 0, 9);
/* Framing Error Status Flag */
FIELD(FLEXCOMM_USART_FIFORD, FRAMERR, 13, 1);
/* Parity Error Status Flag */
FIELD(FLEXCOMM_USART_FIFORD, PARITYERR, 14, 1);
/* Received Noise Flag */
FIELD(FLEXCOMM_USART_FIFORD, RXNOISE, 15, 1);

/* FIFO Data Read with No FIFO Pop */
REG32(FLEXCOMM_USART_FIFORDNOPOP, 0xE40);
/* Received Data from the FIFO */
FIELD(FLEXCOMM_USART_FIFORDNOPOP, RXDATA, 0, 9);
/* Framing Error Status Flag */
FIELD(FLEXCOMM_USART_FIFORDNOPOP, FRAMERR, 13, 1);
/* Parity Error Status Flag */
FIELD(FLEXCOMM_USART_FIFORDNOPOP, PARITYERR, 14, 1);
/* Received Noise Flag */
FIELD(FLEXCOMM_USART_FIFORDNOPOP, RXNOISE, 15, 1);

/* FIFO Size */
REG32(FLEXCOMM_USART_FIFOSIZE, 0xE48);
/* FIFO Size */
FIELD(FLEXCOMM_USART_FIFOSIZE, FIFOSIZE, 0, 5);

/* Peripheral Identification */
REG32(FLEXCOMM_USART_ID, 0xFFC);


#define FLEXCOMM_USART_REGISTER_ACCESS_INFO_ARRAY(_name) \
    struct RegisterAccessInfo _name[FLEXCOMM_USART_REGS_NO] = { \
        [0 ... FLEXCOMM_USART_REGS_NO - 1] = { \
            .name = "", \
            .addr = -1, \
        }, \
        [0x0] = { \
            .name = "CFG", \
            .addr = 0x0, \
            .ro = 0xFF032402, \
            .reset = 0x0, \
        }, \
        [0x1] = { \
            .name = "CTL", \
            .addr = 0x4, \
            .ro = 0xFFFEFCB9, \
            .reset = 0x0, \
        }, \
        [0x2] = { \
            .name = "STAT", \
            .addr = 0x8, \
            .ro = 0xFFFE07DF, \
            .reset = 0xA, \
        }, \
        [0x3] = { \
            .name = "INTENSET", \
            .addr = 0xC, \
            .ro = 0xFFFE0797, \
            .reset = 0x0, \
        }, \
        [0x4] = { \
            .name = "INTENCLR", \
            .addr = 0x10, \
            .ro = 0xFFFE0797, \
            .reset = 0x0, \
        }, \
        [0x8] = { \
            .name = "BRG", \
            .addr = 0x20, \
            .ro = 0xFFFF0000, \
            .reset = 0x0, \
        }, \
        [0x9] = { \
            .name = "INTSTAT", \
            .addr = 0x24, \
            .ro = 0xFFFFFFFF, \
            .reset = 0x0, \
        }, \
        [0xA] = { \
            .name = "OSR", \
            .addr = 0x28, \
            .ro = 0xFFFFFFF0, \
            .reset = 0xF, \
        }, \
        [0xB] = { \
            .name = "ADDR", \
            .addr = 0x2C, \
            .ro = 0xFFFFFF00, \
            .reset = 0x0, \
        }, \
        [0x380] = { \
            .name = "FIFOCFG", \
            .addr = 0xE00, \
            .ro = 0xFFF80FFC, \
            .reset = 0x0, \
        }, \
        [0x381] = { \
            .name = "FIFOSTAT", \
            .addr = 0xE04, \
            .ro = 0xFFFFFFFC, \
            .reset = 0x30, \
        }, \
        [0x382] = { \
            .name = "FIFOTRIG", \
            .addr = 0xE08, \
            .ro = 0xFFF0F0FC, \
            .reset = 0x0, \
        }, \
        [0x384] = { \
            .name = "FIFOINTENSET", \
            .addr = 0xE10, \
            .ro = 0xFFFFFFF0, \
            .reset = 0x0, \
        }, \
        [0x385] = { \
            .name = "FIFOINTENCLR", \
            .addr = 0xE14, \
            .ro = 0xFFFFFFF0, \
            .reset = 0x0, \
        }, \
        [0x386] = { \
            .name = "FIFOINTSTAT", \
            .addr = 0xE18, \
            .ro = 0xFFFFFFFF, \
            .reset = 0x0, \
        }, \
        [0x388] = { \
            .name = "FIFOWR", \
            .addr = 0xE20, \
            .ro = 0xFFFFFE00, \
            .reset = 0x0, \
        }, \
        [0x38C] = { \
            .name = "FIFORD", \
            .addr = 0xE30, \
            .ro = 0xFFFFFFFF, \
            .reset = 0x0, \
        }, \
        [0x390] = { \
            .name = "FIFORDNOPOP", \
            .addr = 0xE40, \
            .ro = 0xFFFFFFFF, \
            .reset = 0x0, \
        }, \
        [0x392] = { \
            .name = "FIFOSIZE", \
            .addr = 0xE48, \
            .ro = 0xFFFFFFFF, \
            .reset = 0x10, \
        }, \
        [0x3FF] = { \
            .name = "ID", \
            .addr = 0xFFC, \
            .ro = 0xFFFFFFFF, \
            .reset = 0xE0102100, \
        }, \
    }
