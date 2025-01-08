/*
 * IRQ sharing device.
 *
 * Copyright (c) 2025 Bernhard Beschow <shentey@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/*
 * This is a simple device which has one GPIO output line and multiple GPIO
 * input lines. The output line is active if at least one of the input lines is.
 *
 * QEMU interface:
 *  + N unnamed GPIO inputs: the input lines
 *  + one unnamed GPIO output: the output line
 *  + QOM property "num-lines": sets the number of input lines
 */
#ifndef HW_SHARED_IRQ_H
#define HW_SHARED_IRQ_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_SHARED_IRQ "shared-irq"

#define MAX_SHARED_LINES 16


OBJECT_DECLARE_SIMPLE_TYPE(SharedIRQ, SHARED_IRQ)

struct SharedIRQ {
    DeviceState parent_obj;

    qemu_irq out_irq;
    uint16_t irq_states;
    uint8_t num_lines;
};

#endif
