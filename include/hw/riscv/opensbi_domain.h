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

#ifndef RISCV_DOMAIN_H
#define RISCV_DOMAIN_H

#include "hw/sysbus.h"
#include "qom/object.h"
#include "cpu.h"

#define TYPE_OPENSBI_MEMREGION "opensbi-memregion"
OBJECT_DECLARE_SIMPLE_TYPE(OpenSBIMemregionState, OPENSBI_MEMREGION)

#define OPENSBI_MEMREGION_DEVICES_MAX   16

struct OpenSBIMemregionState {
    /* public */
    DeviceState parent_obj;

    /* private */
    uint64_t base;
    uint32_t order;
    bool mmio;
    char *devices[OPENSBI_MEMREGION_DEVICES_MAX];
};

#define TYPE_OPENSBI_DOMAIN "opensbi-domain"
OBJECT_DECLARE_SIMPLE_TYPE(OpenSBIDomainState, OPENSBI_DOMAIN)

#define OPENSBI_DOMAIN_MEMREGIONS_MAX   16

struct OpenSBIDomainState {
    /* public */
    DeviceState parent_obj;

    /* private */
    OpenSBIMemregionState *regions[OPENSBI_DOMAIN_MEMREGIONS_MAX];
    unsigned int region_perms[OPENSBI_DOMAIN_MEMREGIONS_MAX];
    unsigned long first_possible_hart, last_possible_hart;
    unsigned int boot_hart;
    uint64_t next_arg1;
    uint64_t next_addr;
    uint32_t next_mode;
    bool system_reset_allowed;
    bool system_suspend_allowed;

    bool assign;
};

void create_fdt_opensbi_domains(MachineState *s);

#endif /* RISCV_DOMAIN_H */
