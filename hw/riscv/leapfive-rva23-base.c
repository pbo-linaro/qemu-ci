/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *
 * QEMU RISC-V leapfive Board
 *
 * Copyright (c) 2025 LeapFive, Inc.
 *
 * Provides a RISCV board with follow device
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
#include "qemu/units.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "qemu/guest-random.h"
#include "qapi/error.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "hw/sysbus.h"
#include "hw/qdev-properties.h"
#include "hw/char/serial-mm.h"
#include "target/riscv/cpu.h"
#include "hw/core/sysbus-fdt.h"
#include "target/riscv/pmu.h"
#include "hw/riscv/riscv_hart.h"
#include "hw/riscv/iommu.h"
#include "hw/riscv/riscv-iommu-bits.h"
#include "hw/riscv/leapfive-rva23-base.h"
#include "hw/riscv/boot.h"
#include "hw/riscv/numa.h"
#include "kvm/kvm_riscv.h"
#include "hw/intc/riscv_aplic.h"
#include "hw/intc/sifive_plic.h"
#include "hw/intc/riscv_aclint.h"
#include "hw/misc/sifive_test.h"
#include "hw/platform-bus.h"
#include "chardev/char.h"
#include "system/device_tree.h"
#include "system/system.h"
#include "system/tcg.h"
#include "system/kvm.h"
#include "system/qtest.h"
#include "hw/pci/pci.h"
#include "hw/pci-host/gpex.h"
#include "hw/display/ramfb.h"
#include "qapi/qapi-visit-common.h"
#include "hw/virtio/virtio-iommu.h"
#include "hw/ssi/sifive_spi.h"
#include "hw/sd/sdhci.h"
#include "hw/uefi/var-service-api.h"

#define LEAPFIVE_IRQCHIP_NUM_MSIS               255
#define LEAPFIVE_IRQCHIP_NUM_SOURCES            96
#define LEAPFIVE_IRQCHIP_NUM_PRIO_BITS          3
#define LEAPFIVE_IRQCHIP_MAX_GUESTS_BITS        3
#define LEAPFIVE_IRQCHIP_MAX_GUESTS \
        ((1U << LEAPFIVE_IRQCHIP_MAX_GUESTS_BITS) - 1U)

#define LEAPFIVE_PLIC_PRIORITY_BASE             0x00
#define LEAPFIVE_PLIC_PENDING_BASE              0x1000
#define LEAPFIVE_PLIC_ENABLE_BASE               0x2000
#define LEAPFIVE_PLIC_ENABLE_STRIDE             0x80
#define LEAPFIVE_PLIC_CONTEXT_BASE              0x200000
#define LEAPFIVE_PLIC_CONTEXT_STRIDE            0x1000

#define LEAPFIVE_UART_REF_CLK                   100000000
#define LEAPFIVE_SDHCI_XIN_CLK                  100000000
#define LEAPFIVE_SDHCI_AHB_CLK                  100000000
#define LEAPFIVE_SDHCI_SPEC_VERSION             3
#define LEAPFIVE_SDHCI_CAPABILITIES             0x280737ec6481

#define FDT_PCI_ADDR_CELLS                      3
#define FDT_PCI_INT_CELLS                       1
#define FDT_PLIC_ADDR_CELLS                     0
#define FDT_PLIC_INT_CELLS                      1
#define FDT_APLIC_INT_CELLS                     2
#define FDT_APLIC_ADDR_CELLS                    0
#define FDT_IMSIC_INT_CELLS                     0
#define FDT_MAX_INT_CELLS                       2
#define FDT_MAX_INT_MAP_WIDTH \
        (FDT_PCI_ADDR_CELLS + FDT_PCI_INT_CELLS + 1 + FDT_MAX_INT_CELLS)
#define LEAPFIVE_ACLINT_DEFAULT_TIMEBASE_FREQ   10000000
#define PCI_NUM_PINS                            4
#define APEI_MEM_SZ                             0x80000UL

#define LEAPFIVE_IMSIC_GROUP_MAX_SIZE   (1U << IMSIC_MMIO_GROUP_MIN_SHIFT)
#define LEAPFIVE_IMSIC_MAX_SIZE         (LEAPFIVE_IMSIC_GROUP_MAX_SIZE << 2)
#define LEAPFIVE_NUM_GUESTS             10

enum {
    LEAPFIVE_DEBUG,
    LEAPFIVE_MROM,
    LEAPFIVE_TEST,
    LEAPFIVE_RTC,
    LEAPFIVE_CLINT,
    LEAPFIVE_PLIC,
    LEAPFIVE_APLIC_M,
    LEAPFIVE_APLIC_S,
    LEAPFIVE_IMSIC_M,
    LEAPFIVE_IMSIC_S,
    LEAPFIVE_UART0,
    LEAPFIVE_VIRTIO,
    LEAPFIVE_SDHCI,
    LEAPFIVE_DRAM,
    LEAPFIVE_PCIE_MMIO,
    LEAPFIVE_PCIE_PIO,
    LEAPFIVE_PCIE_ECAM,
    LEAPFIVE_PCIE_MMIO_HIGH,
    LEAPFIVE_IOMMU_SYS,
    LEAPFIVE_LAST_MEMMAP /* Keep this entry always last */
};

enum {
    LEAPFIVE_VIRTIO_IRQ = 1,
    LEAPFIVE_VIRTIO_COUNT = 8,
    LEAPFIVE_UART0_IRQ = 10,
    LEAPFIVE_SDHCI_IRQ = 14,
    LEAPFIVE_RTC_IRQ = 20,
    LEAPFIVE_PCIE_IRQ = 36, /* 36 to 39 */
    LEAPFIVE_IOMMU_SYS_IRQ = 40, /* 40-43 */
};

static const MemMapEntry leapfive_memmap[] = {
    [LEAPFIVE_DEBUG] =                  { 0x00000000, 0x00001000 },
    [LEAPFIVE_MROM] =                   { 0x00001000, 0x00001000 },
    [LEAPFIVE_TEST] =                   { 0x00100000, 0x00001000 },
    [LEAPFIVE_RTC] =                    { 0x00101000, 0x00001000 },
    [LEAPFIVE_CLINT] =                  { 0x02000000, 0x00010000 },
    [LEAPFIVE_PCIE_PIO] =               { 0x03000000, 0x00010000 },
    [LEAPFIVE_IOMMU_SYS] =              { 0x03010000, 0x00001000 },
    [LEAPFIVE_PLIC] =                   { 0x0c000000, 0x00400000 },
    [LEAPFIVE_APLIC_M] =                { 0x0c000000, 0x00400000 },
    [LEAPFIVE_APLIC_S] =                { 0x0d000000, 0x00400000 },
    [LEAPFIVE_UART0] =                  { 0x10000000, 0x00001000 },
    [LEAPFIVE_VIRTIO] =                 { 0x10001000, 0x00001000 },
    [LEAPFIVE_SDHCI] =                  { 0x10004000, 0x00001000 },
    [LEAPFIVE_IMSIC_M] =                { 0x20000000, 0x00400000 },
    [LEAPFIVE_IMSIC_S] =                { 0x20400000, 0x00400000 },
    [LEAPFIVE_PCIE_ECAM] =              { 0x30000000, 0x10000000 },
    [LEAPFIVE_PCIE_MMIO] =              { 0x40000000, 0x40000000 },
    [LEAPFIVE_DRAM] =                   { 0x80000000, 0xFF80000000ULL },
    [LEAPFIVE_PCIE_MMIO_HIGH] =         { 0x10000000000ULL, 0x400000000ULL },
};

struct LeapfiveCpuCache {
    int type;
    int level;
    int size;
    int sets;
};
enum LeapfiveCacheType {
    LEAPFIVE_CACHE_I,
    LEAPFIVE_CACHE_D,
    LEAPFIVE_CACHE_U,
};
enum LeapfiveCacheId  {
    LEAPFIVE_CACHE_DL1,
    LEAPFIVE_CACHE_IL1,
    LEAPFIVE_CACHE_DL2,
    LEAPFIVE_CACHE_DL3,
    LEAPFIVE_CACHE_ID_MAX,
};

struct LeapfiveCpuCache leapfive_caches[4] = {
    {LEAPFIVE_CACHE_D, 1, 131072, 256},
    {LEAPFIVE_CACHE_I, 1, 524288, 1024},
    {LEAPFIVE_CACHE_D, 2, 1048576, 2048},
    {LEAPFIVE_CACHE_U, 3, 4194304, 4096},
};

