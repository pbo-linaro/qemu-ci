/*
 * QEMU model for NXP's FLEXCOMM
 *
 * Copyright (c) 2024 Google LLC
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_FLEXCOMM_FUNCTION_H
#define HW_FLEXCOMM_FUNCTION_H

#include "qemu/fifo32.h"
#include "hw/sysbus.h"

#define TYPE_FLEXCOMM_FUNCTION "flexcomm-function"
OBJECT_DECLARE_TYPE(FlexcommFunction, FlexcommFunctionClass, FLEXCOMM_FUNCTION);

struct FlexcommFunction {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    uint32_t *regs;
};

typedef void (*FlexcommFunctionSelect)(FlexcommFunction *f, bool selected);

struct FlexcommFunctionClass {
    SysBusDeviceClass parent_class;

    const MemoryRegionOps *mmio_ops;
    const char *name;
    FlexcommFunctionSelect select;
};

static inline void flexcomm_select(FlexcommFunction *obj, bool selected)
{
    FlexcommFunctionClass *klass = FLEXCOMM_FUNCTION_GET_CLASS(obj);

    klass->select(obj, selected);
}

void flexcomm_set_irq(FlexcommFunction *f, bool irq);

#endif /* HW_FLEXCOMM_FUNCTION_H */
