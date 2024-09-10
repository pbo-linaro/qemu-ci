/*
 * OpenSBI Domains
 *
 * Copyright (c) 2024 Gregor Haas
 *
 * Generates OpenSBI domain nodes in the machine's device tree
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/riscv/opensbi_domain.h"
#include "hw/boards.h"
#include "hw/riscv/virt.h"
#include "qapi/error.h"
#include "qemu/cutils.h"
#include "qemu/error-report.h"
#include "sysemu/device_tree.h"

#include <libfdt.h>

static void create_fdt_domain_possible_harts(MachineState *ms,
                                             OpenSBIDomainState *s,
                                             char *path) {
    unsigned long i, cpu;
    unsigned long num_cpus;

    num_cpus = s->last_possible_hart - s->first_possible_hart + 1;
    if (num_cpus) {
        g_autofree uint32_t *phandles = g_malloc0_n(num_cpus, sizeof(uint32_t));

        for (i = 0, cpu = s->first_possible_hart; i < num_cpus; i++, cpu++) {
            g_autofree char *cpu_name = g_strdup_printf("/cpus/cpu@%li", cpu);
            phandles[i] = cpu_to_fdt32(qemu_fdt_get_phandle(
                    ms->fdt, cpu_name));
        }

        qemu_fdt_setprop(ms->fdt, path, "possible-harts",
                         phandles, num_cpus * 4);
    }
}

static void create_fdt_domain_regions(MachineState *ms,
                                      OpenSBIDomainState *s,
                                      char *path) {
    unsigned long i;
    int num_regions = 0;
    DeviceState *ds;

    for (i = 0; i < OPENSBI_DOMAIN_MEMREGIONS_MAX; i++) {
        if (s->regions[i]) {
            num_regions++;
        }
    }

    if (num_regions) {
        g_autofree uint32_t *regions =
                 g_malloc0_n(num_regions, 2 * sizeof(uint32_t));
        for (i = 0; i < OPENSBI_DOMAIN_MEMREGIONS_MAX; i++) {
            if (s->regions[i]) {
                ds = DEVICE(s->regions[i]);
                g_autofree char *region_name = g_strdup_printf(
                       "/chosen/opensbi-domains/%s", ds->id);
                regions[2 * i] = cpu_to_fdt32(qemu_fdt_get_phandle
                        (ms->fdt, region_name));
                regions[2 * i + 1] = cpu_to_fdt32(s->region_perms[i]);
            }
        }

        qemu_fdt_setprop(ms->fdt, path, "regions",
                         regions, num_regions * 8);
    }
}

struct DomainFDTState {
    MachineState *ms;
    bool regions;
};

static void create_fdt_one_domain(MachineState *ms, OpenSBIDomainState *s)
{
    DeviceState *ds = DEVICE(s);
    g_autofree char *path = g_strdup_printf("/chosen/opensbi-domains/%s", ds->id);

    qemu_fdt_add_subnode(ms->fdt, path);
    qemu_fdt_setprop_string(ms->fdt, path, "compatible",
                            "opensbi,domain,instance");
    qemu_fdt_setprop_cells(ms->fdt, path, "phandle",
                           qemu_fdt_alloc_phandle(ms->fdt));

    create_fdt_domain_possible_harts(ms, s, path);
    create_fdt_domain_regions(ms, s, path);

    if (s->boot_hart != -1) {
        g_autofree char *cpu_name = g_strdup_printf("/cpus/cpu@%i", s->boot_hart);
        qemu_fdt_setprop_cell(ms->fdt, path, "boot-hart",
                              qemu_fdt_get_phandle(ms->fdt, cpu_name));
        if (s->assign) {
            qemu_fdt_setprop_cell(ms->fdt, cpu_name, "opensbi-domain",
                                    qemu_fdt_get_phandle(ms->fdt, path));
        }
    }

    if (s->next_arg1 != -1) {
        qemu_fdt_setprop_cells(ms->fdt, path, "next-arg1",
                             (uint64_t) s->next_arg1 >> 32, s->next_arg1);
    }

    if (s->next_addr != -1) {
        qemu_fdt_setprop_cells(ms->fdt, path, "next-addr",
                             (uint64_t) s->next_addr >> 32, s->next_addr);
    }

    if (s->next_mode != -1) {
        qemu_fdt_setprop_cell(ms->fdt, path, "next-mode",
                            s->next_mode);
    }

    if (s->system_reset_allowed) {
        qemu_fdt_setprop(ms->fdt, path, "system-reset-allowed", NULL, 0);
    }

    if (s->system_suspend_allowed) {
        qemu_fdt_setprop(ms->fdt, path, "system-suspend-allowed", NULL, 0);
    }
}

static uint32_t create_fdt_one_device(MachineState *ms, char *device)
{
    uint32_t phandle;
    int offs = fdt_path_offset(ms->fdt, device);

    if (offs < 0) {
        error_report("%s: Could not find device %s: %s", __func__,
                     device, fdt_strerror(offs));
        exit(1);
    }

    phandle = fdt_get_phandle(ms->fdt, offs);
    if (!phandle) {
        phandle = qemu_fdt_alloc_phandle(ms->fdt);
        qemu_fdt_setprop_cell(ms->fdt, device, "phandle", phandle);
    }

    return phandle;
}

static void create_fdt_one_memregion(MachineState *ms,
                                     OpenSBIMemregionState *s)
{
    g_autofree char *path;
    int i, dev, num_devices;
    DeviceState *ds = DEVICE(s);

    path = g_strdup_printf("/chosen/opensbi-domains/%s", ds->id);
    qemu_fdt_add_subnode(ms->fdt, path);
    qemu_fdt_setprop_string(ms->fdt, path, "compatible",
                            "opensbi,domain,memregion");
    qemu_fdt_setprop_cells(ms->fdt, path, "base",
                           (uint64_t) s->base >> 32, s->base);

    qemu_fdt_setprop_cell(ms->fdt, path, "order",
                          (uint32_t) s->order);

    if (s->mmio) {
        qemu_fdt_setprop(ms->fdt, path, "mmio", NULL, 0);

        /* Get all phandles for related devices */
        num_devices = 0;
        for (i = 0; i < OPENSBI_MEMREGION_DEVICES_MAX; i++) {
            if (s->devices[i]) {
                num_devices++;
            }
        }

        if (num_devices) {
            g_autofree uint32_t *devices =
                g_malloc0_n(num_devices, sizeof(uint32_t));
            for (i = 0, dev = 0; i < OPENSBI_MEMREGION_DEVICES_MAX &&
                                 dev < num_devices; i++) {
                if (s->devices[i]) {
                    devices[dev++] = create_fdt_one_device(ms,
                                                         s->devices[i]);
                }
            }

            qemu_fdt_setprop(ms->fdt, path, "devices", devices,
                             num_devices * 4);
        }
    }

    qemu_fdt_setprop_cells(ms->fdt, path, "phandle",
                           qemu_fdt_alloc_phandle(ms->fdt));
}

