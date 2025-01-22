/*
 * QEMU CPU model (system specific)
 *
 * Copyright (c) 2012-2014 SUSE LINUX Products GmbH
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see
 * <http://www.gnu.org/licenses/gpl-2.0.html>
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "exec/tswap.h"
#include "hw/core/sysemu-cpu-ops.h"

bool cpu_paging_enabled(const CPUState *cpu)
{
    if (cpu->cc->sysemu_ops->get_paging_enabled) {
        return cpu->cc->sysemu_ops->get_paging_enabled(cpu);
    }

    return false;
}

bool cpu_get_memory_mapping(CPUState *cpu, MemoryMappingList *list,
                            Error **errp)
{
    if (cpu->cc->sysemu_ops->get_memory_mapping) {
        return cpu->cc->sysemu_ops->get_memory_mapping(cpu, list, errp);
    }

    error_setg(errp, "Obtaining memory mappings is unsupported on this CPU.");
    return false;
}

hwaddr cpu_get_phys_page_attrs_debug(CPUState *cpu, vaddr addr,
                                     MemTxAttrs *attrs)
{
    if (cpu->cc->sysemu_ops->get_phys_page_attrs_debug) {
        return cpu->cc->sysemu_ops->get_phys_page_attrs_debug(cpu, addr, attrs);
    }
    /* Fallback for CPUs which don't implement the _attrs_ hook */
    *attrs = MEMTXATTRS_UNSPECIFIED;
    return cpu->cc->sysemu_ops->get_phys_page_debug(cpu, addr);
}

hwaddr cpu_get_phys_page_debug(CPUState *cpu, vaddr addr)
{
    MemTxAttrs attrs = {};

    return cpu_get_phys_page_attrs_debug(cpu, addr, &attrs);
}

int cpu_asidx_from_attrs(CPUState *cpu, MemTxAttrs attrs)
{
    int ret = 0;

    if (cpu->cc->sysemu_ops->asidx_from_attrs) {
        ret = cpu->cc->sysemu_ops->asidx_from_attrs(cpu, attrs);
        assert(ret < cpu->num_ases && ret >= 0);
    }
    return ret;
}

int cpu_write_elf32_qemunote(WriteCoreDumpFunction f, CPUState *cpu,
                             void *opaque)
{
    if (!cpu->cc->sysemu_ops->write_elf32_qemunote) {
        return 0;
    }
    return (*cpu->cc->sysemu_ops->write_elf32_qemunote)(f, cpu, opaque);
}

int cpu_write_elf32_note(WriteCoreDumpFunction f, CPUState *cpu,
                         int cpuid, void *opaque)
{
    if (!cpu->cc->sysemu_ops->write_elf32_note) {
        return -1;
    }
    return (*cpu->cc->sysemu_ops->write_elf32_note)(f, cpu, cpuid, opaque);
}

int cpu_write_elf64_qemunote(WriteCoreDumpFunction f, CPUState *cpu,
                             void *opaque)
{
    if (!cpu->cc->sysemu_ops->write_elf64_qemunote) {
        return 0;
    }
    return (*cpu->cc->sysemu_ops->write_elf64_qemunote)(f, cpu, opaque);
}

int cpu_write_elf64_note(WriteCoreDumpFunction f, CPUState *cpu,
                         int cpuid, void *opaque)
{
    if (!cpu->cc->sysemu_ops->write_elf64_note) {
        return -1;
    }
    return (*cpu->cc->sysemu_ops->write_elf64_note)(f, cpu, cpuid, opaque);
}

bool cpu_virtio_is_big_endian(CPUState *cpu)
{
    if (cpu->cc->sysemu_ops->virtio_is_big_endian) {
        return cpu->cc->sysemu_ops->virtio_is_big_endian(cpu);
    }
    return target_words_bigendian();
}

GuestPanicInformation *cpu_get_crash_info(CPUState *cpu)
{
    GuestPanicInformation *res = NULL;

    if (cpu->cc->sysemu_ops->get_crash_info) {
        res = cpu->cc->sysemu_ops->get_crash_info(cpu);
    }
    return res;
}
