/*
 * STM32 System-on-Chip general purpose input/output register definition
 *
 * Copyright 2024 Román Cárdenas <rcardenas.rod@gmail.com>
 *
 * Based on sifive_gpio.c:
 *
 * Copyright 2019 AdaCore
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/arm/stm32.h"
#include "hw/gpio/stm32_gpio.h"
#include "migration/vmstate.h"
#include "trace.h"

static void stm32_gpio_update_state(STM32GPIOState *s)
{
    bool prev_id, new_id, od, in, in_mask;
    uint8_t mode, pupd;

    for (size_t i = 0; i < s->ngpio; i++) {
        prev_id = extract32(s->idr, i, 1);
        od = extract32(s->odr, i, 1);
        in = extract32(s->in, i, 1);
        in_mask = extract32(s->in_mask, i, 1);

        mode = extract32(s->moder, i * 2, 2);
        pupd = extract32(s->pupdr, i * 2, 2);

        /* Pin both driven externally and internally */
        if (mode == STM32_GPIO_MODE_OUTPUT && in_mask) {
            qemu_log_mask(LOG_GUEST_ERROR, "GPIO pin %zu short circuited\n", i);
        }

        if (in_mask) {
            /* The pin is driven by external device */
            new_id = in;
        } else if (mode == STM32_GPIO_MODE_OUTPUT) {
            /* The pin is driven by internal circuit */
            new_id = od;
        } else {
            /* Floating? Apply pull-up resistor */
            new_id = pupd == STM32_GPIO_PULL_UP;
        }

        /* Update IDR */
        s->idr = deposit32(s->idr, i, 1, new_id);

        /* If pin is in input mode and IDR has changed, trigger an IRQ */
        if (new_id != prev_id) {
            if (mode == STM32_GPIO_MODE_INPUT) {
                qemu_set_irq(s->input_irq[i], new_id);
            }
        }
    }
    /* Notify that GPIO has changed its state */
    qemu_irq_pulse(s->state_irq);
}

static void stm32_gpio_reset(DeviceState *dev)
{
    STM32GPIOState *s = STM32_GPIO(dev);

    /*
     * Enabled is not affected by reset. It is ruled by RCC IDR is not
     * directly reset. It is updated at the end by update_state
     */

    /* By default, we set all the registers to 0 */
    s->moder = 0;
    s->otyper = 0;
    s->ospeedr = 0;
    s->pupdr = 0;
    s->odr = 0;
    s->lckr = 0;
    s->aflr = 0;
    s->afhr = 0;

    /* Next, we check model particularities */
    if (s->family == STM32_F4) {
        if (s->port == STM32_GPIO_PORT_A) {
            s->moder     = 0xA8000000;
            s->pupdr   = 0x64000000;
        } else if (s->port == STM32_GPIO_PORT_B) {
            s->moder     = 0x00000280;
            s->ospeedr = 0x000000C0;
            s->pupdr   = 0x00000100;
        }
    }

    stm32_gpio_update_state(s);
}

static void stm32_gpio_irq_reset(void *opaque, int line, int value)
{
    STM32GPIOState *s = STM32_GPIO(opaque);

    trace_stm32_gpio_irq_reset(line, value);

    bool prev_reset = s->reset;
    s->reset = value != 0;
    if (prev_reset != s->reset) {
        if (s->reset) {
            stm32_gpio_reset(DEVICE(s));
        } else {
            stm32_gpio_update_state(s);
        }
    }
}

static void stm32_gpio_irq_enable(void *opaque, int line, int value)
{
    STM32GPIOState *s = STM32_GPIO(opaque);

    trace_stm32_gpio_irq_enable(line, value);

    bool prev_enable = s->enable;
    s->enable = value != 0;
    if (prev_enable != s->enable) {
        stm32_gpio_update_state(s);
    }
}

static void stm32_gpio_irq_set(void *opaque, int line, int value)
{
    STM32GPIOState *s = STM32_GPIO(opaque);

    trace_stm32_gpio_irq_set(line, value);

    assert(line >= 0 && line < s->ngpio);

    s->in_mask = deposit32(s->in_mask, line, 1, value >= 0);

    /*
     * If value < 0, the pin is connected to a load.
     * If value == 0, the pin is low.
     * If value > 0, the pin is high.
     */
    if (value >= 0) {
        s->in = deposit32(s->in, line, 1, value != 0);
    }

    stm32_gpio_update_state(s);
}


static uint64_t stm32_gpio_read(void *opaque, hwaddr offset, unsigned int size)
{
    STM32GPIOState *s = STM32_GPIO(opaque);
    uint64_t r = 0;

    if (!s->enable) {
        qemu_log_mask(
            LOG_GUEST_ERROR, "%s: GPIO peripheral is disabled\n", __func__
        );
        return 0;
    }

    switch (offset) {
    case STM32_GPIO_REG_MODER:
        r = s->moder;
        break;

    case STM32_GPIO_REG_OTYPER:
        r = s->otyper;
        break;

    case STM32_GPIO_REG_OSPEEDR:
        r = s->ospeedr;
        break;

    case STM32_GPIO_REG_PUPDR:
        r = s->pupdr;
        break;

    case STM32_GPIO_REG_IDR:
        r = s->idr;
        break;

    case STM32_GPIO_REG_ODR:
        r = s->odr;
        break;

    case STM32_GPIO_REG_BSRR:
        break; /* BSRR is write-only */

    case STM32_GPIO_REG_LCKR:
        r = s->lckr;
        break;

    case STM32_GPIO_REG_AFRL:
        r = s->aflr;
        break;

    case STM32_GPIO_REG_AFRH:
        r = s->afhr;
        break;

    case STM32_GPIO_REG_BRR:
        if (s->family != STM32_F4) {
            break; /* BRR is write-only */
        }
        /* STM32F4xx SoCs do not have this register */
        qemu_log_mask(
            LOG_GUEST_ERROR,
            "%s: bad read offset 0x%" HWADDR_PRIx "\n",  __func__, offset
        );
        break;

    default:
        qemu_log_mask(
            LOG_GUEST_ERROR,
            "%s: bad read offset 0x%" HWADDR_PRIx "\n",  __func__, offset
        );
    }

    trace_stm32_gpio_read(offset, r);

    return r;
}

