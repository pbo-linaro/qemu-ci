/*
 * Simple SPI peripheral device used for SPI controller testing.
 *
 * Copyright (c) 2024 Google LLC.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_SPI_TESTER_H
#define HW_SPI_TESTER_H

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/bswap.h"
#include "hw/irq.h"
#include "hw/ssi/ssi.h"
#include "qemu/timer.h"
#include "hw/qdev-properties.h"

#define TYPE_SPI_TESTER "spi-tester"
#define SPI_TESTER(obj) OBJECT_CHECK(SpiTesterState, (obj), TYPE_SPI_TESTER)

typedef struct {
    SSIPeripheral ssidev;
    bool cs;
} SpiTesterState;

#endif /* HW_SPI_TESTER_H */
