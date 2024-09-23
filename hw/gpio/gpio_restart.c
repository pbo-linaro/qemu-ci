/*
 * GPIO qemu restart controller
 *
 * Copyright (c) 2020 Linaro Limited
 *
 * Author: Maxim Uvarov <maxim.uvarov@linaro.org>
 *
 * Virtual gpio driver which can be used on top of pl061 to reboot qemu virtual
 * machine. One of use case is gpio driver for secure world application (ARM
 * Trusted Firmware).
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "sysemu/runstate.h"

#define TYPE_GPIO_RESTART "gpio-restart"
OBJECT_DECLARE_SIMPLE_TYPE(GpioRestartState, GPIO_RESTART)

struct GpioRestartState {
    SysBusDevice parent_obj;
};

static void gpio_restart(void *opaque, int n, int level)
{
    if (level) {
        qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
    }
}

static void gpio_restart_init(Object *obj)
{
    DeviceState *dev = DEVICE(obj);

    qdev_init_gpio_in(dev, gpio_restart, 1);
}

static const TypeInfo types[] = {
    {
        .name          = TYPE_GPIO_RESTART,
        .parent        = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(GpioRestartState),
        .instance_init = gpio_restart_init,
    },
};

DEFINE_TYPES(types)