static int create_fdt_domains(Object *obj, void *opaque)
{
    struct DomainFDTState *dfs = opaque;
    OpenSBIDomainState *osds;
    OpenSBIMemregionState *osms;

    osds = (OpenSBIDomainState *)
            object_dynamic_cast(obj, TYPE_OPENSBI_DOMAIN);
    osms = (OpenSBIMemregionState *)
            object_dynamic_cast(obj, TYPE_OPENSBI_MEMREGION);

    if (dfs->regions) {
        if (osms) {
            create_fdt_one_memregion(dfs->ms, osms);
        }
    } else {
        if (osds) {
            create_fdt_one_domain(dfs->ms, osds);
        }
    }

    return 0;
}

static const char *containers[] = {
        "/peripheral", "/peripheral-anon"
};

void create_fdt_opensbi_domains(MachineState *s)
{
    int i;
    MachineState *ms = MACHINE(s);
    Object *container;

    struct DomainFDTState check = {
            .ms = ms,
            .regions = true
    };

    /* Make sure that top-level node exists */
    qemu_fdt_add_subnode(ms->fdt, "/chosen/opensbi-domains");
    qemu_fdt_setprop_string(ms->fdt, "/chosen/opensbi-domains",
                            "compatible", "opensbi,domain,config");

    /* Do a scan through regions first */
    for (i = 0; i < ARRAY_SIZE(containers); i++) {
        container = container_get(OBJECT(s), containers[i]);
        object_child_foreach(container, create_fdt_domains, &check);
    }

    /* Then scan through domains */
    check.regions = false;
    for (i = 0; i < ARRAY_SIZE(containers); i++) {
        container = container_get(OBJECT(s), containers[i]);
        object_child_foreach(container, create_fdt_domains, &check);
    }
}

/* OpenSBI Memregions */

static void set_mmio(Object *obj, bool val, Error **err)
{
    OpenSBIMemregionState *s = OPENSBI_MEMREGION(obj);
    s->mmio = val;
}

static void set_device(Object *obj, const char *val, Error **err)
{
    int i;
    OpenSBIMemregionState *s = OPENSBI_MEMREGION(obj);

    for (i = 0; i < OPENSBI_DOMAIN_MEMREGIONS_MAX; i++) {
        if (!s->devices[i]) {
            s->devices[i] = g_strdup(val);
            break;
        }
    }
}

