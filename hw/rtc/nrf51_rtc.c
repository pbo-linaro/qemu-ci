/*
 * nRF51 SoC RTC emulation
 *
 * Copyright 2025 Kaido Kert <kaidokert@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/rtc/nrf51_rtc.h"
#include "hw/irq.h"
#include "hw/registerfields.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/timer.h"
#include "qemu/units.h"
#include "system/rtc.h"
#include "system/system.h"
#include "trace.h"

#define NRF51_RTC_SIZE 0x1000
#define NRF51_RTC_TICK_HZ 32768 /* 32.768 kHz LFCLK */
#define NRF51_RTC_TICK_NS (NANOSECONDS_PER_SECOND / NRF51_RTC_TICK_HZ)

/* Register field definitions */
FIELD(NRF51_RTC_INTEN, TICK, 0, 1)
FIELD(NRF51_RTC_INTEN, OVRFLW, 1, 1)
FIELD(NRF51_RTC_INTEN, COMPARE0, 16, 1)
FIELD(NRF51_RTC_INTEN, COMPARE1, 17, 1)
FIELD(NRF51_RTC_INTEN, COMPARE2, 18, 1)
FIELD(NRF51_RTC_INTEN, COMPARE3, 19, 1)

/* 24-bit counter mask */
#define BIT24_MASK 0xFFFFFF

/* Map event address to s->events index */
static int nrf51_rtc_event_index(hwaddr addr)
{
    switch (addr) {
    case NRF51_RTC_EVENTS_TICK:
        return 0;
    case NRF51_RTC_EVENTS_OVRFLW:
        return 1;
    case NRF51_RTC_EVENTS_COMPARE0:
        return 2;
    case NRF51_RTC_EVENTS_COMPARE1:
        return 3;
    case NRF51_RTC_EVENTS_COMPARE2:
        return 4;
    case NRF51_RTC_EVENTS_COMPARE3:
        return 5;
    default:
        return -1; /* Invalid */
    }
}

static void nrf51_rtc_update_irq(NRF51RTCState *s)
{
    bool irq_pending = false;

    irq_pending |= s->events[NRF51_RTC_EVENT_TICK] &&
        FIELD_EX32(s->inten, NRF51_RTC_INTEN, TICK);
    irq_pending |= s->events[NRF51_RTC_EVENT_OVRFLW] &&
        FIELD_EX32(s->inten, NRF51_RTC_INTEN, OVRFLW);
    for (int i = 0; i < 4; i++) {
        irq_pending |= s->events[NRF51_RTC_EVENT_COMPARE + i]
        && (s->inten & BIT(16 + i));
    }

    s->irq_pending = irq_pending;
    qemu_set_irq(s->irq, s->irq_pending);
}

static void nrf51_rtc_tick(void *opaque)
{
    NRF51RTCState *s = opaque;
    uint64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    uint64_t elapsed_ns = now - s->last_update;
    uint64_t tick_period_ns = NRF51_RTC_TICK_NS * (s->prescaler + 1);
    uint32_t num_ticks = elapsed_ns / tick_period_ns;

    /* Schedule next tick before any early returns */
    timer_mod_ns(s->timer, now + tick_period_ns);

    if (!s->powered || s->last_update == 0) {
        s->last_update = now;
        /*  Skip increment on first tick */
        return;
    }

    for (uint32_t tick = 0; tick < num_ticks; tick++) {
        s->tick_count = (s->tick_count + 1) & BIT24_MASK; /* 24-bit counter */
        /*
         * Check for tick event. Note: Events are always generated
         * regardless of EVTEN register settings. EVTEN only controls
         * PPI routing of events.
         */
        s->events[NRF51_RTC_EVENT_TICK] = 1;

        /* Check for overflow (24-bit counter) */
        if (s->tick_count == 0) {
            s->events[NRF51_RTC_EVENT_OVRFLW] = 1;
        }

        /* Check compare registers */
        for (int i = 0; i < 4; i++) {
            if (s->tick_count == s->cc[i]) {
                s->events[NRF51_RTC_EVENT_COMPARE + i] = 1;
            }
        }
    }

    s->last_update += num_ticks * tick_period_ns;
    nrf51_rtc_update_irq(s);
}

