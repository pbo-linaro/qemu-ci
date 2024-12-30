/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * LoongArch IPI interrupt support
 *
 * Copyright (C) 2024 Loongson Technology Corporation Limited
 */

#include "qemu/osdep.h"
#include "hw/boards.h"
#include "qemu/error-report.h"
#include "hw/intc/loongarch_ipi.h"
#include "target/loongarch/cpu.h"

static AddressSpace *get_iocsr_as(CPUState *cpu)
{
    return LOONGARCH_CPU(cpu)->env.address_space_iocsr;
}

static int archid_cmp(const void *a, const void *b)
{
   CPUArchId *archid_a = (CPUArchId *)a;
   CPUArchId *archid_b = (CPUArchId *)b;

   return archid_a->arch_id - archid_b->arch_id;
}

static CPUArchId *find_cpu_by_archid(MachineState *ms, uint32_t id)
{
    CPUArchId apic_id, *found_cpu;

    apic_id.arch_id = id;
    found_cpu = bsearch(&apic_id, ms->possible_cpus->cpus,
                        ms->possible_cpus->len,
                        sizeof(*ms->possible_cpus->cpus),
                        archid_cmp);

    return found_cpu;
}

static int loongarch_cpu_by_arch_id(LoongsonIPICommonState *lics,
                                    int64_t arch_id, int *index, CPUState **pcs)
{
    MachineState *machine = MACHINE(qdev_get_machine());
    CPUArchId *archid;
    CPUState *cs;

    archid = find_cpu_by_archid(machine, arch_id);
    if (archid && archid->cpu) {
        cs = archid->cpu;
        if (index) {
            *index = cs->cpu_index;
        }

        if (pcs) {
            *pcs = cs;
        }

        return MEMTX_OK;
    }

    return MEMTX_ERROR;
}

static void loongarch_cpu_plug(HotplugHandler *hotplug_dev,
                               DeviceState *dev, Error **errp)
{
    Object *obj = OBJECT(dev);

    if (!object_dynamic_cast(obj, TYPE_LOONGARCH_CPU)) {
        warn_report("LoongArch IPI: Invalid %s device type",
                                       object_get_typename(obj));
        return;
    }
}

static void loongarch_cpu_unplug(HotplugHandler *hotplug_dev,
                                 DeviceState *dev, Error **errp)
{
    Object *obj = OBJECT(dev);

    if (!object_dynamic_cast(obj, TYPE_LOONGARCH_CPU)) {
        warn_report("LoongArch IPI: Invalid %s device type",
                                       object_get_typename(obj));
        return;
    }
}

static void loongarch_ipi_class_init(ObjectClass *klass, void *data)
{
    LoongsonIPICommonClass *licc = LOONGSON_IPI_COMMON_CLASS(klass);
    HotplugHandlerClass *hc = HOTPLUG_HANDLER_CLASS(klass);

    licc->get_iocsr_as = get_iocsr_as;
    licc->cpu_by_arch_id = loongarch_cpu_by_arch_id;
    hc->plug = loongarch_cpu_plug;
    hc->unplug = loongarch_cpu_unplug;
}

static const TypeInfo loongarch_ipi_types[] = {
    {
        .name               = TYPE_LOONGARCH_IPI,
        .parent             = TYPE_LOONGSON_IPI_COMMON,
        .class_init         = loongarch_ipi_class_init,
        .interfaces         = (InterfaceInfo[]) {
            { TYPE_HOTPLUG_HANDLER },
            { }
        }
    }
};

DEFINE_TYPES(loongarch_ipi_types)
