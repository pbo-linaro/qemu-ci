/*
 * Simple I2C peripheral for testing I2C device models.
 *
 * Copyright (c) 2024 Google LLC
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "hw/misc/i2c_tester.h"

#include "qemu/log.h"
#include "qemu/module.h"
#include "migration/vmstate.h"

static void i2c_tester_reset_enter(Object *o, ResetType type)
{
    I2cTesterState *s = I2C_TESTER(o);

    s->set_reg_idx = false;
    s->reg_idx     = 0;
    memset(s->regs, 0, I2C_TESTER_NUM_REGS);
}

static int i2c_tester_event(I2CSlave *i2c, enum i2c_event event)
{
    I2cTesterState *s = I2C_TESTER(i2c);

    if (event == I2C_START_SEND) {
        s->set_reg_idx = true;
    }

    return 0;
}

static uint8_t i2c_tester_rx(I2CSlave *i2c)
{
    I2cTesterState *s = I2C_TESTER(i2c);

    if (s->reg_idx >= I2C_TESTER_NUM_REGS) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid reg 0x%02x\n", __func__,
                      s->reg_idx);
        return I2C_NACK;
    }

    return s->regs[s->reg_idx];
}

static int i2c_tester_tx(I2CSlave *i2c, uint8_t data)
{
    I2cTesterState *s = I2C_TESTER(i2c);

    if (s->set_reg_idx) {
        /* Setting the register in which the operation will be done. */
        s->reg_idx = data;
        s->set_reg_idx = false;
        return 0;
    }

    if (s->reg_idx >= I2C_TESTER_NUM_REGS) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid reg 0x%02x\n", __func__,
                      s->reg_idx);
        return I2C_NACK;
    }

    /* Write reg data. */
    s->regs[s->reg_idx] = data;

    return 0;
}

static const VMStateDescription vmstate_i2c_tester = {
    .name = "i2c-tester",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_I2C_SLAVE(i2c, I2cTesterState),
        VMSTATE_BOOL(set_reg_idx, I2cTesterState),
        VMSTATE_UINT8(reg_idx, I2cTesterState),
        VMSTATE_UINT8_ARRAY(regs, I2cTesterState, I2C_TESTER_NUM_REGS),
        VMSTATE_END_OF_LIST()
    }
};

static void i2c_tester_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    ResettableClass *rc = RESETTABLE_CLASS(oc);
    I2CSlaveClass *isc = I2C_SLAVE_CLASS(oc);

    rc->phases.enter = i2c_tester_reset_enter;
    dc->vmsd = &vmstate_i2c_tester;
    isc->event = i2c_tester_event;
    isc->recv = i2c_tester_rx;
    isc->send = i2c_tester_tx;
}

static const TypeInfo i2c_tester_types[] = {
    {
        .name = TYPE_I2C_TESTER,
        .parent = TYPE_I2C_SLAVE,
        .instance_size = sizeof(I2cTesterState),
        .class_init = i2c_tester_class_init
    },
};

DEFINE_TYPES(i2c_tester_types);
