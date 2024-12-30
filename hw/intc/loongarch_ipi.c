/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * LoongArch IPI interrupt support
 *
 * Copyright (C) 2024 Loongson Technology Corporation Limited
 */

#include "qemu/osdep.h"
#include "hw/boards.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "hw/intc/loongarch_ipi.h"
#include "target/loongarch/cpu.h"

static AddressSpace *get_iocsr_as(CPUState *cpu)
{
    return LOONGARCH_CPU(cpu)->env.address_space_iocsr;
}

static int loongarch_cpu_by_arch_id(LoongsonIPICommonState *lics,
                                    int64_t arch_id, int *index, CPUState **pcs)
{
    LoongarchIPIState *lis = LOONGARCH_IPI(lics);

    if ((arch_id >= MAX_PHY_ID) || (lis->devs[arch_id].cs == NULL)) {
        return MEMTX_ERROR;
    }

    if (index) {
        *index = lis->devs[arch_id].index;
    }

    if (pcs) {
        *pcs = lis->devs[arch_id].cs;
    }

    return MEMTX_OK;
}

static void loongarch_cpu_plug(HotplugHandler *hotplug_dev,
                               DeviceState *dev, Error **errp)
{
    LoongarchIPIState *lis = LOONGARCH_IPI(hotplug_dev);
    Object *obj = OBJECT(dev);
    LoongArchCPU *cpu;
    int phy_id, index;

    if (!object_dynamic_cast(obj, TYPE_LOONGARCH_CPU)) {
        warn_report("LoongArch IPI: Invalid %s device type",
                                       object_get_typename(obj));
        return;
    }

    cpu = LOONGARCH_CPU(dev);
    phy_id = cpu->phy_id;
    if ((phy_id >= MAX_PHY_ID) || (phy_id < 0)) {
        warn_report("LoongArch IPI: Invalid phy id %d", phy_id);
        return;
    }

    if (lis->devs[phy_id].index >= 0) {
        warn_report("LoongArch IPI: phy id %d is added already", phy_id);
        return;
    }

    index = find_first_zero_bit(lis->present_map, LOONGARCH_MAX_CPUS);
    if (index == LOONGARCH_MAX_CPUS) {
        error_setg(errp, "no free cpu slots available");
        return;
    }

    /* connect ipi irq to cpu irq */
    set_bit(index, lis->present_map);
    lis->devs[phy_id].index = index;
    lis->devs[phy_id].cs = CPU(dev);
    qdev_connect_gpio_out(DEVICE(lis), index, qdev_get_gpio_in(dev, IRQ_IPI));
}

static void loongarch_cpu_unplug(HotplugHandler *hotplug_dev,
                                 DeviceState *dev, Error **errp)
{
    LoongarchIPIState *lis = LOONGARCH_IPI(hotplug_dev);
    Object *obj = OBJECT(dev);
    LoongArchCPU *cpu;
    int phy_id;

    if (!object_dynamic_cast(obj, TYPE_LOONGARCH_CPU)) {
        warn_report("LoongArch IPI: Invalid %s device type",
                                       object_get_typename(obj));
        return;
    }

    cpu = LOONGARCH_CPU(dev);
    phy_id = cpu->phy_id;
    if ((phy_id >= MAX_PHY_ID) || (phy_id < 0)) {
        warn_report("LoongArch IPI: Invalid phy id %d", phy_id);
        return;
    }

    if (lis->devs[phy_id].index < 0) {
        warn_report("LoongArch IPI: phy id %d is not added", phy_id);
        return;
    }

    clear_bit(lis->devs[phy_id].index, lis->present_map);
    lis->devs[phy_id].index = INVALID_CPU;
    lis->devs[phy_id].cs = NULL;
}

static void loongarch_ipi_realize(DeviceState *dev, Error **errp)
{
    LoongarchIPIState *lis = LOONGARCH_IPI(dev);
    LoongarchIPIClass *lic = LOONGARCH_IPI_GET_CLASS(dev);
    Error *local_err = NULL;
    int i;

    lic->parent_realize(dev, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    for (i = 0; i < MAX_PHY_ID; i++) {
        lis->devs[i].index = INVALID_CPU;
    }
}

static void loongarch_ipi_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    LoongsonIPICommonClass *licc = LOONGSON_IPI_COMMON_CLASS(klass);
    LoongarchIPIClass *lic = LOONGARCH_IPI_CLASS(klass);
    HotplugHandlerClass *hc = HOTPLUG_HANDLER_CLASS(klass);

    device_class_set_parent_realize(dc, loongarch_ipi_realize,
                                    &lic->parent_realize);
    licc->get_iocsr_as = get_iocsr_as;
    licc->cpu_by_arch_id = loongarch_cpu_by_arch_id;
    hc->plug = loongarch_cpu_plug;
    hc->unplug = loongarch_cpu_unplug;
}

static const TypeInfo loongarch_ipi_types[] = {
    {
        .name               = TYPE_LOONGARCH_IPI,
        .parent             = TYPE_LOONGSON_IPI_COMMON,
        .instance_size      = sizeof(LoongarchIPIState),
        .class_init         = loongarch_ipi_class_init,
        .interfaces         = (InterfaceInfo[]) {
            { TYPE_HOTPLUG_HANDLER },
            { }
        }
    }
};

DEFINE_TYPES(loongarch_ipi_types)
