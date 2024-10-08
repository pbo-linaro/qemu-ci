/*
 * QEMU model for NXP's FLEXCOMM I2C
 *
 * Copyright (c) 2024 Google LLC
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_FLEXCOMM_I2C_H
#define HW_FLEXCOMM_I2C_H

#include "hw/i2c/i2c.h"
#include "hw/misc/flexcomm_function.h"

#define TYPE_FLEXCOMM_I2C "flexcomm-i2c"
OBJECT_DECLARE_TYPE(FlexcommI2cState, FlexcommI2cClass, FLEXCOMM_I2C);

struct FlexcommI2cState {
    FlexcommFunction parent_obj;

    I2CBus *bus;
};

struct FlexcommI2cClass {
    FlexcommFunctionClass parent_obj;

    FlexcommFunctionSelect select;
};

#define MSTSTATE_IDLE 0
#define MSTSTATE_RXRDY 1
#define MSTSTATE_TXRDY 2
#define MSTSTATE_NAKADR 3
#define MSTSTATE_NAKDAT 4


#endif /* HW_FLEXCOMM_I2C_H */