static void opensbi_memregion_instance_init(Object *obj)
{
    int i;
    OpenSBIMemregionState *s = OPENSBI_MEMREGION(obj);

    s->base = -1;
    object_property_add_uint64_ptr(obj, "base", &s->base,
                                   OBJ_PROP_FLAG_WRITE);
    object_property_set_description(obj, "base",
                                    "The base address of the domain memory region. If \"order\" is also specified, "
                                    "this property should be a 2 ^ order aligned 64 bit address");

    s->order = -1;
    object_property_add_uint32_ptr(obj, "order", &s->order,
                                   OBJ_PROP_FLAG_WRITE);
    object_property_set_description(obj, "order",
                                    "The order of the domain memory region. This property should have a 32 bit value "
                                    "(i.e. one DT cell) in the range 3 <= order <= __riscv_xlen.");

    s->mmio = false;
    object_property_add_bool(obj, "mmio", NULL, set_mmio);
    object_property_set_description(obj, "mmio",
                                    "A boolean flag representing whether the domain memory region is a "
                                    "memory-mapped I/O (MMIO) region.");

    for (i = 0; i < OPENSBI_DOMAIN_MEMREGIONS_MAX; i++) {
        g_autofree char *propname = g_strdup_printf("device%i", i);
        object_property_add_str(obj, propname, NULL, set_device);

        g_autofree char *description = g_strdup_printf(
                "Device %i (out of %i) for this memregion. This property should be a device tree path to the device.",
                i, OPENSBI_DOMAIN_MEMREGIONS_MAX);
        object_property_set_description(obj, propname, description);
    }
}

static void opensbi_memregion_realize(DeviceState *ds, Error **errp)
{
    #if defined(TARGET_RISCV32)
    int xlen = 32;
    #elif defined(TARGET_RISCV64)
    int xlen = 64;
    #endif

    OpenSBIMemregionState *s = OPENSBI_MEMREGION(ds);

    if (s->base == -1) {
        error_setg(errp, "must specify base");
        return;
    }

    if (s->order == -1) {
        error_setg(errp, "must specify order");
        return;
    }

    /* Check order bounds */
    if (s->order < 3 || s->order > xlen) {
        error_setg(errp, "order must be between 3 and %d", xlen);
        return;
    }

    /* Check base alignment */
    if (s->order < xlen && (s->base & (BIT(s->order) - 1))) {
        error_setg(errp, "base not aligned to order");
        return;
    }
}

static void opensbi_memregion_class_init(ObjectClass *oc, void *opaque)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    dc->realize = opensbi_memregion_realize;
}

static const TypeInfo opensbi_memregion_info = {
        .name = TYPE_OPENSBI_MEMREGION,
        .parent = TYPE_DEVICE,
        .instance_init = opensbi_memregion_instance_init,
        .instance_size = sizeof(OpenSBIDomainState),
        .class_init = opensbi_memregion_class_init
};

/* OpenSBI Domains */

static void set_sysreset_allowed(Object *obj, bool val, Error **err)
{
    OpenSBIDomainState *s = OPENSBI_DOMAIN(obj);
    s->system_reset_allowed = val;
}

static void set_suspend_allowed(Object *obj, bool val, Error **err)
{
    OpenSBIDomainState *s = OPENSBI_DOMAIN(obj);
    s->system_suspend_allowed = val;
}

static void set_assign(Object *obj, bool val, Error **err)
{
    OpenSBIDomainState *s = OPENSBI_DOMAIN(obj);
    s->assign = val;
}

static void set_possible_harts(Object *obj, const char *str, Error **err)
{
    OpenSBIDomainState *s = OPENSBI_DOMAIN(obj);
    const char *firstcpu,  *firstcpu_end, *lastcpu;

    firstcpu = str;
    if (qemu_strtoul(firstcpu, &firstcpu_end, 0,
                     &s->first_possible_hart) < 0) {
        error_setg(err, "could not convert firstcpu");
        return;
    }

    lastcpu = qemu_strchrnul(str, '-');
    if (*lastcpu) {
        if (lastcpu != firstcpu_end) {
            error_setg(err, "could not separate firstcpu and lastcpu");
            return;
        }

        lastcpu++;
        if (qemu_strtoul(lastcpu, NULL, 0,
                         &s->last_possible_hart) < 0) {
            error_setg(err, "could not convert lastcpu");
            return;
        }
    } else {
        s->last_possible_hart = s->first_possible_hart;
    }
}

