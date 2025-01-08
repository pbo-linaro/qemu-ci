/*
 * IRQ sharing device.
 *
 * Copyright (c) 2025 Bernhard Beschow <shentey@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/core/shared-irq.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "qapi/error.h"
#include "migration/vmstate.h"

static void shared_irq_handler(void *opaque, int n, int level)
{
    SharedIRQ *s = opaque;
    uint16_t mask = BIT(n);

    if (level) {
        s->irq_states |= mask;
    } else {
        s->irq_states &= ~mask;
    }

    qemu_set_irq(s->out_irq, !!s->irq_states);
}

static void shared_irq_init(Object *obj)
{
    SharedIRQ *s = SHARED_IRQ(obj);

    qdev_init_gpio_out(DEVICE(s), &s->out_irq, 1);
}

static void shared_irq_realize(DeviceState *dev, Error **errp)
{
    SharedIRQ *s = SHARED_IRQ(dev);

    if (s->num_lines < 1 || s->num_lines >= MAX_SHARED_LINES) {
        error_setg(errp,
                   "IRQ shared number of lines %d must be between 1 and %d",
                   s->num_lines, MAX_SHARED_LINES);
        return;
    }

    qdev_init_gpio_in(dev, shared_irq_handler, s->num_lines);
}

static const Property shared_irq_properties[] = {
    DEFINE_PROP_UINT8("num-lines", SharedIRQ, num_lines, 1),
};

static const VMStateDescription shared_irq_vmstate = {
    .name = TYPE_SHARED_IRQ,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT16(irq_states, SharedIRQ),
        VMSTATE_END_OF_LIST()
    },
};

static void shared_irq_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    /* No state to reset */
    device_class_set_props(dc, shared_irq_properties);
    dc->vmsd = &shared_irq_vmstate;
    dc->realize = shared_irq_realize;

    /* Reason: Needs to be wired up to work */
    dc->user_creatable = false;
}

static const TypeInfo shared_irq_types[] = {
    {
       .name = TYPE_SHARED_IRQ,
       .parent = TYPE_DEVICE,
       .instance_size = sizeof(SharedIRQ),
       .instance_init = shared_irq_init,
       .class_init = shared_irq_class_init,
    },
};

DEFINE_TYPES(shared_irq_types)
