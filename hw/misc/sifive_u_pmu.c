/*
 * RISC-V SiFive U PMU emulation.
 *
 * Copyright (c) 2024 Alexei Filippov <alexei.filippov@syntacore.com>.
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
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "target/riscv/cpu.h"
#include "target/riscv/pmu.h"
#include "include/hw/misc/sifive_u_pmu.h"
#include "sysemu/cpu-timers.h"
#include "sysemu/device_tree.h"

REG32(SIFIVE_U_PMU_MHPMEVENT, 0x323)
    FIELD(SIFIVE_U_PMU_MHPMEVENT, EVENT_CLASS, 0, 8)
    FIELD(SIFIVE_U_PMU_MHPMEVENT, EVENT_MASK, 8, 18)

    /*
     * Support all PMU events  described in
     * https://sifive.cdn.prismic.io/sifive/1a82e600-1f93-4f41-b2d8-86ed8b16acba_fu740-c000-manual-v1p6.pdf
     * FU740-C000 Manual sec. 3.8 "Hardware Performace Monitor", all
     * events trigger irq by counter overflow, by default all caunters
     * caunted identically, special behavior, combining events for example,
     * must be described separately in write/read and trigger irq functions.
     */

#define SIFIVE_U_PMU_INST { \
    X(RISCV_SIFIVE_U_EVENT_EXCEPTION_TAKEN,                   0x00001), \
    X(RISCV_SIFIVE_U_EVENT_INTEGER_LOAD_RETIRED,              0x00002), \
    X(RISCV_SIFIVE_U_EVENT_INTEGER_STORE_RETIRED,             0x00004), \
    X(RISCV_SIFIVE_U_EVENT_ATOMIC_MEMORY_RETIRED,             0x00008), \
    X(RISCV_SIFIVE_U_EVENT_SYSTEM_INSTRUCTION_RETIRED,        0x00010), \
    X(RISCV_SIFIVE_U_EVENT_INTEGER_ARITHMETIC_RETIRED,        0x00020), \
    X(RISCV_SIFIVE_U_EVENT_CONDITIONAL_BRANCH_RETIRED,        0x00040), \
    X(RISCV_SIFIVE_U_EVENT_JAL_INSTRUCTION_RETIRED,           0x00080), \
    X(RISCV_SIFIVE_U_EVENT_JALR_INSTRUCTION_RETIRED,          0x00100), \
    X(RISCV_SIFIVE_U_EVENT_INTEGER_MULTIPLICATION_RETIRED,    0x00200), \
    X(RISCV_SIFIVE_U_EVENT_INTEGER_DIVISION_RETIRED,          0x00400), \
    X(RISCV_SIFIVE_U_EVENT_FP_LOAD_RETIRED,                   0x00800), \
    X(RISCV_SIFIVE_U_EVENT_FP_STORE_RETIRED,                  0x01000), \
    X(RISCV_SIFIVE_U_EVENT_FP_ADDITION_RETIRED,               0x02000), \
    X(RISCV_SIFIVE_U_EVENT_FP_MULTIPLICATION_RETIRED,         0x04000), \
    X(RISCV_SIFIVE_U_EVENT_FP_FUSEDMADD_RETIRED,              0x08000), \
    X(RISCV_SIFIVE_U_EVENT_FP_DIV_SQRT_RETIRED,               0x10000), \
    X(RISCV_SIFIVE_U_EVENT_OTHER_FP_RETIRED,                  0x20000), }

#define SIFIVE_U_PMU_MICROARCH { \
    X(RISCV_SIFIVE_U_EVENT_ADDRESSGEN_INTERLOCK,              0x00001), \
    X(RISCV_SIFIVE_U_EVENT_LONGLAT_INTERLOCK,                 0x00002), \
    X(RISCV_SIFIVE_U_EVENT_CSR_READ_INTERLOCK,                0x00004), \
    X(RISCV_SIFIVE_U_EVENT_ICACHE_ITIM_BUSY,                  0x00008), \
    X(RISCV_SIFIVE_U_EVENT_DCACHE_DTIM_BUSY,                  0x00010), \
    X(RISCV_SIFIVE_U_EVENT_BRANCH_DIRECTION_MISPREDICTION,    0x00020), \
    X(RISCV_SIFIVE_U_EVENT_BRANCH_TARGET_MISPREDICTION,       0x00040), \
    X(RISCV_SIFIVE_U_EVENT_PIPE_FLUSH_CSR_WRITE,              0x00080), \
    X(RISCV_SIFIVE_U_EVENT_PIPE_FLUSH_OTHER_EVENT,            0x00100), \
    X(RISCV_SIFIVE_U_EVENT_INTEGER_MULTIPLICATION_INTERLOCK,  0x00200), \
    X(RISCV_SIFIVE_U_EVENT_FP_INTERLOCK,                      0x00400), }

