/*
 * Common definitions for the softmmu tlb
 *
 * Copyright (c) 2003 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */
#ifndef EXEC_TLB_COMMON_H
#define EXEC_TLB_COMMON_H 1

#define CPU_TLB_ENTRY_BITS 5

/* Minimalized TLB entry for use by TCG fast path. */
typedef union CPUTLBEntry {
    struct {
        uint64_t addr_read;
        uint64_t addr_write;
        uint64_t addr_code;
        /*
         * Addend to virtual address to get host address.  IO accesses
         * use the corresponding iotlb value.
         */
        uintptr_t addend;
    };
    /*
     * Padding to get a power of two size, as well as index
     * access to addr_{read,write,code}.
     */
    uint64_t addr_idx[(1 << CPU_TLB_ENTRY_BITS) / sizeof(uint64_t)];
} CPUTLBEntry;

QEMU_BUILD_BUG_ON(sizeof(CPUTLBEntry) != (1 << CPU_TLB_ENTRY_BITS));

/*
 * Data elements that are per MMU mode, accessed by the fast path.
 * The structure is aligned to aid loading the pair with one insn.
 */
typedef struct CPUTLBDescFast {
    /* Contains (n_entries - 1) << CPU_TLB_ENTRY_BITS */
    uintptr_t mask;
    /* The array of tlb entries itself. */
    CPUTLBEntry *table;
} CPUTLBDescFast QEMU_ALIGNED(2 * sizeof(void *));

/*
 * The full TLB entry, which is not accessed by generated TCG code,
 * so the layout is not as critical as that of CPUTLBEntry. This is
 * also why we don't want to combine the two structs.
 */
struct CPUTLBEntryFull {
    /*
     * @xlat_section contains:
     *  - in the lower TARGET_PAGE_BITS, a physical section number
     *  - with the lower TARGET_PAGE_BITS masked off, an offset which
     *    must be added to the virtual address to obtain:
     *     + the ram_addr_t of the target RAM (if the physical section
     *       number is PHYS_SECTION_NOTDIRTY or PHYS_SECTION_ROM)
     *     + the offset within the target MemoryRegion (otherwise)
     */
    hwaddr xlat_section;

    /*
     * @phys_addr contains the physical address in the address space
     * given by cpu_asidx_from_attrs(cpu, @attrs).
     */
    hwaddr phys_addr;

    /* @attrs contains the memory transaction attributes for the page. */
    MemTxAttrs attrs;

    /* @prot contains the complete protections for the page. */
    uint8_t prot;

    /* @lg_page_size contains the log2 of the page size. */
    uint8_t lg_page_size;

    /* Additional tlb flags requested by tlb_fill. */
    uint8_t tlb_fill_flags;

    /*
     * Additional tlb flags for use by the slow path. If non-zero,
     * the corresponding CPUTLBEntry comparator must have TLB_FORCE_SLOW.
     */
    uint8_t slow_flags[MMU_ACCESS_COUNT];

    /*
     * Allow target-specific additions to this structure.
     * This may be used to cache items from the guest cpu
     * page tables for later use by the implementation.
     */
    union {
        /*
         * Cache the attrs and shareability fields from the page table entry.
         *
         * For ARMMMUIdx_Stage2*, pte_attrs is the S2 descriptor bits [5:2].
         * Otherwise, pte_attrs is the same as the MAIR_EL1 8-bit format.
         * For shareability and guarded, as in the SH and GP fields respectively
         * of the VMSAv8-64 PTEs.
         */
        struct {
            uint8_t pte_attrs;
            uint8_t shareability;
            bool guarded;
        } arm;
    } extra;
};

#endif /* EXEC_TLB_COMMON_H */
