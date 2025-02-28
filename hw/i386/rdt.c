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
#include "include/hw/boards.h"
#include "qom/object.h"
#include "target/i386/cpu.h"

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

static inline int16_t cache_ids_contain(uint32_t current_ids[],
                                        uint16_t size, uint32_t id) {
    for (int i = 0; i < size; i++) {
        if (current_ids[i] == id) {
            return i;
        }
    }
    return -1;
}

OBJECT_DEFINE_TYPE(RDTState, rdt, RDT, ISA_DEVICE);

static Property rdt_properties[] = {
    DEFINE_PROP_UINT32(RDT_NUM_RMID_PROP, RDTState, rmids, 256),
};

static void rdt_init(Object *obj)
{
}

static void rdt_realize(DeviceState *dev, Error **errp) {
    RDTState *rdtDev = RDT(dev);
    MachineState *ms = MACHINE(qdev_get_machine());
    rdtDev->rdtInstances = NULL;
    rdtDev->l3_caches = 0;
    uint32_t *cache_ids_found = g_malloc(sizeof(uint32_t) * 256);
    uint32_t cache_ids_size = 0;

    /* Iterate over all CPUs and set RDT state */
    for (int i = 0; i < ms->possible_cpus->len; i++) {
        X86CPU *x86_cpu = X86_CPU(ms->possible_cpus->cpus[i].cpu);
        X86CPUTopoInfo topo_info = x86_cpu->env.topo_info;

        uint32_t num_threads_sharing = apicid_pkg_offset(&topo_info);
        uint32_t index_msb = 32 - clz32(num_threads_sharing);
        uint32_t l3_id = x86_cpu->apic_id & ~((1 << index_msb) - 1);

        int16_t pos = cache_ids_contain(cache_ids_found,
                                        cache_ids_size, l3_id);
        /*
         * If we find a core that shares a new L3 cache,
         * initialize the relevant per-L3 state.
         * */
        if (pos == -1) {
            cache_ids_size++;
            pos = cache_ids_size - 1;
            cache_ids_found[pos] = l3_id;

            rdtDev->rdtInstances = g_realloc(rdtDev->rdtInstances,
                                             sizeof(RDTStatePerL3Cache) *
                                             cache_ids_size);
            rdtDev->l3_caches++;
            RDTStatePerL3Cache *rdt = &rdtDev->rdtInstances[pos];
            rdt->rdtstate = rdtDev;
            rdt->monitors = g_malloc(sizeof(RDTMonitor) * rdtDev->rmids);
            rdt->rdtstate->allocations = g_malloc(sizeof(RDTAllocation) *
                                                  rdtDev->rmids);
            rdt->monitors->count_local = 0;
            rdt->monitors->count_remote = 0;
            rdt->monitors->count_l3 = 0;
            memset(rdt->msr_L2_ia32_mask_n, 0xF,
                   sizeof(rdt->msr_L2_ia32_mask_n));
            memset(rdt->msr_L3_ia32_mask_n, 0xF,
                   sizeof(rdt->msr_L3_ia32_mask_n));
            memset(rdt->ia32_L2_qos_ext_bw_thrtl_n, 0xF,
                   sizeof(rdt->ia32_L2_qos_ext_bw_thrtl_n));
            qemu_mutex_init(&rdt->rdtstate->allocations->lock);
            qemu_mutex_init(&rdt->lock);
        }

        x86_cpu->rdtStatePerL3Cache = &rdtDev->rdtInstances[pos];
        x86_cpu->rdtPerCore = g_malloc(sizeof(RDTStatePerCore));

        qemu_mutex_init(&x86_cpu->rdtPerCore->lock);
    }
}

static void rdt_finalize(Object *obj)
{
    RDTState *rdt = RDT(obj);
    MachineState *ms = MACHINE(qdev_get_machine());

    for (int i = 0; i < ms->possible_cpus->len; i++) {
        RDTStatePerL3Cache *rdtInstance = &rdt->rdtInstances[i];
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