#define SIFIVE_U_PMU_MEM { \
    X(RISCV_SIFIVE_U_EVENT_ICACHE_RETIRED,                    0x00001), \
    X(RISCV_SIFIVE_U_EVENT_DCACHE_MISS_MMIO_ACCESSES,         0x00002), \
    X(RISCV_SIFIVE_U_EVENT_DCACHE_WRITEBACK,                  0x00004), \
    X(RISCV_SIFIVE_U_EVENT_INST_TLB_MISS,                     0x00008), \
    X(RISCV_SIFIVE_U_EVENT_DATA_TLB_MISS,                     0x00010), \
    X(RISCV_SIFIVE_U_EVENT_UTLB_MISS,                         0x00020), }

#define X(a, b) a = b
    enum SIFIVE_U_PMU_INST;
    enum SIFIVE_U_PMU_MEM;
    enum SIFIVE_U_PMU_MICROARCH;
#undef X

#define X(a, b) a
    const uint32_t
    riscv_sifive_u_pmu_events[RISCV_SIFIVE_U_CLASS_MAX][RISCV_SIFIVE_U_MASK_MAX] = {
    SIFIVE_U_PMU_INST,
    SIFIVE_U_PMU_MICROARCH,
    SIFIVE_U_PMU_MEM,
    };
#undef X