void create_fdt_leapfive_cpu_cache(void *fdt, int base_hartid,
                                   char *clust_name, int num_harts,
                                   uint32_t *phandle)
{
    int cpu, i;
    uint32_t l2_phandle, l3_phandle;
    struct LeapfiveCpuCache *caches = &leapfive_caches[0];
    char *cpu_name, *l3_name, *l2_name, *cache_name, *cache_p = NULL;
    char *cache_sets = NULL;

    l3_name = g_strdup_printf("%s/l3-cache", clust_name);
    l3_phandle = (*phandle)++;
    qemu_fdt_add_subnode(fdt, l3_name);
    qemu_fdt_setprop_cell(fdt, l3_name, "phandle", l3_phandle);

    for (cpu = base_hartid; cpu < num_harts + base_hartid; cpu++) {
        cpu_name = g_strdup_printf("/cpus/cpu@%d", cpu);
        l2_name = g_strdup_printf("%s/l2-caches", cpu_name);
        l2_phandle = (*phandle)++;
        qemu_fdt_add_subnode(fdt, l2_name);
        qemu_fdt_setprop_cell(fdt, l2_name, "phandle", l2_phandle);
        for (i = 0; i < 4; i++) {
            switch (i) {
            case LEAPFIVE_CACHE_IL1:
            case LEAPFIVE_CACHE_DL1:
                cache_name = cpu_name;
                break;
            case LEAPFIVE_CACHE_DL2:
                cache_name = l2_name;
                break;
            case LEAPFIVE_CACHE_DL3:
                cache_name = l3_name;
                break;
            }

            switch (caches[i].type) {
            case LEAPFIVE_CACHE_I:
                cache_p = g_strdup_printf("i-cache-size");
                cache_sets = g_strdup_printf("i-cache-sets");
                break;
            case LEAPFIVE_CACHE_D:
                cache_p = g_strdup_printf("d-cache-size");
                cache_sets = g_strdup_printf("d-cache-sets");
                break;
            case LEAPFIVE_CACHE_U:
                cache_p = g_strdup_printf("cache-size");
                cache_sets = g_strdup_printf("cache-sets");
                break;
            }

            switch (caches[i].level) {
            case 1:
                qemu_fdt_setprop_cell(fdt, cpu_name,
                                    "next-level-cache",
                                    l2_phandle);
                break;
            case 2:
                qemu_fdt_setprop_cell(fdt, cache_name,
                                    "next-level-cache",
                                    l3_phandle);
            /* Fallthrough */
            case 3:
                qemu_fdt_setprop_string(fdt, cache_name, "compatible",
                                        "cache");
                qemu_fdt_setprop_cell(fdt, cache_name, "cache-level",
                                    caches[i].level);
                break;
            }

            qemu_fdt_setprop_cell(fdt, cache_name, cache_sets,
                                  caches[i].level < 3 ?  caches[i].sets :
                                  caches[i].sets * num_harts);
            qemu_fdt_setprop_cell(fdt, cache_name, cache_p,
                                  caches[i].level < 3 ?  caches[i].size :
                                  caches[i].size * num_harts);
        }
        g_free(cpu_name);
        g_free(l2_name);
    }
}

static void create_pcie_irq_map(LeapfiveState *s, void *fdt,
                                char *nodename,
                                uint32_t irqchip_phandle)
{
    int pin, dev;
    uint32_t irq_map_stride = 0;
    uint32_t full_irq_map[PCI_NUM_PINS * PCI_NUM_PINS *
                        FDT_MAX_INT_MAP_WIDTH] = {};
    uint32_t *irq_map = full_irq_map;

    /*
     * This code creates a standard swizzle of interrupts such that
     * each device's first interrupt is based on it's PCI_SLOT number.
     * (See pci_swizzle_map_irq_fn())
     *
     * We only need one entry per interrupt in the table (not one per
     * possible slot) seeing the interrupt-map-mask will allow the table
     * to wrap to any number of devices.
     */
    for (dev = 0; dev < PCI_NUM_PINS; dev++) {
        int devfn = dev * 0x8;
        for (pin = 0; pin < PCI_NUM_PINS; pin++) {
            int irq_nr = LEAPFIVE_PCIE_IRQ +
                            ((pin + PCI_SLOT(devfn)) % PCI_NUM_PINS);
            int i = 0;

            /* Fill PCI address cells */
            irq_map[i] = cpu_to_be32(devfn << 8);
            i += FDT_PCI_ADDR_CELLS;

            /* Fill PCI Interrupt cells */
            irq_map[i] = cpu_to_be32(pin + 1);
            i += FDT_PCI_INT_CELLS;

            /* Fill interrupt controller phandle and cells */
            irq_map[i++] = cpu_to_be32(irqchip_phandle);
            irq_map[i++] = cpu_to_be32(irq_nr);
            if (s->aia) {
                irq_map[i++] = cpu_to_be32(0x4);
            }

            if (!irq_map_stride) {
                irq_map_stride = i;
            }
            irq_map += irq_map_stride;
        }
    }

    qemu_fdt_setprop(fdt, nodename, "interrupt-map", full_irq_map,
                     PCI_NUM_PINS * PCI_NUM_PINS *
                     irq_map_stride * sizeof(uint32_t));

    qemu_fdt_setprop_cells(fdt, nodename, "interrupt-map-mask",
                           0x1800, 0, 0, 0x7);
}

static void create_fdt_cpus(LeapfiveState *s, int numa,
                            char *clust_name, uint32_t *phandle,
                            uint32_t *intc_phandles,
                            uint32_t *cpu_phandles)
{
    int cpu;
    uint32_t cpu_phandle;
    MachineState *ms = MACHINE(s);
    for (cpu = s->soc[numa].num_harts - 1; cpu >= 0; cpu--) {
        RISCVCPU *cpu_ptr = &s->soc[numa].harts[cpu];
        g_autofree char *cpu_name = NULL;
        g_autofree char *core_name = NULL;
        g_autofree char *intc_name = NULL;
        g_autofree char *sv_name = NULL;
        cpu_phandle = (*phandle)++;
        cpu_phandles[cpu] = cpu_phandle;

        cpu_name = g_strdup_printf("/cpus/cpu@%d",
                                   s->soc[numa].hartid_base + cpu);
        qemu_fdt_add_subnode(ms->fdt, cpu_name);
        qemu_fdt_setprop_string(ms->fdt, cpu_name, "mmu-type", "riscv,sv48");
        riscv_isa_write_fdt(cpu_ptr, ms->fdt, cpu_name);

        if (cpu_ptr->cfg.ext_zicbom) {
            qemu_fdt_setprop_cell(ms->fdt, cpu_name, "riscv,cbom-block-size",
                                  cpu_ptr->cfg.cbom_blocksize);
        }

        if (cpu_ptr->cfg.ext_zicboz) {
            qemu_fdt_setprop_cell(ms->fdt, cpu_name, "riscv,cboz-block-size",
                                  cpu_ptr->cfg.cboz_blocksize);
        }

        if (cpu_ptr->cfg.ext_zicbop) {
            qemu_fdt_setprop_cell(ms->fdt, cpu_name, "riscv,cbop-block-size",
                                  cpu_ptr->cfg.cbop_blocksize);
        }

        qemu_fdt_setprop_string(ms->fdt, cpu_name, "compatible", "riscv");
        qemu_fdt_setprop_string(ms->fdt, cpu_name, "status", "okay");
        qemu_fdt_setprop_cell(ms->fdt, cpu_name, "reg",
                              s->soc[numa].hartid_base + cpu);
        qemu_fdt_setprop_string(ms->fdt, cpu_name, "device_type", "cpu");
        riscv_socket_fdt_write_id(ms, cpu_name, numa);
        qemu_fdt_setprop_cell(ms->fdt, cpu_name, "phandle", cpu_phandle);
        intc_phandles[cpu] = (*phandle)++;

        intc_name = g_strdup_printf("%s/interrupt-controller", cpu_name);
        qemu_fdt_add_subnode(ms->fdt, intc_name);
        qemu_fdt_setprop_cell(ms->fdt, intc_name, "phandle",
                              intc_phandles[cpu]);
        qemu_fdt_setprop_string(ms->fdt, intc_name, "compatible",
                                "riscv,cpu-intc");
        qemu_fdt_setprop(ms->fdt, intc_name, "interrupt-controller",
                         NULL, 0);
        qemu_fdt_setprop_cell(ms->fdt, intc_name, "#interrupt-cells", 1);

        core_name = g_strdup_printf("%s/core%d", clust_name, cpu);
        qemu_fdt_add_subnode(ms->fdt, core_name);
        qemu_fdt_setprop_cell(ms->fdt, core_name, "cpu", cpu_phandle);
    }
    create_fdt_leapfive_cpu_cache(ms->fdt, s->soc[numa].hartid_base,
                                  clust_name, s->soc[numa].num_harts,
                                  phandle);
}

static void create_fdt_memory(LeapfiveState *s, int numa)
{
    g_autofree char *mem_name = NULL;
    uint64_t addr, size;
    MachineState *ms = MACHINE(s);
    const MemMapEntry *memmap = leapfive_memmap;
    addr = memmap[LEAPFIVE_DRAM].base +
            riscv_socket_mem_offset(ms, numa);
    /* Calcute memory size */
    size = riscv_socket_mem_size(ms, numa);
    mem_name = g_strdup_printf("/memory@%lx", addr);
    qemu_fdt_add_subnode(ms->fdt, mem_name);
    qemu_fdt_setprop_sized_cells(ms->fdt, mem_name, "reg", 2, addr, 2, size);
    qemu_fdt_setprop_string(ms->fdt, mem_name, "device_type", "memory");
    riscv_socket_fdt_write_id(ms, mem_name, numa);
}

static void create_fdt_clint(LeapfiveState *s, int numa,
                            uint32_t *intc_phandles)
{
    int cpu;
    g_autofree char *clint_name = NULL;
    g_autofree uint32_t *clint_cells = NULL;
    unsigned long clint_addr;
    MachineState *ms = MACHINE(s);
    const MemMapEntry *memmap = leapfive_memmap;
    static const char * const clint_compat[2] = {
        "sifive,clint0", "riscv,clint0"
    };

    clint_cells = g_new0(uint32_t, s->soc[numa].num_harts * 4);

    for (cpu = 0; cpu < s->soc[numa].num_harts; cpu++) {
        clint_cells[cpu * 4 + 0] = cpu_to_be32(intc_phandles[cpu]);
        clint_cells[cpu * 4 + 1] = cpu_to_be32(IRQ_M_SOFT);
        clint_cells[cpu * 4 + 2] = cpu_to_be32(intc_phandles[cpu]);
        clint_cells[cpu * 4 + 3] = cpu_to_be32(IRQ_M_TIMER);
    }

    clint_addr = memmap[LEAPFIVE_CLINT].base +
                (memmap[LEAPFIVE_CLINT].size * numa);
    clint_name = g_strdup_printf("/soc/clint@%lx", clint_addr);
    qemu_fdt_add_subnode(ms->fdt, clint_name);
    qemu_fdt_setprop_string_array(ms->fdt, clint_name, "compatible",
                                (char **)&clint_compat,
                                ARRAY_SIZE(clint_compat));
    qemu_fdt_setprop_sized_cells(ms->fdt, clint_name, "reg",
                                2, clint_addr, 2,
                                memmap[LEAPFIVE_CLINT].size);
    qemu_fdt_setprop(ms->fdt, clint_name, "interrupts-extended",
                clint_cells, s->soc[numa].num_harts * sizeof(uint32_t) * 4);
    riscv_socket_fdt_write_id(ms, clint_name, numa);
}