static uint64_t nrf51_rtc_read(void *opaque, hwaddr addr, unsigned size)
{
    NRF51RTCState *s = opaque;
    uint64_t value = 0;

    if (!s->powered) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: read at 0x%" HWADDR_PRIx " when powered off\n",
                      __func__,
                      addr);
        return 0;
    }

    switch (addr) {
    case NRF51_RTC_EVENTS_TICK:
    case NRF51_RTC_EVENTS_OVRFLW:
    case NRF51_RTC_EVENTS_COMPARE0:
    case NRF51_RTC_EVENTS_COMPARE1:
    case NRF51_RTC_EVENTS_COMPARE2:
    case NRF51_RTC_EVENTS_COMPARE3: {
        int index = nrf51_rtc_event_index(addr);
        if (index >= 0) {
            value = s->events[index];
        }
        break;
    }
    case NRF51_RTC_INTENSET:
    case NRF51_RTC_INTENCLR:
        value = s->inten;
        break;
    case NRF51_RTC_EVTEN:
    case NRF51_RTC_EVTENSET:
    case NRF51_RTC_EVTENCLR:
        value = s->evten;
        break;
    case NRF51_RTC_COUNTER:
        value = s->tick_count & BIT24_MASK; /* 24-bit counter */
        break;
    case NRF51_RTC_PRESCALER:
        value = s->prescaler & 0xFFF; /* 12-bit prescaler */
        break;
    case NRF51_RTC_CC0:
    case NRF51_RTC_CC1:
    case NRF51_RTC_CC2:
    case NRF51_RTC_CC3:
        value = s->cc[(addr - NRF51_RTC_CC0) / 4];
        break;
    case NRF51_RTC_POWER:
        value = s->powered ? 1 : 0;
        break;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "%s: unimplemented read at 0x%" HWADDR_PRIx "\n",
                      __func__,
                      addr);
        break;
    }

    trace_nrf51_rtc_read(addr, value);
    return value;
}

static void nrf51_rtc_write(void *opaque,
                            hwaddr addr,
                            uint64_t value,
                            unsigned size) {
    NRF51RTCState *s = opaque;

    if (!s->powered && addr != NRF51_RTC_POWER) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: write at 0x%" HWADDR_PRIx " when powered off\n",
                      __func__,
                      addr);
        return;
    }

    switch (addr) {
    case NRF51_RTC_TASKS_START:
        if (value == 1) {
            s->running = true;
            s->last_update = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
            timer_mod_ns(s->timer,
                         s->last_update +
                             NRF51_RTC_TICK_NS * (s->prescaler + 1));
        }
        break;
    case NRF51_RTC_TASKS_STOP:
        if (value == 1) {
            s->running = false;
            timer_del(s->timer);
        }
        break;
    case NRF51_RTC_TASKS_CLEAR:
        if (value == 1) {
            s->tick_count = 0;
            memset(s->events, 0, sizeof(s->events)); /* Clear all events */
            s->last_update = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
            nrf51_rtc_update_irq(s);
        }
        break;
    case NRF51_RTC_TASKS_TRIGOVRFLW:
        if (value == 1) {
            s->tick_count = 0xFFFFFE; /* Trigger overflow on next tick */
        }
        break;
    case NRF51_RTC_EVENTS_TICK:
    case NRF51_RTC_EVENTS_OVRFLW:
    case NRF51_RTC_EVENTS_COMPARE0:
    case NRF51_RTC_EVENTS_COMPARE1:
    case NRF51_RTC_EVENTS_COMPARE2:
    case NRF51_RTC_EVENTS_COMPARE3: {
        int index = nrf51_rtc_event_index(addr);
        if (index >= 0) {
            s->events[index] = value & 1;
            nrf51_rtc_update_irq(s);
        }
        break;
    }
    case NRF51_RTC_INTENSET:
        s->inten |= value;
        nrf51_rtc_update_irq(s);
        break;
    case NRF51_RTC_INTENCLR:
        s->inten &= ~value;
        nrf51_rtc_update_irq(s);
        break;
    case NRF51_RTC_EVTENSET:
        s->evten |= value;
        break;
    case NRF51_RTC_EVTENCLR:
        s->evten &= ~value;
        break;
    case NRF51_RTC_PRESCALER:
        if (s->running) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: PRESCALER write while RTC running\n",
                          __func__);
        } else {
            s->prescaler = value & 0xFFF; /* 12-bit prescaler */
        }
        break;
    case NRF51_RTC_CC0:
    case NRF51_RTC_CC1:
    case NRF51_RTC_CC2:
    case NRF51_RTC_CC3: {
        int index = (addr - NRF51_RTC_CC0) / 4;
        s->cc[index] = value & BIT24_MASK;
         /* Writing to a CC register clears its associated COMPARE event */
        s->events[NRF51_RTC_EVENT_COMPARE + index] = 0;
        nrf51_rtc_update_irq(s);
        break;
    }
    case NRF51_RTC_POWER:
        s->powered = (value & 1) != 0;
        if (!s->powered) {
            s->running = false;
            timer_del(s->timer);
            s->tick_count = 0;
            s->last_update = 0;
        }
        break;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "%s: unimplemented write at 0x%" HWADDR_PRIx "\n",
                      __func__,
                      addr);
        break;
    }

    trace_nrf51_rtc_write(addr, value);
}