static void stm32_gpio_write(void *opaque, hwaddr offset,
                             uint64_t value, unsigned int size)
{
    STM32GPIOState *s = STM32_GPIO(opaque);

    trace_stm32_gpio_write(offset, value);

    if (!s->enable) {
        qemu_log_mask(
            LOG_GUEST_ERROR, "%s: GPIO peripheral is disabled\n", __func__
        );
        return;
    }

    switch (offset) {

    case STM32_GPIO_REG_MODER:
        s->moder = value;
        break;

    case STM32_GPIO_REG_OTYPER:
        s->otyper = value;
        break;

    case STM32_GPIO_REG_OSPEEDR:
        s->ospeedr = value;
        break;

    case STM32_GPIO_REG_PUPDR:
        s->pupdr = value;
        break;

    case STM32_GPIO_REG_IDR:
        break; /* IDR is read-only */

    case STM32_GPIO_REG_ODR:
        s->odr = value; /* IDR is updated in update_state */
        break;

    case STM32_GPIO_REG_BSRR:
        s->odr &= ~((value >> 16) & 0xFFFF);
        /* set bits have higher priority than reset bits */
        s->odr |= value & 0xFFFF;
        break;

    case STM32_GPIO_REG_LCKR:
        s->lckr = value;
        break;

    case STM32_GPIO_REG_AFRL:
        s->aflr = value;
        break;

    case STM32_GPIO_REG_AFRH:
        s->afhr = value;
        break;

    case STM32_GPIO_REG_BRR:
        if (s->family != STM32_F4) {
            s->odr &= ~(value & 0xFFFF);
            break;
        }
        /* STM32F4xx SoCs do not have this register */
        qemu_log_mask(
            LOG_GUEST_ERROR, "%s: bad write offset 0x%" HWADDR_PRIx "\n",
            __func__, offset
        );
        break;

    default:
        qemu_log_mask(
            LOG_GUEST_ERROR, "%s: bad write offset 0x%" HWADDR_PRIx "\n",
            __func__, offset
        );
    }

    stm32_gpio_update_state(s);
}

static const MemoryRegionOps stm32_gpio_ops = {
    .read =  stm32_gpio_read,
    .write = stm32_gpio_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
};

static const VMStateDescription vmstate_stm32_gpio = {
    .name = TYPE_STM32_GPIO,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(moder, STM32GPIOState),
        VMSTATE_UINT32(otyper, STM32GPIOState),
        VMSTATE_UINT32(ospeedr, STM32GPIOState),
        VMSTATE_UINT32(pupdr, STM32GPIOState),
        VMSTATE_UINT32(idr, STM32GPIOState),
        VMSTATE_UINT32(odr, STM32GPIOState),
        VMSTATE_UINT32(lckr, STM32GPIOState),
        VMSTATE_UINT32(aflr, STM32GPIOState),
        VMSTATE_UINT32(afhr, STM32GPIOState),
        VMSTATE_BOOL(reset, STM32GPIOState),
        VMSTATE_BOOL(enable, STM32GPIOState),
        VMSTATE_UINT32(in, STM32GPIOState),
        VMSTATE_UINT32(in_mask, STM32GPIOState),
        VMSTATE_END_OF_LIST()
    }
};

static Property stm32_gpio_properties[] = {
    DEFINE_PROP_UINT32("family", STM32GPIOState, family, STM32_F2),
    DEFINE_PROP_UINT32("port", STM32GPIOState, port, STM32_GPIO_PORT_A),
    DEFINE_PROP_UINT32("ngpio", STM32GPIOState, ngpio, STM32_GPIO_NPINS),
    DEFINE_PROP_END_OF_LIST(),
};

static void stm32_gpio_realize(DeviceState *dev, Error **errp)
{
    STM32GPIOState *s = STM32_GPIO(dev);

    memory_region_init_io(&s->mmio, OBJECT(dev), &stm32_gpio_ops, s,
                          TYPE_STM32_GPIO, STM32_GPIO_PERIPHERAL_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->mmio);

    qdev_init_gpio_in_named(DEVICE(s), stm32_gpio_irq_reset, "reset-in", 1);
    qdev_init_gpio_in_named(DEVICE(s), stm32_gpio_irq_enable, "enable-in", 1);
    qdev_init_gpio_in_named(DEVICE(s), stm32_gpio_irq_set,
                            "input-in", STM32_GPIO_NPINS);

    qdev_init_gpio_out_named(DEVICE(s), &s->state_irq, "state-out", 1);
    qdev_init_gpio_out_named(DEVICE(s), s->input_irq,
                             "input-out", STM32_GPIO_NPINS);
}

static void stm32_gpio_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_props(dc, stm32_gpio_properties);
    dc->vmsd = &vmstate_stm32_gpio;
    dc->realize = stm32_gpio_realize;
    device_class_set_legacy_reset(dc, stm32_gpio_reset);
    dc->desc = "STM32 GPIO";
}

static const TypeInfo stm32_gpio_info = {
    .name = TYPE_STM32_GPIO,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(STM32GPIOState),
    .class_init = stm32_gpio_class_init
};

static void stm32_gpio_register_types(void)
{
    type_register_static(&stm32_gpio_info);
}

type_init(stm32_gpio_register_types)