static void opensbi_domain_instance_init(Object *obj)
{
    int i;
    OpenSBIDomainState *s = OPENSBI_DOMAIN(obj);

    s->boot_hart = VIRT_CPUS_MAX;
    object_property_add_uint32_ptr(obj, "boot-hart", &s->boot_hart,
                                   OBJ_PROP_FLAG_WRITE);
    object_property_set_description(obj, "boot-hart",
                                    "The HART booting the domain instance.");

    s->first_possible_hart = -1;
    s->last_possible_hart = -1;
    object_property_add_str(obj, "possible-harts", NULL, set_possible_harts);
    object_property_set_description(obj, "possible-harts",
                                    "The contiguous list of CPUs for the domain instance, specified as firstcpu[-lastcpu]");

    s->next_arg1 = -1;
    object_property_add_uint64_ptr(obj, "next-arg1", &s->next_arg1,
                                   OBJ_PROP_FLAG_WRITE);
    object_property_set_description(obj, "next-arg1",
                                    "The 64 bit next booting stage arg1 for the domain instance.");

    s->next_addr = -1;
    object_property_add_uint64_ptr(obj, "next-addr", &s->next_addr,
                                   OBJ_PROP_FLAG_WRITE);
    object_property_set_description(obj, "next-addr",
                                    "The 64 bit next booting stage address for the domain instance.");

    s->next_mode = -1;
    object_property_add_uint32_ptr(obj, "next-mode", &s->next_mode,
                                   OBJ_PROP_FLAG_WRITE);
    object_property_set_description(obj, "next-mode",
                                    "The 32 bit next booting stage mode for the domain instance.");

    s->system_reset_allowed = false;
    object_property_add_bool(obj, "system-reset-allowed", NULL,
                             set_sysreset_allowed);
    object_property_set_description(obj, "system-reset-allowed",
                                    "Whether the domain instance is allowed to do system reset.");

    s->system_suspend_allowed = false;
    object_property_add_bool(obj, "system-suspend-allowed", NULL,
                             set_suspend_allowed);
    object_property_set_description(obj, "system-suspend-allowed",
                                    "Whether the domain instance is allowed to do system suspend.");

    for (i = 0; i < OPENSBI_DOMAIN_MEMREGIONS_MAX; i++) {
        s->regions[i] = NULL;
        g_autofree char *reg_propname = g_strdup_printf("region%i", i);
        object_property_add_link(obj, reg_propname, TYPE_OPENSBI_MEMREGION,
                                 (Object **) &s->regions[i],
                                 qdev_prop_allow_set_link_before_realize, 0);

        g_autofree char *reg_description = g_strdup_printf(
                "Region %i (out of %i) for this domain.",
                i, OPENSBI_DOMAIN_MEMREGIONS_MAX);
        object_property_set_description(obj, reg_propname, reg_description);

        s->region_perms[i] = 0;
        g_autofree char *perm_propname = g_strdup_printf("perms%i", i);
        object_property_add_uint32_ptr(obj, perm_propname, &s->region_perms[i],
                                       OBJ_PROP_FLAG_WRITE);

        g_autofree char *perm_description = g_strdup_printf(
                "Permissions for region %i for this domain.", i);
        object_property_set_description(obj, perm_propname, perm_description);
    }

    object_property_add_bool(obj, "assign", NULL, set_assign);
    object_property_set_description(obj, "assign",
                                    "Whether to assign this domain to its boot hart.");
}

static void opensbi_domain_realize(DeviceState *ds, Error **errp)
{
    OpenSBIDomainState *s = OPENSBI_DOMAIN(ds);

    if (!ds->id) {
        error_setg(errp, "must specify an id");
        return;
    }

    if (s->boot_hart >= VIRT_CPUS_MAX) {
        error_setg(errp, "boot hart larger than maximum number of CPUs (%d)",
                 VIRT_CPUS_MAX);
        return;
    }

    if (s->first_possible_hart == -1) {
        if (s->last_possible_hart != -1) {
            error_setg(errp,
                     "last possible hart set when first possible hart unset");
            return;
        }
    } else {
        if (s->first_possible_hart >= VIRT_CPUS_MAX) {
            error_setg(errp,
                     "first possible hart larger than maximum number of CPUs (%d)",
                     VIRT_CPUS_MAX);
            return;
        }

        if (s->last_possible_hart != -1) {
            if (s->last_possible_hart < s->first_possible_hart) {
                error_setg(errp,
                         "last possible hart larger than first possible hart");
                return;
            }

            if (s->last_possible_hart >= VIRT_CPUS_MAX) {
                error_setg(errp,
                         "last possible hart larger than maximum number of CPUS (%d)",
                         VIRT_CPUS_MAX);
                return;
            }
        }
    }
}

static void opensbi_domain_class_init(ObjectClass *oc, void *opaque)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    dc->realize = opensbi_domain_realize;
}

static const TypeInfo opensbi_domain_info = {
        .name = TYPE_OPENSBI_DOMAIN,
        .parent = TYPE_DEVICE,
        .instance_init = opensbi_domain_instance_init,
        .instance_size = sizeof(OpenSBIDomainState),
        .class_init = opensbi_domain_class_init
};

static void opensbi_register_types(void)
{
    type_register_static(&opensbi_domain_info);
    type_register_static(&opensbi_memregion_info);
}

type_init(opensbi_register_types)
