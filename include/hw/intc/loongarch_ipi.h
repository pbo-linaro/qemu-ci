/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * LoongArch IPI interrupt header files
 *
 * Copyright (C) 2024 Loongson Technology Corporation Limited
 */

#ifndef HW_LOONGARCH_IPI_H
#define HW_LOONGARCH_IPI_H

#include "qom/object.h"
#include "hw/intc/loongson_ipi_common.h"
#include "hw/loongarch/virt.h"

#define INVALID_CPU         -1
#define TYPE_LOONGARCH_IPI  "loongarch_ipi"
OBJECT_DECLARE_TYPE(LoongarchIPIState, LoongarchIPIClass, LOONGARCH_IPI)

typedef struct IPICPUState {
    CPUState *cs;
    int index;
} IPICPUState;

struct LoongarchIPIState {
    LoongsonIPICommonState parent_obj;
    DECLARE_BITMAP(present_map, LOONGARCH_MAX_CPUS);
    IPICPUState devs[MAX_PHY_ID];
};

struct LoongarchIPIClass {
    LoongsonIPICommonClass parent_class;
    DeviceRealize parent_realize;
};

#endif
