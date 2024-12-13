/*
 * Intel Resource Director Technology (RDT).
 *
 * Copyright 2024 Google LLC
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
#include "target/i386/cpu.h"

/* RDT Monitoring Event Codes */
#define RDT_EVENT_L3_OCCUPANCY 1
#define RDT_EVENT_L3_REMOTE_BW 2
#define RDT_EVENT_L3_LOCAL_BW 3

/* Max counts for allocation masks or CBMs. In other words, the size of
 * respective MSRs.
 * L3_MASK and L3_mask are architectural limitations. THRTL_COUNT is just
 * the space left until the next MSR.
 * */
#define RDT_MAX_L3_MASK_COUNT      127
#define RDT_MAX_L2_MASK_COUNT      63
#define RDT_MAX_MBA_THRTL_COUNT    63

#define TYPE_RDT "rdt"
#define RDT_NUM_RMID_PROP "rmids"

#define QM_CTR_ERROR        (1ULL << 63)
#define QM_CTR_UNAVAILABLE  (1ULL << 62)

OBJECT_DECLARE_TYPE(RDTState, RDTStateClass, RDT);

struct RDTMonitor {
    uint64_t count_local;
    uint64_t count_remote;
    uint64_t count_l3;
};

struct RDTAllocation {
    uint32_t active_cos;
};

struct RDTStatePerCore {
    uint32_t active_rmid;
    GArray *monitors;

    /*Parent RDTState*/
    RDTState *rdtstate;
};

/*One instance of RDT-internal state to be shared by all cores*/
struct RDTState {
    ISADevice parent;

    /*Max amount of RMIDs*/
    uint32_t rmids;

    /*Per core state*/
    RDTStatePerCore *rdtInstances;
    RDTAllocation *allocations;

    /*RDT Allocation bitmask MSRs*/
    uint32_t msr_L3_ia32_mask_n[RDT_MAX_L3_MASK_COUNT];
    uint32_t msr_L2_ia32_mask_n[RDT_MAX_L2_MASK_COUNT];
    uint32_t ia32_L2_qos_ext_bw_thrtl_n[RDT_MAX_MBA_THRTL_COUNT];
};

struct RDTStateClass {
};

bool rdt_associate_rmid_cos(uint64_t msr_ia32_pqr_assoc)
{
    X86CPU *cpu = X86_CPU(current_cpu);
    RDTStatePerCore *rdt = cpu->rdt;
    RDTAllocation *alloc;

    uint32_t cos_id = (msr_ia32_pqr_assoc & 0xffff0000) >> 16;
    uint32_t rmid = msr_ia32_pqr_assoc & 0xffff;

    if (cos_id > RDT_MAX_L3_MASK_COUNT || cos_id > RDT_MAX_L2_MASK_COUNT ||
        cos_id > RDT_MAX_MBA_THRTL_COUNT || rmid > rdt_max_rmid(rdt)) {
        return false;
    }

    rdt->active_rmid = rmid;

    alloc = &rdt->rdtstate->allocations[rmid];

    alloc->active_cos = cos_id;

    return true;
}

uint32_t rdt_read_l3_mask(uint32_t pos)
{
    X86CPU *cpu = X86_CPU(current_cpu);
    RDTStatePerCore *rdt = cpu->rdt;

    return rdt->rdtstate->msr_L3_ia32_mask_n[pos];
}

uint32_t rdt_read_l2_mask(uint32_t pos)
{
    X86CPU *cpu = X86_CPU(current_cpu);
    RDTStatePerCore *rdt = cpu->rdt;

    return rdt->rdtstate->msr_L2_ia32_mask_n[pos];
}

uint32_t rdt_read_mba_thrtl(uint32_t pos)
{
    X86CPU *cpu = X86_CPU(current_cpu);
    RDTStatePerCore *rdt = cpu->rdt;

    return rdt->rdtstate->ia32_L2_qos_ext_bw_thrtl_n[pos];
}

