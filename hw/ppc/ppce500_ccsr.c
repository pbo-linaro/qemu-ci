/*
 * QEMU PowerPC E500 embedded processors CCSR space emulation
 *
 * Copyright (C) 2009 Freescale Semiconductor, Inc. All rights reserved.
 *
 * Author: Yu Liu,     <yu.liu@freescale.com>
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of  the GNU General  Public License as published by
 * the Free Software Foundation;  either version 2 of the  License, or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "ppce500_ccsr.h"

static void ppce500_ccsr_init(Object *obj)
{
    PPCE500CCSRState *s = CCSR(obj);

    memory_region_init(&s->ccsr_space, obj, "e500-ccsr", MPC85XX_CCSRBAR_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->ccsr_space);
}

static const TypeInfo ppce500_ccsr_types[] = {
    {
        .name          = TYPE_CCSR,
        .parent        = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(PPCE500CCSRState),
        .instance_init = ppce500_ccsr_init,
    },
};

DEFINE_TYPES(ppce500_ccsr_types)