static void create_fdt_plic(LeapfiveState *s, int numa,
                            uint32_t *phandle,
                            uint32_t *intc_phandles,
                            uint32_t *plic_phandles)
{
    int cpu;
    g_autofree char *plic_name = NULL;
    g_autofree uint32_t *plic_cells;
    unsigned long plic_addr;
    MachineState *ms = MACHINE(s);
    const MemMapEntry *memmap = leapfive_memmap;
    static const char * const plic_compat[2] = {
        "sifive,plic-1.0.0", "riscv,plic0"
    };

    plic_phandles[numa] = (*phandle)++;
    plic_addr = memmap[LEAPFIVE_PLIC].base +
                (memmap[LEAPFIVE_PLIC].size * numa);
    plic_name = g_strdup_printf("/soc/plic@%lx", plic_addr);
    qemu_fdt_add_subnode(ms->fdt, plic_name);
    qemu_fdt_setprop_cell(ms->fdt, plic_name,
                          "#interrupt-cells", FDT_PLIC_INT_CELLS);
    qemu_fdt_setprop_cell(ms->fdt, plic_name,
                          "#address-cells", FDT_PLIC_ADDR_CELLS);
    qemu_fdt_setprop_string_array(ms->fdt, plic_name, "compatible",
                                  (char **)&plic_compat,
                                  ARRAY_SIZE(plic_compat));
    qemu_fdt_setprop(ms->fdt, plic_name, "interrupt-controller", NULL, 0);

    if (kvm_enabled()) {
        plic_cells = g_new0(uint32_t, s->soc[numa].num_harts * 2);
        for (cpu = 0; cpu < s->soc[numa].num_harts; cpu++) {
            plic_cells[cpu * 2 + 0] = cpu_to_be32(intc_phandles[cpu]);
            plic_cells[cpu * 2 + 1] = cpu_to_be32(IRQ_S_EXT);
        }

        qemu_fdt_setprop(ms->fdt, plic_name, "interrupts-extended",
                         plic_cells,
                         s->soc[numa].num_harts * sizeof(uint32_t) * 2);
    } else {
        plic_cells = g_new0(uint32_t, s->soc[numa].num_harts * 4);
        for (cpu = 0; cpu < s->soc[numa].num_harts; cpu++) {
            plic_cells[cpu * 4 + 0] = cpu_to_be32(intc_phandles[cpu]);
            plic_cells[cpu * 4 + 1] = cpu_to_be32(IRQ_M_EXT);
            plic_cells[cpu * 4 + 2] = cpu_to_be32(intc_phandles[cpu]);
            plic_cells[cpu * 4 + 3] = cpu_to_be32(IRQ_S_EXT);
        }

        qemu_fdt_setprop(ms->fdt, plic_name, "interrupts-extended",
                         plic_cells,
                         s->soc[numa].num_harts * sizeof(uint32_t) * 4);
    }

    qemu_fdt_setprop_sized_cells(ms->fdt, plic_name, "reg",
                                 0x2, plic_addr, 0x2,
                                 memmap[LEAPFIVE_PLIC].size);
    qemu_fdt_setprop_cell(ms->fdt, plic_name, "riscv,ndev",
                          LEAPFIVE_IRQCHIP_NUM_SOURCES - 1);
    riscv_socket_fdt_write_id(ms, plic_name, numa);
    qemu_fdt_setprop_cell(ms->fdt, plic_name, "phandle",
                          plic_phandles[numa]);
}

static void create_fdt_sdhci(LeapfiveState *s,
                             uint32_t *phandle,
                             uint32_t plic_phandle) {
    MachineState *ms = MACHINE(s);
    uint32_t ahb_clk = (*phandle)++;
    uint32_t xin_clk = (*phandle)++;
    const MemMapEntry *memmap = leapfive_memmap;
    uint64_t base = memmap[LEAPFIVE_SDHCI].base;
    uint64_t size = memmap[LEAPFIVE_SDHCI].size;

    char *name = g_strdup_printf("/soc/sdhci_xin_clk");
    qemu_fdt_add_subnode(ms->fdt, name);
    qemu_fdt_setprop_cell(ms->fdt, name, "phandle", xin_clk);
    qemu_fdt_setprop_string(ms->fdt, name, "clock-output-names", "clk_xin");
    qemu_fdt_setprop_cell(ms->fdt, name, "clock-frequency",
                          LEAPFIVE_SDHCI_XIN_CLK);
    qemu_fdt_setprop_string(ms->fdt, name, "compatible", "fixed-clock");
    qemu_fdt_setprop_cell(ms->fdt, name, "#clock-cells", 0x0);

    name = g_strdup_printf("/soc/sdhci_ahb_clk");
    qemu_fdt_add_subnode(ms->fdt, name);
    qemu_fdt_setprop_cell(ms->fdt, name, "phandle", ahb_clk);
    qemu_fdt_setprop_string(ms->fdt, name, "clock-output-names", "clk_ahb");
    qemu_fdt_setprop_cell(ms->fdt, name, "clock-frequency",
                        LEAPFIVE_SDHCI_AHB_CLK);
    qemu_fdt_setprop_string(ms->fdt, name, "compatible", "fixed-clock");
    qemu_fdt_setprop_cell(ms->fdt, name, "#clock-cells", 0x0);

    name = g_strdup_printf("/soc/mmc@%lx", (unsigned long)base);
    qemu_fdt_add_subnode(ms->fdt, name);
    qemu_fdt_setprop_string(ms->fdt, name, "compatible", "arasan,sdhci-8.9a");
    qemu_fdt_setprop_sized_cells(ms->fdt, name, "reg",
                                 (uint32_t)(base >> 32),
                                 (uint32_t)base,
                                 (uint32_t)(base >> 32),
                                 (uint32_t)size);
    qemu_fdt_setprop_cell(ms->fdt, name, "interrupt-parent", plic_phandle);
    if (s->aia) {
        qemu_fdt_setprop_cells(ms->fdt, name, "interrupts",
                               LEAPFIVE_SDHCI_IRQ, 0x4);
    } else {
        qemu_fdt_setprop_cell(ms->fdt, name, "interrupts",
                              LEAPFIVE_SDHCI_IRQ);
    }

    qemu_fdt_setprop_cells(ms->fdt, name, "clocks",
                           ahb_clk, ahb_clk, xin_clk, xin_clk);
    {
        const char clk_names[] = "clk_xin\0clk_ahb";
        qemu_fdt_setprop(ms->fdt, name, "clock-names",
                         clk_names, sizeof(clk_names));
    }
    qemu_fdt_setprop_string(ms->fdt, name, "status", "okay");
    g_free(name);
}

static uint32_t leapfive_imsic_num_bits(uint32_t count)
{
    uint32_t ret = 0;
    while (BIT(ret) < count) {
        ret++;
    }
    return ret;
}

