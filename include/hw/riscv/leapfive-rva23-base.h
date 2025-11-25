/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *
 * LeapFive Victory RISC-V machine interface
 *
 * Copyright (c) 2025 LeapFive, Inc.
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

#ifndef HW_LEAPFIVE_RVA23_BASE_H
#define HW_LEAPFIVE_RVA23_BASE_H

#include "hw/boards.h"
#include "hw/riscv/riscv_hart.h"
#include "hw/sysbus.h"
#include "hw/intc/riscv_imsic.h"
#include "hw/char/serial.h"
#include "hw/sd/sdhci.h"

#define TYPE_LEAPFIVE_MACHINE MACHINE_TYPE_NAME("leapfive-rva23-base")
typedef struct LeapfiveState LeapfiveState;
DECLARE_INSTANCE_CHECKER(LeapfiveState, LEAPFIVE_MACHINE,
                        TYPE_LEAPFIVE_MACHINE)

#define LEAPFIVE_CPUS_MAX           32
#define LEAPFIVE_NUMA_MAX           4

struct LeapfiveState {
    /*< private >*/
    MachineState parent;

    /*< public >*/
    Notifier machine_done;
    RISCVHartArrayState soc[LEAPFIVE_NUMA_MAX];
    DeviceState *irqchip[LEAPFIVE_NUMA_MAX];
    int fdt_size;
    bool iommu_sys;
    bool aia;
};

void create_fdt_leapfive_cpu_cache(void *fdt, int base_hartid,
                                char *clust_name, int num_harts,
                                uint32_t *phandle);