void sifive_u_pmu_generate_fdt_node(void *fdt, uint32_t cmask, char *pmu_name)
{
    uint32_t fdt_event_mhpmevent_map[10 * 3] = {};
    uint32_t fdt_event_mhpmctr_map[6 * 4] = {};
    uint32_t event_idx;

    /*
     * SBI_PMU_HW_CACHE_REFERENCES: 0x3 -> Instruction cache/ITIM busy |
     *                                     Data cache/DTIM busy
     * result: < 0x3 0x0 1801 >
     */
    fdt_event_mhpmevent_map[0]  = cpu_to_be32(0x3);
    fdt_event_mhpmevent_map[1]  = cpu_to_be32(0x0);
    event_idx = FIELD_DP32(0, SIFIVE_U_PMU_MHPMEVENT, EVENT_MASK,
                           RISCV_SIFIVE_U_EVENT_ICACHE_ITIM_BUSY |
                           RISCV_SIFIVE_U_EVENT_DCACHE_DTIM_BUSY);
    event_idx = FIELD_DP32(event_idx, SIFIVE_U_PMU_MHPMEVENT, EVENT_CLASS,
                           RISCV_SIFIVE_U_CLASS_MICROARCH);
    fdt_event_mhpmevent_map[2]  = cpu_to_be32(event_idx);


    /*
     * SBI_PMU_HW_CACHE_MISSES: 0x4 -> Instruction cache miss |
     *                                 Data cache miss or mem-mapped I/O access
     * result: < 0x4 0x0 0x302 >
     */
    fdt_event_mhpmevent_map[3]  = cpu_to_be32(0x4);
    fdt_event_mhpmevent_map[4]  = cpu_to_be32(0x0);
    event_idx = FIELD_DP32(0, SIFIVE_U_PMU_MHPMEVENT, EVENT_MASK,
                           RISCV_SIFIVE_U_EVENT_ICACHE_RETIRED |
                           RISCV_SIFIVE_U_EVENT_DCACHE_MISS_MMIO_ACCESSES);
    event_idx = FIELD_DP32(event_idx, SIFIVE_U_PMU_MHPMEVENT, EVENT_CLASS,
                           RISCV_SIFIVE_U_CLASS_MEM);
    fdt_event_mhpmevent_map[5]  = cpu_to_be32(event_idx);

    /*
     * SBI_PMU_HW_BRANCH_INSTRUCTIONS: 0x5 -> Conditional branch retired
     * result: < 0x5 0x0 0x4000 >
     */
    fdt_event_mhpmevent_map[6]  = cpu_to_be32(0x5);
    fdt_event_mhpmevent_map[7]  = cpu_to_be32(0x0);
    event_idx = FIELD_DP32(0, SIFIVE_U_PMU_MHPMEVENT, EVENT_MASK,
                           RISCV_SIFIVE_U_EVENT_CONDITIONAL_BRANCH_RETIRED);
    event_idx = FIELD_DP32(event_idx, SIFIVE_U_PMU_MHPMEVENT, EVENT_CLASS,
                           RISCV_SIFIVE_U_CLASS_INST);
    fdt_event_mhpmevent_map[8]  = cpu_to_be32(event_idx);

    /*
     * SBI_PMU_HW_BRANCH_MISSES: 0x6 -> Branch direction misprediction |
     *                                  Branch/jump target misprediction
     * result: < 0x6 0x0 0x6001 >
     */
    fdt_event_mhpmevent_map[9]  = cpu_to_be32(0x6);
    fdt_event_mhpmevent_map[10] = cpu_to_be32(0x0);
    event_idx = FIELD_DP32(0, SIFIVE_U_PMU_MHPMEVENT, EVENT_MASK,
                           RISCV_SIFIVE_U_EVENT_BRANCH_DIRECTION_MISPREDICTION |
                           RISCV_SIFIVE_U_EVENT_BRANCH_TARGET_MISPREDICTION);
    event_idx = FIELD_DP32(event_idx, SIFIVE_U_PMU_MHPMEVENT, EVENT_CLASS,
                           RISCV_SIFIVE_U_CLASS_MICROARCH);
    fdt_event_mhpmevent_map[11] = cpu_to_be32(event_idx);

    /*
     * L1D_READ_MISS: 0x10001 -> Data cache miss or memory-mapped I/O access
     * result: < 0x10001 0x0 0x202 >
     */
    fdt_event_mhpmevent_map[12]  = cpu_to_be32(0x10001);
    fdt_event_mhpmevent_map[13] = cpu_to_be32(0x0);
    event_idx = FIELD_DP32(0, SIFIVE_U_PMU_MHPMEVENT, EVENT_MASK,
                           RISCV_SIFIVE_U_EVENT_DCACHE_MISS_MMIO_ACCESSES);
    event_idx = FIELD_DP32(event_idx, SIFIVE_U_PMU_MHPMEVENT, EVENT_CLASS,
                           RISCV_SIFIVE_U_CLASS_MEM);
    fdt_event_mhpmevent_map[14] = cpu_to_be32(event_idx);

    /*
     * L1D_WRITE_ACCESS: 0x10002 -> Data cache write back
     * result: < 0x10002 0x0 0x402 >
     */
    fdt_event_mhpmevent_map[15]  = cpu_to_be32(0x10002);
    fdt_event_mhpmevent_map[16] = cpu_to_be32(0x0);
    event_idx = FIELD_DP32(0, SIFIVE_U_PMU_MHPMEVENT, EVENT_MASK,
                           RISCV_SIFIVE_U_EVENT_DCACHE_WRITEBACK);
    event_idx = FIELD_DP32(event_idx, SIFIVE_U_PMU_MHPMEVENT, EVENT_CLASS,
                           RISCV_SIFIVE_U_CLASS_MEM);
    fdt_event_mhpmevent_map[17] = cpu_to_be32(event_idx);

    /*
     * L1I_READ_ACCESS: 0x10009 -> Instruction cache miss
     * result: < 0x10009 0x0 0x102 >
     */
    fdt_event_mhpmevent_map[18]  = cpu_to_be32(0x10009);
    fdt_event_mhpmevent_map[19] = cpu_to_be32(0x0);
    event_idx = FIELD_DP32(0, SIFIVE_U_PMU_MHPMEVENT, EVENT_MASK,
                           RISCV_SIFIVE_U_EVENT_ICACHE_RETIRED);
    event_idx = FIELD_DP32(event_idx, SIFIVE_U_PMU_MHPMEVENT, EVENT_CLASS,
                           RISCV_SIFIVE_U_CLASS_MEM);
    fdt_event_mhpmevent_map[20] = cpu_to_be32(event_idx);

    /*
     * LL_READ_MISS: 0x10011 -> UTLB miss
     * result: < 0x10011 0x0 0x2002 >
     */
    fdt_event_mhpmevent_map[21]  = cpu_to_be32(0x10011);
    fdt_event_mhpmevent_map[22] = cpu_to_be32(0x0);
    event_idx = FIELD_DP32(0, SIFIVE_U_PMU_MHPMEVENT, EVENT_MASK,
                           RISCV_SIFIVE_U_EVENT_UTLB_MISS);
    event_idx = FIELD_DP32(event_idx, SIFIVE_U_PMU_MHPMEVENT, EVENT_CLASS,
                           RISCV_SIFIVE_U_CLASS_MEM);
    fdt_event_mhpmevent_map[23] = cpu_to_be32(event_idx);

    /*
     * DTLB_READ_MISS: 0x10019 -> Data TLB miss
     * result: < 0x10019 0x0 0x1002 >
     */
    fdt_event_mhpmevent_map[24]  = cpu_to_be32(0x10019);
    fdt_event_mhpmevent_map[25] = cpu_to_be32(0x0);
    event_idx = FIELD_DP32(0, SIFIVE_U_PMU_MHPMEVENT, EVENT_MASK,
                           RISCV_SIFIVE_U_EVENT_DATA_TLB_MISS);
    event_idx = FIELD_DP32(event_idx, SIFIVE_U_PMU_MHPMEVENT, EVENT_CLASS,
                           RISCV_SIFIVE_U_CLASS_MEM);
    fdt_event_mhpmevent_map[26] = cpu_to_be32(event_idx);

    /*
     * DTLB_READ_MISS: 0x10021 -> Data TLB miss
     * result: < 0x10019 0x0 0x802 >
     */
    fdt_event_mhpmevent_map[27]  = cpu_to_be32(0x10021);
    fdt_event_mhpmevent_map[28] = cpu_to_be32(0x0);
    event_idx = FIELD_DP32(0, SIFIVE_U_PMU_MHPMEVENT, EVENT_MASK,
                           RISCV_SIFIVE_U_EVENT_INST_TLB_MISS);
    event_idx = FIELD_DP32(event_idx, SIFIVE_U_PMU_MHPMEVENT, EVENT_CLASS,
                           RISCV_SIFIVE_U_CLASS_MEM);
    fdt_event_mhpmevent_map[29] = cpu_to_be32(event_idx);

    fdt_event_mhpmctr_map[0] = cpu_to_be32(0x00003);
    fdt_event_mhpmctr_map[1] = cpu_to_be32(0x00006);
    fdt_event_mhpmctr_map[2] = cpu_to_be32(cmask);

    fdt_event_mhpmctr_map[3] = cpu_to_be32(0x10001);
    fdt_event_mhpmctr_map[4] = cpu_to_be32(0x10002);
    fdt_event_mhpmctr_map[5] = cpu_to_be32(cmask);

    fdt_event_mhpmctr_map[6] = cpu_to_be32(0x10009);
    fdt_event_mhpmctr_map[7] = cpu_to_be32(0x10009);
    fdt_event_mhpmctr_map[8] = cpu_to_be32(cmask);

    fdt_event_mhpmctr_map[9] = cpu_to_be32(0x10011);
    fdt_event_mhpmctr_map[10] = cpu_to_be32(0x10011);
    fdt_event_mhpmctr_map[11] = cpu_to_be32(cmask);

    fdt_event_mhpmctr_map[12] = cpu_to_be32(0x10019);
    fdt_event_mhpmctr_map[13] = cpu_to_be32(0x10019);
    fdt_event_mhpmctr_map[14] = cpu_to_be32(cmask);

    fdt_event_mhpmctr_map[15] = cpu_to_be32(0x10021);
    fdt_event_mhpmctr_map[16] = cpu_to_be32(0x10021);
    fdt_event_mhpmctr_map[17] = cpu_to_be32(cmask);

    fdt_event_mhpmctr_map[18] = cpu_to_be32(0x1);
    fdt_event_mhpmctr_map[19] = cpu_to_be32(0x1);
    fdt_event_mhpmctr_map[20] = cpu_to_be32(cmask | 1 << 0);

    fdt_event_mhpmctr_map[21] = cpu_to_be32(0x2);
    fdt_event_mhpmctr_map[22] = cpu_to_be32(0x2);
    fdt_event_mhpmctr_map[23] = cpu_to_be32(cmask | 1 << 2);

   /* This a OpenSBI specific DT property documented in OpenSBI docs */
    qemu_fdt_setprop(fdt, pmu_name, "riscv,event-to-mhpmevent",
                     fdt_event_mhpmevent_map, sizeof(fdt_event_mhpmevent_map));
    qemu_fdt_setprop(fdt, pmu_name, "riscv,event-to-mhpmcounters",
                     fdt_event_mhpmctr_map, sizeof(fdt_event_mhpmctr_map));

}