static void create_fdt_one_imsic(LeapfiveState *s, hwaddr base_addr,
                                uint32_t *intc_phandles,
                                uint32_t msi_phandle,
                                bool m_mode, uint32_t imsic_guest_bits) {
    int cpu;
    g_autofree char *imsic_name = NULL;
    MachineState *ms = MACHINE(s);
    int numa_count = riscv_socket_count(ms);
    uint32_t imsic_max_hart_per_socket, imsic_addr, imsic_size;
    g_autofree uint32_t *imsic_cells = NULL;
    g_autofree uint32_t *imsic_regs = NULL;
    static const char * const imsic_compat[2] = {
        "qemu,imsics", "riscv,imsics"
    };

    imsic_cells = g_new0(uint32_t, ms->smp.cpus * 2);
    imsic_regs = g_new0(uint32_t, numa_count * 4);

    for (cpu = 0; cpu < ms->smp.cpus; cpu++) {
        imsic_cells[cpu * 2 + 0] = cpu_to_be32(intc_phandles[cpu]);
        imsic_cells[cpu * 2 + 1] = cpu_to_be32(m_mode ? IRQ_M_EXT : IRQ_S_EXT);
    }

    imsic_max_hart_per_socket = 0;
    for (int numa = 0; numa < numa_count; ++numa) {
        imsic_addr = base_addr + numa * LEAPFIVE_IMSIC_GROUP_MAX_SIZE;
        imsic_size = IMSIC_HART_SIZE(imsic_guest_bits) *
                        s->soc[numa].num_harts;
        imsic_regs[numa * 4 + 0] = 0;
        imsic_regs[numa * 4 + 1] = cpu_to_be32(imsic_addr);
        imsic_regs[numa * 4 + 2] = 0;
        imsic_regs[numa * 4 + 3] = cpu_to_be32(imsic_size);
        if (imsic_max_hart_per_socket < s->soc[numa].num_harts) {
            imsic_max_hart_per_socket = s->soc[numa].num_harts;
        }
    }

    imsic_name = g_strdup_printf("/soc/interrupt-controller@%lx",
                                 (unsigned long)base_addr);
    qemu_fdt_add_subnode(ms->fdt, imsic_name);
    qemu_fdt_setprop_string_array(ms->fdt, imsic_name, "compatible",
                                  (char **)&imsic_compat,
                                  ARRAY_SIZE(imsic_compat));

    qemu_fdt_setprop_cell(ms->fdt, imsic_name, "#interrupt-cells",
                          FDT_IMSIC_INT_CELLS);
    qemu_fdt_setprop(ms->fdt, imsic_name, "interrupt-controller", NULL, 0);
    qemu_fdt_setprop(ms->fdt, imsic_name, "msi-controller", NULL, 0);
    qemu_fdt_setprop(ms->fdt, imsic_name, "interrupts-extended",
                     imsic_cells, ms->smp.cpus * sizeof(uint32_t) * 2);
    qemu_fdt_setprop(ms->fdt, imsic_name, "reg", imsic_regs,
                     numa_count * sizeof(uint32_t) * 4);
    qemu_fdt_setprop_cell(ms->fdt, imsic_name, "riscv,num-ids",
                          LEAPFIVE_IRQCHIP_NUM_MSIS);

    if (imsic_guest_bits) {
        qemu_fdt_setprop_cell(ms->fdt, imsic_name, "riscv,guest-index-bits",
                              imsic_guest_bits);
    }
    if (numa_count > 1) {
        qemu_fdt_setprop_cell(ms->fdt, imsic_name, "riscv,hart-index-bits",
                        leapfive_imsic_num_bits(imsic_max_hart_per_socket));
        qemu_fdt_setprop_cell(ms->fdt, imsic_name, "riscv,group-index-bits",
                        leapfive_imsic_num_bits(numa_count));
        qemu_fdt_setprop_cell(ms->fdt, imsic_name, "riscv,group-index-shift",
                        IMSIC_MMIO_GROUP_MIN_SHIFT);
    }
    qemu_fdt_setprop_cell(ms->fdt, imsic_name, "phandle", msi_phandle);
}

static void create_fdt_imsic(LeapfiveState *s, uint32_t *phandle,
                             uint32_t *intc_phandles,
                             uint32_t *msi_m_phandle,
                             uint32_t *msi_s_phandle)
{
    *msi_m_phandle = (*phandle)++;
    *msi_s_phandle = (*phandle)++;
    const MemMapEntry *memmap = leapfive_memmap;
    if (!kvm_enabled()) {
        /* M-level IMSIC node */
        create_fdt_one_imsic(s, memmap[LEAPFIVE_IMSIC_M].base,
                             intc_phandles, *msi_m_phandle, true, 0);
    }

    /* S-level IMSIC node */
    create_fdt_one_imsic(s, memmap[LEAPFIVE_IMSIC_S].base,
                         intc_phandles, *msi_s_phandle, false,
                         leapfive_imsic_num_bits(LEAPFIVE_NUM_GUESTS + 1));
}

/* Caller must free string after use */
static char *fdt_get_aplic_nodename(unsigned long aplic_addr)
{
    return g_strdup_printf("/soc/interrupt-controller@%lx", aplic_addr);
}

static void create_fdt_one_aplic(LeapfiveState *s, int numa,
                                 unsigned long aplic_addr,
                                 uint32_t aplic_size,
                                 uint32_t msi_phandle,
                                 uint32_t *intc_phandles,
                                 uint32_t aplic_phandle,
                                 uint32_t aplic_child_phandle,
                                 bool m_mode, int num_harts)
{
    int cpu;
    g_autofree char *aplic_name = fdt_get_aplic_nodename(aplic_addr);
    g_autofree uint32_t *aplic_cells = g_new0(uint32_t, num_harts * 2);
    MachineState *ms = MACHINE(s);
    static const char * const aplic_compat[2] = {
        "qemu,aplic", "riscv,aplic"
    };

    for (cpu = 0; cpu < num_harts; cpu++) {
        aplic_cells[cpu * 2 + 0] = cpu_to_be32(intc_phandles[cpu]);
        aplic_cells[cpu * 2 + 1] = cpu_to_be32(m_mode ?
                                    IRQ_M_EXT : IRQ_S_EXT);
    }

    qemu_fdt_add_subnode(ms->fdt, aplic_name);
    qemu_fdt_setprop_string_array(ms->fdt, aplic_name, "compatible",
                                  (char **)&aplic_compat,
                                  ARRAY_SIZE(aplic_compat));
    qemu_fdt_setprop_cell(ms->fdt, aplic_name, "#address-cells",
                          FDT_APLIC_ADDR_CELLS);
    qemu_fdt_setprop_cell(ms->fdt, aplic_name, "#interrupt-cells",
                          FDT_APLIC_INT_CELLS);
    qemu_fdt_setprop(ms->fdt, aplic_name, "interrupt-controller", NULL, 0);
    qemu_fdt_setprop_cell(ms->fdt, aplic_name, "msi-parent", msi_phandle);
    qemu_fdt_setprop_sized_cells(ms->fdt, aplic_name, "reg",
                                 0x2, aplic_addr, 0x2, aplic_size);
    qemu_fdt_setprop_cell(ms->fdt, aplic_name, "riscv,num-sources",
                          LEAPFIVE_IRQCHIP_NUM_SOURCES);

    if (aplic_child_phandle) {
        qemu_fdt_setprop_cell(ms->fdt, aplic_name, "riscv,children",
                              aplic_child_phandle);
        qemu_fdt_setprop_cells(ms->fdt, aplic_name, "riscv,delegation",
                               aplic_child_phandle, 0x1,
                               LEAPFIVE_IRQCHIP_NUM_SOURCES);
        /*
         * DEPRECATED_9.1: Compat property kept temporarily
         * to allow old firmwares to work with AIA. Do *not*
         * use 'riscv,delegate' in new code: use
         * 'riscv,delegation' instead.
         */
        qemu_fdt_setprop_cells(ms->fdt, aplic_name, "riscv,delegate",
                               aplic_child_phandle, 0x1,
                               LEAPFIVE_IRQCHIP_NUM_SOURCES);
    }
    riscv_socket_fdt_write_id(ms, aplic_name, numa);
    qemu_fdt_setprop_cell(ms->fdt, aplic_name, "phandle", aplic_phandle);
}

static void create_fdt_aplic(LeapfiveState *s, int numa,
                             uint32_t msi_m_phandle,
                             uint32_t msi_s_phandle,
                             uint32_t *phandle,
                             uint32_t *intc_phandles,
                             uint32_t *aplic_phandles)
{
    unsigned long aplic_addr;
    uint32_t aplic_m_phandle, aplic_s_phandle;
    const MemMapEntry *memmap = leapfive_memmap;
    aplic_m_phandle = (*phandle)++;
    aplic_s_phandle = (*phandle)++;

    if (!kvm_enabled()) {
        /* M-level APLIC node */
        aplic_addr = memmap[LEAPFIVE_APLIC_M].base +
                        (memmap[LEAPFIVE_APLIC_M].size * numa);
        create_fdt_one_aplic(s, numa, aplic_addr,
                             memmap[LEAPFIVE_APLIC_M].size,
                             msi_m_phandle, intc_phandles,
                             aplic_m_phandle, aplic_s_phandle,
                             true, s->soc[numa].num_harts);
    }

    /* S-level APLIC node */
    aplic_addr = memmap[LEAPFIVE_APLIC_S].base +
                    (memmap[LEAPFIVE_APLIC_S].size * numa);
    create_fdt_one_aplic(s, numa, aplic_addr,
                         memmap[LEAPFIVE_APLIC_S].size,
                         msi_s_phandle, intc_phandles,
                         aplic_s_phandle, 0,
                         false, s->soc[numa].num_harts);
    aplic_phandles[numa] = aplic_s_phandle;
}

static void create_fdt_pmu(LeapfiveState *s)
{
    g_autofree char *pmu_name = g_strdup_printf("/pmu");
    MachineState *ms = MACHINE(s);
    RISCVCPU hart = s->soc[0].harts[0];

    qemu_fdt_add_subnode(ms->fdt, pmu_name);
    qemu_fdt_setprop_string(ms->fdt, pmu_name, "compatible", "riscv,pmu");
    riscv_pmu_generate_fdt_node(ms->fdt, hart.pmu_avail_ctrs, pmu_name);
}

