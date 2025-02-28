/*
 * Intel Resource Director Technology (RDT).
 *
 * Copyright 2025 Google LLC
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#include "hw/i386/rdt.h"
#include "qemu/osdep.h" /* Needs to be included before isa.h */
#include "hw/isa/isa.h"
#include "hw/qdev-properties.h"
#include "qom/object.h"

#define TYPE_RDT "rdt"
#define RDT_NUM_RMID_PROP "rmids"

OBJECT_DECLARE_TYPE(RDTState, RDTStateClass, RDT);

struct RDTMonitor {
    uint64_t count_local;
    uint64_t count_remote;
    uint64_t count_l3;
};

struct RDTAllocation {
    QemuMutex lock;
    uint32_t active_cos;
};

struct RDTStatePerCore {
    QemuMutex lock;
    uint32_t active_rmid;
};

struct RDTStatePerL3Cache {
    QemuMutex lock;

    RDTMonitor *monitors;

    /* RDT Allocation bitmask MSRs */
    uint32_t msr_L3_ia32_mask_n[RDT_MAX_L3_MASK_COUNT];
    uint32_t msr_L2_ia32_mask_n[RDT_MAX_L2_MASK_COUNT];
    uint32_t ia32_L2_qos_ext_bw_thrtl_n[RDT_MAX_MBA_THRTL_COUNT];

    /* Parent RDTState */
    RDTState *rdtstate;
};

/* One instance of RDT-internal state to be shared by all cores */
struct RDTState {
    ISADevice parent;

    /* Max amount of RMIDs */
    uint32_t rmids;

    uint16_t l3_caches;

    RDTStatePerL3Cache *rdtInstances;
    RDTAllocation *allocations;
};

struct RDTStateClass {
};

OBJECT_DEFINE_TYPE(RDTState, rdt, RDT, ISA_DEVICE);

static Property rdt_properties[] = {
    DEFINE_PROP_UINT32(RDT_NUM_RMID_PROP, RDTState, rmids, 256),
};

static void rdt_init(Object *obj)
{
}

static void rdt_finalize(Object *obj)
{
}

static void rdt_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->hotpluggable = false;
    dc->desc = "RDT";
    dc->user_creatable = true;

    device_class_set_props(dc, rdt_properties);
}
