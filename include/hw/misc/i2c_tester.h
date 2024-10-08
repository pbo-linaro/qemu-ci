/*
 * Simple I2C peripheral for testing I2C device models.
 *
 * At the moment of introducing this not all of the functionality can be tested
 * with an existing QEMU peripheral device, notably error paths such as when a
 * peripheral device responds with an I2C_NACK during a transaction.
 *
 * It also provides a place where new future functionality can be added to help
 * with more kinds of tests rather than trying to hack it in a real device where
 * it might not even be possible.
 *
 * The peripheral allows reading and writing to a fixed number of registers.
 * The first transmitted byte in a transaction sets the index register. Note
 * that the index register is not auto-incremented on neither reads nor writes.
 *
 * Copyright (c) 2024 Google LLC
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_I2C_TESTER_H
#define HW_I2C_TESTER_H

#include "qemu/osdep.h"
#include "hw/i2c/i2c.h"
#include "hw/irq.h"

#define I2C_TESTER_NUM_REGS    0x31

#define TYPE_I2C_TESTER "i2c-tester"
#define I2C_TESTER(obj) OBJECT_CHECK(I2cTesterState, (obj), TYPE_I2C_TESTER)

typedef struct {
    I2CSlave i2c;
    bool set_reg_idx;
    uint8_t reg_idx;
    uint8_t regs[I2C_TESTER_NUM_REGS];
} I2cTesterState;

#endif /* HW_I2C_TESTER_H */