static void create_fdt_sockets(LeapfiveState *s,
                               uint32_t *phandle,
                               uint32_t *irq_mmio_phandle,
                               uint32_t *irq_pcie_phandle,
                               uint32_t *irq_virtio_phandle,
                               uint32_t *msi_pcie_phandle,
                               uint32_t *cpu_phandles)
{
    MachineState *ms = MACHINE(s);
    uint32_t msi_m_phandle = 0, msi_s_phandle = 0;
    uint32_t xplic_phandles[MAX_NODES];
    g_autofree uint32_t *intc_phandles = NULL;
    int numa;
    int numa_count = riscv_socket_count(ms);

    qemu_fdt_add_subnode(ms->fdt, "/cpus");
    qemu_fdt_setprop_cell(ms->fdt, "/cpus", "timebase-frequency",
                        kvm_enabled() ?
                        kvm_riscv_get_timebase_frequency(&s->soc->harts[0]) :
                        RISCV_ACLINT_DEFAULT_TIMEBASE_FREQ);
    qemu_fdt_setprop_cell(ms->fdt, "/cpus", "#size-cells", 0x0);
    qemu_fdt_setprop_cell(ms->fdt, "/cpus", "#address-cells", 0x1);
    qemu_fdt_add_subnode(ms->fdt, "/cpus/cpu-map");

    intc_phandles = g_new0(uint32_t, ms->smp.cpus);
    int phandle_pos = ms->smp.cpus;
    for (numa = (numa_count - 1); numa >= 0; numa--) {
        g_autofree char *clust_name =
            g_strdup_printf("/cpus/cpu-map/cluster%d", numa);
        phandle_pos -= s->soc[numa].num_harts;
        qemu_fdt_add_subnode(ms->fdt, clust_name);
        create_fdt_cpus(s, numa, clust_name, phandle,
                        &intc_phandles[phandle_pos],
                        &cpu_phandles[phandle_pos]);
        create_fdt_memory(s, numa);
        create_fdt_clint(s, numa, &intc_phandles[phandle_pos]);
    }

    if (s->aia) {
        create_fdt_imsic(s, phandle, intc_phandles,
                         &msi_m_phandle, &msi_s_phandle);
        *msi_pcie_phandle = msi_s_phandle;
    }

    /*
     * With KVM AIA aplic-imsic, using an irqchip without split
     * mode, we'll use only one APLIC instance.
     */
    if (!riscv_use_emulated_aplic(s->aia)) {
        create_fdt_aplic(s, 0, msi_m_phandle, msi_s_phandle,
                         phandle, &intc_phandles[0], xplic_phandles);
        *irq_mmio_phandle = xplic_phandles[0];
        *irq_virtio_phandle = xplic_phandles[0];
        *irq_pcie_phandle = xplic_phandles[0];
    } else {
        phandle_pos = ms->smp.cpus;
        for (numa = (numa_count - 1); numa >= 0; numa--) {
            phandle_pos -= s->soc[numa].num_harts;
            if (s->aia) {
                create_fdt_aplic(s, numa, msi_m_phandle,
                                 msi_s_phandle, phandle,
                                 &intc_phandles[phandle_pos],
                                 xplic_phandles);
            } else {
                create_fdt_plic(s, numa, phandle,
                                &intc_phandles[phandle_pos],
                                xplic_phandles);
            }
        }

        for (numa = 0; numa < numa_count; numa++) {
            if (numa == 0) {
                *irq_mmio_phandle = xplic_phandles[numa];
                *irq_virtio_phandle = xplic_phandles[numa];
                *irq_pcie_phandle = xplic_phandles[numa];
            }
            if (numa == 1) {
                *irq_virtio_phandle = xplic_phandles[numa];
                *irq_pcie_phandle = xplic_phandles[numa];
            }
            if (numa == 2) {
                *irq_pcie_phandle = xplic_phandles[numa];
            }
        }
    }

    riscv_socket_fdt_write_distance_matrix(ms);
}

static void create_fdt_virtio(LeapfiveState *s,
                              uint32_t irq_virtio_phandle)
{
    MachineState *ms = MACHINE(s);
    const MemMapEntry *memmap = leapfive_memmap;
    for (int i = 0; i < LEAPFIVE_VIRTIO_COUNT; ++i) {
        g_autofree char *name = NULL;
        uint64_t size = memmap[LEAPFIVE_VIRTIO].size;
        hwaddr addr = memmap[LEAPFIVE_VIRTIO].base + i * size;
        name = g_strdup_printf("/soc/virtio_mmio@%lx", addr);
        qemu_fdt_add_subnode(ms->fdt, name);
        qemu_fdt_setprop_string(ms->fdt, name, "compatible", "virtio,mmio");
        qemu_fdt_setprop_sized_cells(ms->fdt, name, "reg",
                                    0x2, addr, 0x2, size);
        qemu_fdt_setprop_cell(ms->fdt, name, "interrupt-parent",
                            irq_virtio_phandle);
        if (s->aia) {
            qemu_fdt_setprop_cells(ms->fdt, name, "interrupts",
                                   LEAPFIVE_VIRTIO_IRQ + i, 0x4);
        } else {
            qemu_fdt_setprop_cell(ms->fdt, name, "interrupts",
                                  LEAPFIVE_VIRTIO_IRQ + i);
        }
    }
}

static void create_fdt_pcie(LeapfiveState *s,
                            uint32_t irq_pcie_phandle,
                            uint32_t msi_pcie_phandle,
                            uint32_t iommu_sys_phandle)
{
    g_autofree char *name = NULL;
    MachineState *ms = MACHINE(s);
    const MemMapEntry *memmap = leapfive_memmap;
    name = g_strdup_printf("/soc/pci@%lx",
        (long) memmap[LEAPFIVE_PCIE_ECAM].base);
    qemu_fdt_setprop_cell(ms->fdt, name, "#address-cells",
        FDT_PCI_ADDR_CELLS);
    qemu_fdt_setprop_cell(ms->fdt, name, "#interrupt-cells",
        FDT_PCI_INT_CELLS);
    qemu_fdt_setprop_cell(ms->fdt, name, "#size-cells", 0x2);
    qemu_fdt_setprop_string(ms->fdt, name, "compatible",
        "pci-host-ecam-generic");
    qemu_fdt_setprop_string(ms->fdt, name, "device_type", "pci");
    qemu_fdt_setprop_cell(ms->fdt, name, "linux,pci-domain", 0);
    qemu_fdt_setprop_cells(ms->fdt, name, "bus-range", 0,
        memmap[LEAPFIVE_PCIE_ECAM].size / PCIE_MMCFG_SIZE_MIN - 1);
    qemu_fdt_setprop(ms->fdt, name, "dma-coherent", NULL, 0);
    if (s->aia) {
        qemu_fdt_setprop_cell(ms->fdt, name, "msi-parent", msi_pcie_phandle);
    }

    qemu_fdt_setprop_sized_cells(ms->fdt, name, "reg",
                                2, memmap[LEAPFIVE_PCIE_ECAM].base,
                                2, memmap[LEAPFIVE_PCIE_ECAM].size);
    qemu_fdt_setprop_sized_cells(ms->fdt, name, "ranges",
                                1, FDT_PCI_RANGE_IOPORT, 2, 0,
                                2, memmap[LEAPFIVE_PCIE_PIO].base,
                                2, memmap[LEAPFIVE_PCIE_PIO].size,
                                1, FDT_PCI_RANGE_MMIO,
                                2, memmap[LEAPFIVE_PCIE_MMIO].base,
                                2, memmap[LEAPFIVE_PCIE_MMIO].base,
                                2, memmap[LEAPFIVE_PCIE_MMIO].size,
                                1, FDT_PCI_RANGE_MMIO_64BIT,
                                2, memmap[LEAPFIVE_PCIE_MMIO_HIGH].base,
                                2, memmap[LEAPFIVE_PCIE_MMIO_HIGH].base,
                                2, memmap[LEAPFIVE_PCIE_MMIO_HIGH].size);
    if (s->iommu_sys) {
        qemu_fdt_setprop_cells(ms->fdt, name, "iommu-map",
                               0, iommu_sys_phandle, 0, 0, 0,
                               iommu_sys_phandle, 0, 0xffff);
    }
    create_pcie_irq_map(s, ms->fdt, name, irq_pcie_phandle);
}

static void create_fdt_reset(LeapfiveState *s,
                             uint32_t *phandle)
{
    uint32_t test_phandle;
    MachineState *ms = MACHINE(s);
    const MemMapEntry *memmap = leapfive_memmap;
    test_phandle = (*phandle)++;
    g_autofree char *name = g_strdup_printf("/soc/test@%lx",
                                (long)memmap[LEAPFIVE_TEST].base);
    qemu_fdt_add_subnode(ms->fdt, name);
    {
        static const char * const compat[3] = {
            "sifive,test1", "sifive,test0", "syscon"
        };
        qemu_fdt_setprop_string_array(ms->fdt, name, "compatible",
                                    (char **)&compat, ARRAY_SIZE(compat));
    }
    qemu_fdt_setprop_sized_cells(ms->fdt, name, "reg",
                                 0x2, memmap[LEAPFIVE_TEST].base,
                                 0x2, memmap[LEAPFIVE_TEST].size);
    qemu_fdt_setprop_cell(ms->fdt, name, "phandle", test_phandle);
    test_phandle = qemu_fdt_get_phandle(ms->fdt, name);
    name = g_strdup_printf("/reboot");
    qemu_fdt_add_subnode(ms->fdt, name);
    qemu_fdt_setprop_string(ms->fdt, name, "compatible", "syscon-reboot");
    qemu_fdt_setprop_cell(ms->fdt, name, "regmap", test_phandle);
    qemu_fdt_setprop_cell(ms->fdt, name, "offset", 0x0);
    qemu_fdt_setprop_cell(ms->fdt, name, "value", FINISHER_RESET);

    name = g_strdup_printf("/poweroff");
    qemu_fdt_add_subnode(ms->fdt, name);
    qemu_fdt_setprop_string(ms->fdt, name, "compatible", "syscon-poweroff");
    qemu_fdt_setprop_cell(ms->fdt, name, "regmap", test_phandle);
    qemu_fdt_setprop_cell(ms->fdt, name, "offset", 0x0);
    qemu_fdt_setprop_cell(ms->fdt, name, "value", FINISHER_PASS);
}

