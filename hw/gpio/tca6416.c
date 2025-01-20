/*
 * QEMU model of TCA6416 16-Bit I/O Expander
 *
 * Copyright (c) 2018 Xilinx Inc.
 *
 * Written by Sai Pavan Boddu <sai.pavan.boddu@xilinx.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include "qemu/osdep.h"
#include "hw/i2c/i2c.h"
#include "trace.h"

#define TYPE_TCA6416 "tca6416"
OBJECT_DECLARE_SIMPLE_TYPE(Tca6416State, TCA6416)

#define IN_PORT0    0
#define IN_PORT1    1
#define OUT_PORT0   2
#define OUT_PORT1   3
#define POL_INV0    4
#define POL_INV1    5
#define CONF_PORT0  6
#define CONF_PORT1  7
#define RMAX (CONF_PORT1 + 1)

enum tca6416_events {
     ADDR_DONE,
     ADDRESSING,
};

struct Tca6416State {
     I2CSlave i2c;

     uint8_t addr;
     uint8_t state;
     uint8_t regs[RMAX];
};

static uint8_t tca6416_read(I2CSlave *i2c)
{
    Tca6416State *s = TCA6416(i2c);
    uint8_t ret;

    ret = s->regs[s->addr];
    trace_tca6416_read(ret);
    return ret;
}

static int tca6416_write(I2CSlave *i2c, uint8_t data)
{
    Tca6416State *s = TCA6416(i2c);

    trace_tca6416_write(data);
    if (s->state == ADDRESSING) {
        s->addr = data;
    } else {
        s->regs[s->addr] = data;
    }

    return 0;
}

static void tca6416_realize(DeviceState *dev, Error **errp)
{
    Tca6416State *s = TCA6416(dev);

    s->regs[CONF_PORT0] = 0xFF;
    s->regs[CONF_PORT1] = 0xFF;
}

static int tca6416_event(I2CSlave *i2c, enum i2c_event event)
{
    Tca6416State *s = TCA6416(i2c);

    switch (event) {
    case I2C_START_SEND:
        s->state = ADDRESSING;
        break;
    default:
         s->state = ADDR_DONE;
    };
    return 0;
}

static void tca6416_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);

    dc->realize = tca6416_realize;
    k->recv = tca6416_read;
    k->send = tca6416_write;
    k->event = tca6416_event;
}

static const TypeInfo tca6416_types[] = {
    {
        .name = TYPE_TCA6416,
        .parent = TYPE_I2C_SLAVE,
        .class_init = tca6416_class_init,
        .instance_size = sizeof(Tca6416State),
    }
};

DEFINE_TYPES(tca6416_types)
