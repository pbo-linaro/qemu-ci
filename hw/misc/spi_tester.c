/*
 * Simple SPI peripheral echo device used for SPI controller testing.
 *
 * Copyright (c) 2024 Google LLC.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "migration/vmstate.h"
#include "hw/misc/spi_tester.h"

static uint32_t spi_tester_transfer(SSIPeripheral *dev, uint32_t value)
{
    SpiTesterState *s = SPI_TESTER(dev);

    if (s->cs) {
        return 0;
    }

    return value;
}

static int spi_tester_set_cs(SSIPeripheral *dev, bool select)
{
    SpiTesterState *s = SPI_TESTER(dev);

    s->cs = select;

    return 0;
}

static const VMStateDescription vmstate_spi_tester = {
    .name = "spi-tester",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_SSI_PERIPHERAL(ssidev, SpiTesterState),
        VMSTATE_BOOL(cs, SpiTesterState),
        VMSTATE_END_OF_LIST()
    }
};

static void spi_tester_class_init(ObjectClass *klass, void *data)
{
    SSIPeripheralClass *k = SSI_PERIPHERAL_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_spi_tester;
    k->transfer    = spi_tester_transfer;
    k->set_cs      = spi_tester_set_cs;
    k->cs_polarity = SSI_CS_LOW;
}

static const TypeInfo spi_tester_types[] = {
    {
        .name          = TYPE_SPI_TESTER,
        .parent        = TYPE_SSI_PERIPHERAL,
        .instance_size = sizeof(SpiTesterState),
        .class_init    = spi_tester_class_init,
    },
};

DEFINE_TYPES(spi_tester_types);