static void create_fdt_uart(LeapfiveState *s,
                            uint32_t irq_mmio_phandle)
{
    g_autofree char *name = NULL;
    MachineState *ms = MACHINE(s);
    const MemMapEntry *memmap = leapfive_memmap;
    name = g_strdup_printf("/soc/serial@%lx",
            (long)memmap[LEAPFIVE_UART0].base);
    qemu_fdt_add_subnode(ms->fdt, name);
    qemu_fdt_setprop_string(ms->fdt, name, "compatible", "ns16550a");
    qemu_fdt_setprop_sized_cells(ms->fdt, name, "reg",
                                0x2, memmap[LEAPFIVE_UART0].base,
                                0x2, memmap[LEAPFIVE_UART0].size);
    qemu_fdt_setprop_cell(ms->fdt, name, "clock-frequency",
                        LEAPFIVE_UART_REF_CLK);
    qemu_fdt_setprop_cell(ms->fdt, name, "interrupt-parent",
                        irq_mmio_phandle);
    if (s->aia) {
        qemu_fdt_setprop_cells(ms->fdt, name, "interrupts",
                               LEAPFIVE_UART0_IRQ, 0x4);
    } else {
        qemu_fdt_setprop_cell(ms->fdt, name, "interrupts",
                              LEAPFIVE_UART0_IRQ);
    }
    qemu_fdt_setprop_string(ms->fdt, "/chosen", "stdout-path", name);
    qemu_fdt_setprop_string(ms->fdt, "/aliases", "serial0", name);
}

static void create_fdt_rtc(LeapfiveState *s, uint32_t irq_mmio_phandle)
{
    g_autofree char *name = NULL;
    MachineState *ms = MACHINE(s);
    const MemMapEntry *memmap = leapfive_memmap;
    name = g_strdup_printf("/soc/rtc@%lx",
            (long)memmap[LEAPFIVE_RTC].base);
    qemu_fdt_add_subnode(ms->fdt, name);
    qemu_fdt_setprop_string(ms->fdt, name, "compatible",
        "google,goldfish-rtc");
    qemu_fdt_setprop_sized_cells(ms->fdt, name, "reg",
                                0x2, memmap[LEAPFIVE_RTC].base,
                                0x2, memmap[LEAPFIVE_RTC].size);
    qemu_fdt_setprop_cell(ms->fdt, name, "interrupt-parent",
        irq_mmio_phandle);
    if (s->aia) {
        qemu_fdt_setprop_cells(ms->fdt, name, "interrupts",
                               LEAPFIVE_RTC_IRQ, 0x4);
    } else {
        qemu_fdt_setprop_cell(ms->fdt, name, "interrupts", LEAPFIVE_RTC_IRQ);
    }
}

static void create_fdt_iommu_sys(LeapfiveState *s, uint32_t irq_chip,
                                 uint32_t msi_phandle,
                                 uint32_t *iommu_sys_phandle)
{
    const char comp[] = "riscv,iommu";
    void *fdt = MACHINE(s)->fdt;
    uint32_t iommu_phandle;
    g_autofree char *iommu_node = NULL;
    const MemMapEntry *memmap = leapfive_memmap;
    hwaddr addr = memmap[LEAPFIVE_IOMMU_SYS].base;
    hwaddr size = memmap[LEAPFIVE_IOMMU_SYS].size;
    uint32_t iommu_irq_map[RISCV_IOMMU_INTR_COUNT] = {
        LEAPFIVE_IOMMU_SYS_IRQ + RISCV_IOMMU_INTR_CQ,
        LEAPFIVE_IOMMU_SYS_IRQ + RISCV_IOMMU_INTR_FQ,
        LEAPFIVE_IOMMU_SYS_IRQ + RISCV_IOMMU_INTR_PM,
        LEAPFIVE_IOMMU_SYS_IRQ + RISCV_IOMMU_INTR_PQ,
    };

    iommu_node = g_strdup_printf("/soc/iommu@%x",
                (unsigned int) memmap[LEAPFIVE_IOMMU_SYS].base);
    iommu_phandle = qemu_fdt_alloc_phandle(fdt);
    qemu_fdt_add_subnode(fdt, iommu_node);

    qemu_fdt_setprop(fdt, iommu_node, "compatible", comp, sizeof(comp));
    qemu_fdt_setprop_cell(fdt, iommu_node, "#iommu-cells", 1);
    qemu_fdt_setprop_cell(fdt, iommu_node, "phandle", iommu_phandle);
    qemu_fdt_setprop_sized_cells(fdt, iommu_node, "reg", 2, addr, 2, size);
    qemu_fdt_setprop_cell(fdt, iommu_node, "interrupt-parent", irq_chip);
    qemu_fdt_setprop_cells(fdt, iommu_node, "interrupts",
        iommu_irq_map[0], FDT_IRQ_TYPE_EDGE_LOW,
        iommu_irq_map[1], FDT_IRQ_TYPE_EDGE_LOW,
        iommu_irq_map[2], FDT_IRQ_TYPE_EDGE_LOW,
        iommu_irq_map[3], FDT_IRQ_TYPE_EDGE_LOW);

    qemu_fdt_setprop_cell(fdt, iommu_node, "msi-parent", msi_phandle);
    *iommu_sys_phandle = iommu_phandle;
}

static void create_fdt(LeapfiveState *s)
{
    MachineState *ms = MACHINE(s);
    g_autofree char *name = NULL;
    uint32_t phandle = 1, irq_mmio_phandle = 1, msi_pcie_phandle = 1;
    uint32_t irq_pcie_phandle = 1, irq_virtio_phandle = 1;
    uint32_t iommu_sys_phandle = 1, *cpu_phandles;
    const MemMapEntry *memmap = leapfive_memmap;
    cpu_phandles = g_new0(uint32_t, ms->smp.cpus);
    ms->fdt = create_device_tree(&s->fdt_size);
    if (!ms->fdt) {
        error_report("create_device_tree() failed");
        exit(1);
    }

    qemu_fdt_setprop_string(ms->fdt, "/", "model", "leapfive-rva23-base");
    qemu_fdt_setprop_string(ms->fdt, "/", "compatible",
                            "leapfive,rva23-base");
    qemu_fdt_setprop_cell(ms->fdt, "/", "#size-cells", 0x2);
    qemu_fdt_setprop_cell(ms->fdt, "/", "#address-cells", 0x2);
    qemu_fdt_add_subnode(ms->fdt, "/soc");
    qemu_fdt_setprop(ms->fdt, "/soc", "ranges", NULL, 0);
    qemu_fdt_setprop_string(ms->fdt, "/soc", "compatible", "simple-bus");
    qemu_fdt_setprop_cell(ms->fdt, "/soc", "#size-cells", 0x2);
    qemu_fdt_setprop_cell(ms->fdt, "/soc", "#address-cells", 0x2);

    name = g_strdup_printf("/soc/pci@%lx", memmap[LEAPFIVE_PCIE_ECAM].base);
    qemu_fdt_add_subnode(ms->fdt, name);
    qemu_fdt_add_subnode(ms->fdt, "/chosen");
    qemu_fdt_add_subnode(ms->fdt, "/aliases");
    create_fdt_sockets(s, &phandle, &irq_mmio_phandle,
                       &irq_pcie_phandle, &irq_virtio_phandle,
                       &msi_pcie_phandle, cpu_phandles);
    create_fdt_virtio(s, irq_virtio_phandle);

    if (s->iommu_sys) {
        create_fdt_iommu_sys(s, irq_mmio_phandle, msi_pcie_phandle,
                             &iommu_sys_phandle);
    }
    create_fdt_pcie(s, irq_pcie_phandle, msi_pcie_phandle,
                    iommu_sys_phandle);

    create_fdt_uart(s, irq_mmio_phandle);

    create_fdt_rtc(s, irq_mmio_phandle);

    create_fdt_sdhci(s, &phandle, irq_mmio_phandle);

    create_fdt_pmu(s);

    create_fdt_reset(s, &phandle);

    g_free(cpu_phandles);
}