bool riscv_sifive_u_supported_events(uint32_t event_idx)
{
    uint32_t group = FIELD_EX32(event_idx, SIFIVE_U_PMU_MHPMEVENT, EVENT_CLASS);
    uint32_t event_mask = FIELD_EX32(event_idx, SIFIVE_U_PMU_MHPMEVENT,
                                     EVENT_MASK);
    uint32_t idx = 32 - clz32(event_mask);

    if (group >= RISCV_SIFIVE_U_CLASS_MAX || idx > RISCV_SIFIVE_U_MASK_MAX) {
        return 0;
    }

    bool event_match = true;
    if (!idx) {
        event_match = false;
    }
    while (event_match && idx) {
        if (!riscv_sifive_u_pmu_events[group][idx - 1]) {
            event_match = false;
        }
        event_mask = event_mask & (~(1 << (idx - 1)));
        idx = 32 - clz32(event_mask);
    }
    return event_match;
}

static target_ulong get_ticks(bool icnt, bool high_half)
{
    int64_t val;
    target_ulong res;

    if (icnt && icount_enabled()) {
        val = icount_get_raw();
    } else {
        val = cpu_get_host_ticks();
    }

    if (high_half) {
        res = val >> 32;
    } else {
        res = val;
    }

    return res;
}

target_ulong riscv_sifive_u_pmu_ctr_read(PMUCTRState *counter,
                                         uint32_t event_idx, bool high_half)
{
    target_ulong ctrl_val = high_half ? counter->mhpmcounterh_val :
                                        counter->mhpmcounter_val;
    uint32_t event_class_field = FIELD_EX32(event_idx,
                                                SIFIVE_U_PMU_MHPMEVENT,
                                                EVENT_CLASS);
    uint32_t event_mask_field = FIELD_EX32(event_idx,
                                               SIFIVE_U_PMU_MHPMEVENT,
                                               EVENT_MASK);

    if (event_class_field >= RISCV_SIFIVE_U_CLASS_MAX ||
        (32 - clz32(event_mask_field)) >= RISCV_SIFIVE_U_MASK_MAX) {
        return ctrl_val;
    }

    switch (event_class_field) {
    /* If we want to handle some events separately */

    /* fall through */
    default:
    /* In case we do not want handle it separately */
        if (riscv_sifive_u_supported_events(event_idx)) {
                return get_ticks(false, high_half);
        }
    /* Did not find event in supported */
        return ctrl_val;
    }

    g_assert_not_reached(); /* unreachable */
    return 0;
}