static const MemoryRegionOps nrf51_rtc_ops = {
    .read = nrf51_rtc_read,
    .write = nrf51_rtc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void nrf51_rtc_reset(DeviceState *dev)
{
    NRF51RTCState *s = NRF51_RTC(dev);

    timer_del(s->timer);
    s->tick_count = 0;
    s->prescaler = 0;
    s->running = false;
    s->powered = false; /* Reset to disabled per POWER register */
    s->irq_pending = false;
    s->inten = 0;
    s->evten = 0;
    s->last_update = 0;
    memset(s->cc, 0, sizeof(s->cc));
    memset(s->events, 0, sizeof(s->events));
}

static void nrf51_rtc_realize(DeviceState *dev, Error **errp)
{
    NRF51RTCState *s = NRF51_RTC(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    memory_region_init_io(&s->iomem,
                          OBJECT(s),
                          &nrf51_rtc_ops,
                          s,
                          TYPE_NRF51_RTC,
                          NRF51_RTC_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);

    s->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, nrf51_rtc_tick, s);
}

static const VMStateDescription nrf51_rtc_vmstate = {
    .name = TYPE_NRF51_RTC,
    .version_id = 1,
    .fields = (const VMStateField[]){VMSTATE_UINT32(tick_count, NRF51RTCState),
                                     VMSTATE_UINT64(last_update, NRF51RTCState),
                                     VMSTATE_UINT32(prescaler, NRF51RTCState),
                                     VMSTATE_BOOL(running, NRF51RTCState),
                                     VMSTATE_BOOL(powered, NRF51RTCState),
                                     VMSTATE_BOOL(irq_pending, NRF51RTCState),
                                     VMSTATE_UINT32(inten, NRF51RTCState),
                                     VMSTATE_UINT32(evten, NRF51RTCState),
                                     VMSTATE_UINT32_ARRAY(cc, NRF51RTCState, 4),
                                     VMSTATE_UINT32_ARRAY(events,
                                                          NRF51RTCState,
                                                          6),
                                     VMSTATE_END_OF_LIST()},
};

static void nrf51_rtc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = nrf51_rtc_realize;
    device_class_set_legacy_reset(dc, nrf51_rtc_reset);
    dc->vmsd = &nrf51_rtc_vmstate;
}

static const TypeInfo nrf51_rtc_info = {
    .name = TYPE_NRF51_RTC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NRF51RTCState),
    .class_init = nrf51_rtc_class_init,
};

static void nrf51_rtc_register_types(void)
{
    type_register_static(&nrf51_rtc_info);
}

type_init(nrf51_rtc_register_types)