static inline DeviceState *gpex_pcie_init(MemoryRegion *sys_mem,
                                          DeviceState *irqchip,
                                          LeapfiveState *s)
{
    DeviceState *dev;
    MemoryRegion *ecam_alias, *ecam_reg;
    MemoryRegion *mmio_alias, *high_mmio_alias, *mmio_reg;
    MemoryRegion *system_memory = get_system_memory();
    const MemMapEntry *memmap = leapfive_memmap;
    hwaddr ecam_base = memmap[LEAPFIVE_PCIE_ECAM].base;
    hwaddr ecam_size = memmap[LEAPFIVE_PCIE_ECAM].size;
    hwaddr mmio_base = memmap[LEAPFIVE_PCIE_MMIO].base;
    hwaddr mmio_size = memmap[LEAPFIVE_PCIE_MMIO].size;
    hwaddr high_mmio_base = memmap[LEAPFIVE_PCIE_MMIO_HIGH].base;
    hwaddr high_mmio_size = memmap[LEAPFIVE_PCIE_MMIO_HIGH].size;
    hwaddr pio_base = memmap[LEAPFIVE_PCIE_PIO].base;
    hwaddr pio_size = memmap[LEAPFIVE_PCIE_PIO].size;
    qemu_irq irq;
    int i;

    dev = qdev_new(TYPE_GPEX_HOST);
    /* Set GPEX object properties for the leapfive machine */
    object_property_set_uint(OBJECT(dev), PCI_HOST_ECAM_BASE,
                            ecam_base, NULL);
    object_property_set_int(OBJECT(dev), PCI_HOST_ECAM_SIZE,
                            ecam_size, NULL);
    object_property_set_uint(OBJECT(dev), PCI_HOST_BELOW_4G_MMIO_BASE,
                            mmio_base, NULL);
    object_property_set_int(OBJECT(dev), PCI_HOST_BELOW_4G_MMIO_SIZE,
                            mmio_size, NULL);
    object_property_set_uint(OBJECT(dev), PCI_HOST_ABOVE_4G_MMIO_BASE,
                            high_mmio_base, NULL);
    object_property_set_int(OBJECT(dev), PCI_HOST_ABOVE_4G_MMIO_SIZE,
                            high_mmio_size, NULL);
    object_property_set_uint(OBJECT(dev), PCI_HOST_PIO_BASE,
                            pio_base, NULL);
    object_property_set_int(OBJECT(dev), PCI_HOST_PIO_SIZE,
                            pio_size, NULL);

    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    ecam_alias = g_new0(MemoryRegion, 1);
    ecam_reg = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0);
    memory_region_init_alias(ecam_alias, OBJECT(dev), "pcie-ecam",
                            ecam_reg, 0, ecam_size);
    memory_region_add_subregion(system_memory, ecam_base, ecam_alias);

    mmio_alias = g_new0(MemoryRegion, 1);
    mmio_reg = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 1);
    memory_region_init_alias(mmio_alias, OBJECT(dev), "pcie-mmio",
                            mmio_reg, mmio_base, mmio_size);
    memory_region_add_subregion(system_memory, mmio_base, mmio_alias);

    /* Map high MMIO space */
    high_mmio_alias = g_new0(MemoryRegion, 1);
    memory_region_init_alias(high_mmio_alias, OBJECT(dev), "pcie-mmio-high",
                            mmio_reg, high_mmio_base, high_mmio_size);
    memory_region_add_subregion(system_memory, high_mmio_base,
                                high_mmio_alias);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 2, pio_base);

    for (i = 0; i < PCI_NUM_PINS; i++) {
        irq = qdev_get_gpio_in(irqchip, LEAPFIVE_PCIE_IRQ + i);
        sysbus_connect_irq(SYS_BUS_DEVICE(dev), i, irq);
        gpex_set_irq_num(GPEX_HOST(dev), i, LEAPFIVE_PCIE_IRQ + i);
    }

    GPEX_HOST(dev)->gpex_cfg.bus = PCI_HOST_BRIDGE(dev)->bus;
    return dev;
}

static DeviceState *leapfive_create_plic(int numa,
                                        int base_hartid,
                                        int hart_count)
{
    g_autofree char *plic_hart_config = NULL;
    const MemMapEntry *memmap = leapfive_memmap;
    /* Per-socket PLIC hart topology configuration string */
    plic_hart_config = riscv_plic_hart_config_string(hart_count);

    /* Per-socket PLIC */
    return sifive_plic_create(
            memmap[LEAPFIVE_PLIC].base + numa * memmap[LEAPFIVE_PLIC].size,
            plic_hart_config, hart_count, base_hartid,
            LEAPFIVE_IRQCHIP_NUM_SOURCES,
            ((1U << LEAPFIVE_IRQCHIP_NUM_PRIO_BITS) - 1),
            LEAPFIVE_PLIC_PRIORITY_BASE,
            LEAPFIVE_PLIC_PENDING_BASE,
            LEAPFIVE_PLIC_ENABLE_BASE,
            LEAPFIVE_PLIC_ENABLE_STRIDE,
            LEAPFIVE_PLIC_CONTEXT_BASE,
            LEAPFIVE_PLIC_CONTEXT_STRIDE,
            memmap[LEAPFIVE_PLIC].size);
}

static DeviceState *leapfive_create_imsic(int numa,
                                          int base_hartid,
                                          int hart_count)
{
    int i;
    hwaddr addr = 0;
    uint32_t guest_bits;
    DeviceState *aplic_s = NULL;
    DeviceState *aplic_m = NULL;

    const MemMapEntry *memmap = leapfive_memmap;
    if (!kvm_enabled()) {
        /* Per-socket M-level IMSICs */
        addr = memmap[LEAPFIVE_IMSIC_M].base +
                numa * LEAPFIVE_IMSIC_GROUP_MAX_SIZE;
        for (i = 0; i < hart_count; i++) {
            riscv_imsic_create(addr + i * IMSIC_HART_SIZE(0),
                            base_hartid + i, true, 1,
                            LEAPFIVE_IRQCHIP_NUM_MSIS);
        }
    } else {
        /* Per-socket S-level IMSICs */
        guest_bits = leapfive_imsic_num_bits(LEAPFIVE_NUM_GUESTS + 1);
        addr = memmap[LEAPFIVE_IMSIC_S].base +
                numa * LEAPFIVE_IMSIC_GROUP_MAX_SIZE;
        for (i = 0; i < hart_count; i++) {
            riscv_imsic_create(addr + i * IMSIC_HART_SIZE(guest_bits),
                            base_hartid + i, false, 1 + LEAPFIVE_NUM_GUESTS,
                            LEAPFIVE_IRQCHIP_NUM_MSIS);
        }
    }

    if (!kvm_enabled()) {
        /* Per-socket M-level APLIC */
        aplic_m = riscv_aplic_create(memmap[LEAPFIVE_APLIC_M].base +
                                     numa * memmap[LEAPFIVE_APLIC_M].size,
                                     memmap[LEAPFIVE_APLIC_M].size,
                                     0, 0 ,
                                     LEAPFIVE_IRQCHIP_NUM_SOURCES,
                                     LEAPFIVE_IRQCHIP_NUM_PRIO_BITS,
                                     true, true, NULL);
    }

    /* Per-socket S-level APLIC */
    aplic_s = riscv_aplic_create(memmap[LEAPFIVE_APLIC_S].base +
                                numa * memmap[LEAPFIVE_APLIC_S].size,
                                memmap[LEAPFIVE_APLIC_S].size,
                                0, 0 ,
                                LEAPFIVE_IRQCHIP_NUM_SOURCES,
                                LEAPFIVE_IRQCHIP_NUM_PRIO_BITS,
                                true, false, aplic_m);
    if (kvm_enabled()) {
        riscv_aplic_set_kvm_msicfgaddr(RISCV_APLIC(aplic_s), addr);
    }

    return kvm_enabled() ? aplic_s : aplic_m;
}

static void leapfive_machine_done(Notifier *notifier, void *data)
{
    LeapfiveState *s = container_of(notifier, LeapfiveState,
                                    machine_done);
    MachineState *machine = MACHINE(s);
    const MemMapEntry *memmap = leapfive_memmap;
    hwaddr start_addr = memmap[LEAPFIVE_DRAM].base;
    target_ulong firmware_end_addr, kernel_start_addr;
    const char *firmware_name = riscv_default_firmware_name(&s->soc[0]);
    uint64_t fdt_load_addr;
    uint64_t kernel_entry = 0;
    RISCVBootInfo boot_info;
    /*
     * Only direct boot kernel is currently supported for KVM VM,
     * so the "-bios" parameter is not supported when KVM is enabled.
     */
    if (kvm_enabled()) {
        if (machine->firmware) {
            if (strcmp(machine->firmware, "none")) {
                error_report("Machine mode firmware is not supported in "
                            "combination with KVM.");
                exit(1);
            }
        } else {
            machine->firmware = g_strdup("none");
        }
    }

    firmware_end_addr = riscv_find_and_load_firmware(machine, firmware_name,
                                                    &start_addr, NULL);
    riscv_boot_info_init(&boot_info, &s->soc[0]);
    if (machine->kernel_filename && !kernel_entry) {
        kernel_start_addr = riscv_calc_kernel_start_addr(&boot_info,
                                                        firmware_end_addr);
        riscv_load_kernel(machine, &boot_info, kernel_start_addr, true, NULL);
        kernel_entry = boot_info.image_low_addr;
    }

    fdt_load_addr = riscv_compute_fdt_addr(memmap[LEAPFIVE_DRAM].base,
                                            memmap[LEAPFIVE_DRAM].size,
                                            machine, &boot_info);
    riscv_load_fdt(fdt_load_addr, machine->fdt);

    /* load the reset vector */
    riscv_setup_rom_reset_vec(machine, &s->soc[0], start_addr,
                            memmap[LEAPFIVE_MROM].base,
                            memmap[LEAPFIVE_MROM].size, kernel_entry,
                            fdt_load_addr);

    /*
     * Only direct boot kernel is currently supported for KVM VM,
     * So here setup kernel start address and fdt address.
     * TODO:Support firmware loading and integrate to TCG start
     */
    if (kvm_enabled()) {
        riscv_setup_direct_kernel(kernel_entry, fdt_load_addr);
    }
}