void riscv_sifive_u_pmu_ctr_write(PMUCTRState *counter, uint32_t event_idx,
                             target_ulong val, bool high_half)
{
    target_ulong *ctr_prev = high_half ? &counter->mhpmcounterh_prev :
                                         &counter->mhpmcounter_prev;
    uint32_t event_class_field = FIELD_EX32(event_idx,
                                                SIFIVE_U_PMU_MHPMEVENT,
                                                EVENT_CLASS);
    uint32_t event_mask_field = FIELD_EX32(event_idx,
                                               SIFIVE_U_PMU_MHPMEVENT,
                                               EVENT_MASK);

    if (event_class_field >= RISCV_SIFIVE_U_CLASS_MAX ||
        (32 - clz32(event_mask_field)) >= RISCV_SIFIVE_U_MASK_MAX) {
        *ctr_prev = val;
        return;
    }

    switch (event_class_field) {
    /* If we want to handle some events separately */

    /* fall through */
    default:
    /* In case we do not want handle it separately */
        if (riscv_sifive_u_supported_events(event_idx)) {
            *ctr_prev = get_ticks(false, high_half);
            return;
        }
    /* Did not find event in supported */
        *ctr_prev = val;
        return;
    }

    g_assert_not_reached(); /* unreachable */
    return;
}
