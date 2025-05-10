/*
 * nRF51 SoC RTC emulation
 *
 * Copyright 2025 Kaido Kert <kaidokert@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_RTC_NRF51_RTC_H
#define HW_RTC_NRF51_RTC_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_NRF51_RTC "nrf51_soc.rtc"
OBJECT_DECLARE_SIMPLE_TYPE(NRF51RTCState, NRF51_RTC)

/* Register offsets */
#define NRF51_RTC_TASKS_START 0x000
#define NRF51_RTC_TASKS_STOP 0x004
#define NRF51_RTC_TASKS_CLEAR 0x008
#define NRF51_RTC_TASKS_TRIGOVRFLW 0x00C
#define NRF51_RTC_EVENTS_TICK 0x100
#define NRF51_RTC_EVENTS_OVRFLW 0x104
#define NRF51_RTC_EVENTS_COMPARE0 0x140
#define NRF51_RTC_EVENTS_COMPARE1 0x144
#define NRF51_RTC_EVENTS_COMPARE2 0x148
#define NRF51_RTC_EVENTS_COMPARE3 0x14C
#define NRF51_RTC_INTENSET 0x304
#define NRF51_RTC_INTENCLR 0x308
#define NRF51_RTC_EVTEN 0x340
#define NRF51_RTC_EVTENSET 0x344
#define NRF51_RTC_EVTENCLR 0x348
#define NRF51_RTC_COUNTER 0x504
#define NRF51_RTC_PRESCALER 0x508
#define NRF51_RTC_CC0 0x540
#define NRF51_RTC_CC1 0x544
#define NRF51_RTC_CC2 0x548
#define NRF51_RTC_CC3 0x54C
#define NRF51_RTC_POWER 0xFFC

#define NRF51_RTC_CC_REG_COUNT 4
#define NRF51_RTC_EVENT_COUNT 6

#define NRF51_RTC_EVENT_TICK 0
#define NRF51_RTC_EVENT_OVRFLW 1
#define NRF51_RTC_EVENT_COMPARE 2

struct NRF51RTCState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    QEMUTimer *timer;
    qemu_irq irq;

    uint32_t tick_count;  /* Current counter value, 24-bit */
    uint64_t last_update; /* Last tick update time (ns) */
    uint32_t prescaler;   /* 12-bit prescaler */
    bool running;         /* RTC running state */
    bool powered;         /* RTC power state (POWER register) */
    bool irq_pending;     /* Pending interrupts */
    uint32_t inten;       /* Interrupt enable */
    uint32_t evten;       /* Event enable */
    uint32_t cc[NRF51_RTC_CC_REG_COUNT];    /* Compare registers CC[0-3] */
    uint32_t events[NRF51_RTC_EVENT_COUNT]; /* TICK, OVRFLW, COMPARE[0-3] */
};

#endif /* HW_RTC_NRF51_RTC_H */