static void leapfive_machine_init(MachineState *machine)
{
    LeapfiveState *s = LEAPFIVE_MACHINE(machine);
    MemoryRegion *system_memory = get_system_memory();
    MemoryRegion *mask_rom = g_new(MemoryRegion, 1);
    DeviceState *mmio_irqchip, *pcie_irqchip, *virtio_irqchip;
    int i, base_hartid, hart_count;
    int numa_count = riscv_socket_count(machine);
    const MemMapEntry *memmap = leapfive_memmap;
    /* Check numa node limit */
    if (LEAPFIVE_NUMA_MAX < numa_count) {
        error_report("number of nodes should be less than %d",
                    LEAPFIVE_NUMA_MAX);
        exit(1);
    }

    /* Initialize sockets */
    mmio_irqchip = virtio_irqchip = pcie_irqchip = NULL;
    for (i = 0; i < numa_count; ++i) {
        g_autofree char *soc_name = g_strdup_printf("soc%d", i);
        if (!riscv_socket_check_hartids(machine, i)) {
            error_report("discontinuous hartids in socket%d", i);
            exit(1);
        }

        base_hartid = riscv_socket_first_hartid(machine, i);
        if (base_hartid < 0) {
            error_report("can't find hartid base for socket%d", i);
            exit(1);
        }
        hart_count = riscv_socket_hart_count(machine, i);
        if (hart_count < 0) {
            error_report("can't find hart count for socket%d", i);
            exit(1);
        }

        object_initialize_child(OBJECT(machine), soc_name, &s->soc[i],
                                TYPE_RISCV_HART_ARRAY);
        object_property_set_str(OBJECT(&s->soc[i]), "cpu-type",
                                machine->cpu_type, &error_abort);
        object_property_set_int(OBJECT(&s->soc[i]), "hartid-base",
                                base_hartid, &error_abort);
        object_property_set_int(OBJECT(&s->soc[i]), "num-harts",
                                hart_count, &error_abort);
        sysbus_realize(SYS_BUS_DEVICE(&s->soc[i]), &error_fatal);
        /* Per-socket SiFive CLINT */
        riscv_aclint_swi_create(memmap[LEAPFIVE_CLINT].base +
                                i * memmap[LEAPFIVE_CLINT].size,
                                base_hartid, hart_count, false);
        riscv_aclint_mtimer_create(
            memmap[LEAPFIVE_CLINT].base +
            i * memmap[LEAPFIVE_CLINT].size +
            RISCV_ACLINT_SWI_SIZE,
            RISCV_ACLINT_DEFAULT_MTIMER_SIZE, base_hartid, hart_count,
            RISCV_ACLINT_DEFAULT_MTIMECMP, RISCV_ACLINT_DEFAULT_MTIME,
            LEAPFIVE_ACLINT_DEFAULT_TIMEBASE_FREQ, true);

        /* Per-socket interrupt controller */
        if (s->aia) {
            s->irqchip[i] = leapfive_create_imsic(i, base_hartid, hart_count);
        } else {
            s->irqchip[i] = leapfive_create_plic(i, base_hartid, hart_count);
        }
        /* Try to use different IRQCHIP instance based device type */
        if (i == 0) {
            mmio_irqchip = s->irqchip[i];
            virtio_irqchip = s->irqchip[i];
            pcie_irqchip = s->irqchip[i];
        }
        if (i == 1) {
            virtio_irqchip = s->irqchip[i];
            pcie_irqchip = s->irqchip[i];
        }
        if (i == 2) {
            pcie_irqchip = s->irqchip[i];
        }
    }

    if (kvm_enabled() && riscv_is_kvm_aia_aplic_imsic(s->aia)) {
        kvm_riscv_aia_create(machine, IMSIC_MMIO_GROUP_MIN_SHIFT,
                             LEAPFIVE_IRQCHIP_NUM_SOURCES,
                             LEAPFIVE_IRQCHIP_NUM_MSIS,
                             memmap[LEAPFIVE_APLIC_S].base,
                             memmap[LEAPFIVE_IMSIC_S].base,
                             LEAPFIVE_NUM_GUESTS);
    }

    /* register system main memory (actual RAM) */
    memory_region_add_subregion(system_memory,
                                memmap[LEAPFIVE_DRAM].base,
                                machine->ram);
    /* boot rom */
    memory_region_init_rom(mask_rom, NULL, "riscv_leapfive_board.mrom",
                           memmap[LEAPFIVE_MROM].size, &error_fatal);
    memory_region_add_subregion(system_memory,
                                memmap[LEAPFIVE_MROM].base,
                                mask_rom);
    /* SiFive Test MMIO device */
    sifive_test_create(memmap[LEAPFIVE_TEST].base);
    /* VirtIO MMIO devices */
    for (i = 0; i < LEAPFIVE_VIRTIO_COUNT; ++i) {
        sysbus_create_simple("virtio-mmio",
                            memmap[LEAPFIVE_VIRTIO].base +
                            i * memmap[LEAPFIVE_VIRTIO].size,
                            qdev_get_gpio_in(virtio_irqchip,
                                             LEAPFIVE_VIRTIO_IRQ + i));
    }

    gpex_pcie_init(system_memory, pcie_irqchip, s);
    /* Setup UART0 */
    serial_mm_init(system_memory, memmap[LEAPFIVE_UART0].base,
                   0, qdev_get_gpio_in(mmio_irqchip, LEAPFIVE_UART0_IRQ),
                   399193, serial_hd(0), DEVICE_LITTLE_ENDIAN);

    /* Setup RTC0 */
    sysbus_create_simple("goldfish_rtc", memmap[LEAPFIVE_RTC].base,
                         qdev_get_gpio_in(mmio_irqchip, LEAPFIVE_RTC_IRQ));

    /* Setup SDHCI0 */
    DeviceState *dev = qdev_new(TYPE_S3C_SDHCI);
    qdev_prop_set_uint8(dev, "sd-spec-version", LEAPFIVE_SDHCI_SPEC_VERSION);
    qdev_prop_set_uint64(dev, "capareg", LEAPFIVE_SDHCI_CAPABILITIES);
    SysBusDevice *busdev = SYS_BUS_DEVICE(dev);
    sysbus_realize_and_unref(busdev, &error_fatal);
    sysbus_mmio_map(busdev, 0, memmap[LEAPFIVE_SDHCI].base);
    sysbus_connect_irq(busdev, 0,
                       qdev_get_gpio_in(mmio_irqchip, LEAPFIVE_SDHCI_IRQ));
    DriveInfo *dinfo = drive_get(IF_SD, 0, 0);
    BlockBackend *blk = dinfo ? blk_by_legacy_dinfo(dinfo) : NULL;
    DeviceState *card_dev = qdev_new(TYPE_SD_CARD);
    qdev_prop_set_drive(card_dev, "drive", blk);
    qdev_realize_and_unref(card_dev, qdev_get_child_bus(dev, "sd-bus"),
                           &error_fatal);

    /* load/create device tree */
    if (machine->dtb) {
        machine->fdt = load_device_tree(machine->dtb, &s->fdt_size);
        if (!machine->fdt) {
            error_report("load_device_tree() failed");
            exit(1);
        }
    } else {
        create_fdt(s);
    }
    if (s->iommu_sys) {
        DeviceState *iommu_sys = qdev_new(TYPE_RISCV_IOMMU_SYS);
        object_property_set_uint(OBJECT(iommu_sys), "addr",
                                memmap[LEAPFIVE_IOMMU_SYS].base,
                                &error_fatal);
        object_property_set_uint(OBJECT(iommu_sys), "base-irq",
                                LEAPFIVE_IOMMU_SYS_IRQ,
                                &error_fatal);
        object_property_set_link(OBJECT(iommu_sys), "irqchip",
                                OBJECT(mmio_irqchip),
                                &error_fatal);
        sysbus_realize_and_unref(SYS_BUS_DEVICE(iommu_sys), &error_fatal);
    }

    s->machine_done.notify = leapfive_machine_done;
    qemu_add_machine_init_done_notifier(&s->machine_done);
}

static void leapfive_machine_instance_init(Object *obj)
{
    LeapfiveState *s = LEAPFIVE_MACHINE(obj);

    s->aia = false;
    s->iommu_sys = false;
}

static bool leapfive_get_aia(Object *obj, Error **errp)
{
    LeapfiveState *s = LEAPFIVE_MACHINE(obj);
    return s->aia;
}

static void leapfive_set_aia(Object *obj, bool value, Error **errp)
{
    LeapfiveState *s = LEAPFIVE_MACHINE(obj);
    s->aia = value;
}

static bool leapfive_get_iommu_sys(Object *obj, Error **errp)
{
    LeapfiveState *s = LEAPFIVE_MACHINE(obj);
    return s->iommu_sys;
}

static void leapfive_set_iommu_sys(Object *obj, bool value, Error **errp)
{
    LeapfiveState *s = LEAPFIVE_MACHINE(obj);
    s->iommu_sys = value;
}

static void leapfive_machine_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    mc->desc = "RISC-V LEAPFIVE board";
    mc->init = leapfive_machine_init;
    mc->max_cpus = LEAPFIVE_CPUS_MAX;
    mc->default_cpu_type = TYPE_RISCV_CPU_LEAPFIVE_V1;
    mc->default_cpus = 8;
    mc->pci_allow_0_address = true;
    mc->possible_cpu_arch_ids = riscv_numa_possible_cpu_arch_ids;
    mc->cpu_index_to_instance_props = riscv_numa_cpu_index_to_props;
    mc->get_default_cpu_node_id = riscv_numa_get_default_cpu_node_id;
    mc->numa_mem_supported = true;
    /* platform instead of architectural choice */
    mc->cpu_cluster_has_numa_boundary = true;
    mc->default_ram_id = "riscv_leapfive_board.ram";

    object_class_property_add_bool(oc, "aia", leapfive_get_aia,
                                   leapfive_set_aia);
    object_class_property_set_description(oc, "aia",
                                          "Set AIA to enable/disable "
                                          "plic and aplic-imsic.");
    object_class_property_add_bool(oc, "iommu-sys", leapfive_get_iommu_sys,
                                   leapfive_set_iommu_sys);
    object_class_property_set_description(oc, "iommu-sys",
                                          "Enable/disable the system IOMMU.");
}

static const TypeInfo leapfive_machine_typeinfo = {
    .name       = MACHINE_TYPE_NAME("leapfive-rva23-base"),
    .parent     = TYPE_MACHINE,
    .class_init = leapfive_machine_class_init,
    .instance_init = leapfive_machine_instance_init,
    .instance_size = sizeof(LeapfiveState),
    .interfaces = (const InterfaceInfo[]) {
        { TYPE_HOTPLUG_HANDLER },
        { }
    },
};

static void leapfive_machine_init_register_types(void)
{
    type_register_static(&leapfive_machine_typeinfo);
}

