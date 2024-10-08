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

#ifndef HW_FLEXCOMM_H
#define HW_FLEXCOMM_H

#include "hw/sysbus.h"
#include "hw/arm/svd/flexcomm.h"
#include "qemu/fifo32.h"

#define FLEXCOMM_FUNC_USART     0
#define FLEXCOMM_FUNC_SPI       1
#define FLEXCOMM_FUNC_I2C       2
#define FLEXCOMM_FUNC_I2S       3
#define FLEXCOMM_FUNCTIONS 4

#define FLEXCOMM_FULL    0xF
#define FLEXCOMM_HSSPI   (1 << FLEXCOMM_FUNC_SPI)
#define FLEXCOMM_PMICI2C (1 << FLEXCOMM_FUNC_I2C)

#define FLEXCOMM_PERSEL_USART  1
#define FLEXCOMM_PERSEL_SPI    2
#define FLEXCOMM_PERSEL_I2C    3
#define FLEXCOMM_PERSEL_I2S_TX 4
#define FLEXCOMM_PERSEL_I2S_RX 5

#define TYPE_FLEXCOMM "flexcomm"
OBJECT_DECLARE_SIMPLE_TYPE(FlexcommState, FLEXCOMM);

struct FlexcommState {
    SysBusDevice parent_obj;

    MemoryRegion container;
    MemoryRegion mmio;
    uint32_t regs[FLEXCOMM_REGS_NO];
    uint32_t functions;
    qemu_irq irq;
    bool irq_state;
    Fifo32 rx_fifo;
    Fifo32 tx_fifo;
};

#endif /* HW_FLEXCOMM_H */
