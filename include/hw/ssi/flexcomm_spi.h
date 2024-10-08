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

#ifndef HW_FLEXCOMM_SPI_H
#define HW_FLEXCOMM_SPI_H

#include "hw/misc/flexcomm_function.h"
#include "hw/ssi/ssi.h"

#define TYPE_FLEXCOMM_SPI "flexcomm-spi"
OBJECT_DECLARE_TYPE(FlexcommSpiState, FlexcommSpiClass, FLEXCOMM_SPI);

struct FlexcommSpiState {
    FlexcommFunction parent_obj;

    SSIBus *bus;
    qemu_irq cs[4];
    bool cs_asserted[4];
    uint32_t tx_ctrl;
};

struct FlexcommSpiClass {
    FlexcommFunctionClass parent_obj;

    FlexcommFunctionSelect select;
};

#endif /* HW_FLEXCOMM_SPI_H */