void rdt_write_msr_l3_mask(uint32_t pos, uint32_t val)
{
    X86CPU *cpu = X86_CPU(current_cpu);
    RDTStatePerCore *rdt = cpu->rdt;

    rdt->rdtstate->msr_L3_ia32_mask_n[pos] = val;
}

void rdt_write_msr_l2_mask(uint32_t pos, uint32_t val)
{
    X86CPU *cpu = X86_CPU(current_cpu);
    RDTStatePerCore *rdt = cpu->rdt;

    rdt->rdtstate->msr_L2_ia32_mask_n[pos] = val;
}

void rdt_write_mba_thrtl(uint32_t pos, uint32_t val)
{
    X86CPU *cpu = X86_CPU(current_cpu);
    RDTStatePerCore *rdt = cpu->rdt;

    rdt->rdtstate->ia32_L2_qos_ext_bw_thrtl_n[pos] = val;
}

uint32_t rdt_max_rmid(RDTStatePerCore *rdt)
{
    RDTState *rdtdev = rdt->rdtstate;
    return rdtdev->rmids - 1;
}

uint64_t rdt_read_event_count(RDTStatePerCore *rdtInstance,
                              uint32_t rmid, uint32_t event_id)
{
    CPUState *cs;
    RDTMonitor *mon;
    RDTState *rdt = rdtInstance->rdtstate;

    uint32_t count_l3 = 0;
    uint32_t count_local = 0;
    uint32_t count_remote = 0;

    if (!rdt) {
        return 0;
    }

    CPU_FOREACH(cs) {
        rdtInstance = &rdt->rdtInstances[cs->cpu_index];
        if (rmid >= rdtInstance->monitors->len) {
            return QM_CTR_ERROR;
        }
        mon = &g_array_index(rdtInstance->monitors, RDTMonitor, rmid);
        count_l3 += mon->count_l3;
        count_local += mon->count_local;
        count_remote += mon->count_remote;
    }

    switch (event_id) {
    case RDT_EVENT_L3_OCCUPANCY:
        return count_l3 == 0 ? QM_CTR_UNAVAILABLE : count_l3;
    case RDT_EVENT_L3_REMOTE_BW:
        return count_remote == 0 ? QM_CTR_UNAVAILABLE : count_remote;
    case RDT_EVENT_L3_LOCAL_BW:
        return count_local == 0 ? QM_CTR_UNAVAILABLE : count_local;
    default:
        return QM_CTR_ERROR;
    }
}

OBJECT_DEFINE_TYPE(RDTState, rdt, RDT, ISA_DEVICE);

static Property rdt_properties[] = {
    DEFINE_PROP_UINT32(RDT_NUM_RMID_PROP, RDTState, rmids, 256),
    DEFINE_PROP_END_OF_LIST(),
};

static void rdt_init(Object *obj)
{
}

static void rdt_realize(DeviceState *dev, Error **errp)
{
    CPUState *cs = first_cpu;
    RDTState *rdtDev = RDT(dev);

    rdtDev->rdtInstances = g_malloc(sizeof(RDTStatePerCore) * cs->nr_cores);
    CPU_FOREACH(cs) {
        RDTStatePerCore *rdt = &rdtDev->rdtInstances[cs->cpu_index];
        X86CPU *cpu = X86_CPU(cs);

        rdt->rdtstate = rdtDev;
        cpu->rdt = rdt;

        rdt->monitors = g_malloc(sizeof(RDTMonitor) * rdtDev->rmids);
        rdt->rdtstate->allocations = g_malloc(sizeof(RDTAllocation) * rdtDev->rmids);
    }
}

static void rdt_finalize(Object *obj)
{
    CPUState *cs;
    RDTState *rdt = RDT(obj);

    CPU_FOREACH(cs) {
        RDTStatePerCore *rdtInstance = &rdt->rdtInstances[cs->cpu_index];
        g_free(rdtInstance->monitors);
        g_free(rdtInstance->rdtstate->allocations);
    }

    g_free(rdt->rdtInstances);
}

static void rdt_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->hotpluggable = false;
    dc->desc = "RDT";
    dc->user_creatable = true;
    dc->realize = rdt_realize;

    device_class_set_props(dc, rdt_properties);
}
