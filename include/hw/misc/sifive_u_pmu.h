#ifndef RISCV_SIFIVE_U_PMU_H
#define RISCV_SIFIVE_U_PMU_H

#include "target/riscv/cpu.h"
#include "qapi/error.h"

/* Maximum events per class */
#define RISCV_SIFIVE_U_MASK_MAX 18

enum riscv_sifive_u_pmu_classes {
    RISCV_SIFIVE_U_CLASS_INST = 0x0,
    RISCV_SIFIVE_U_CLASS_MICROARCH,
    RISCV_SIFIVE_U_CLASS_MEM,
    RISCV_SIFIVE_U_CLASS_MAX  = 0x3
};

bool riscv_sifive_u_supported_events(uint32_t event_idx);
void riscv_sifive_u_pmu_ctr_write(PMUCTRState *counter, uint32_t event_idx,
                                    target_ulong val, bool high_half);
target_ulong riscv_sifive_u_pmu_ctr_read(PMUCTRState *counter,
                                           uint32_t event_idx, bool high_half);
void sifive_u_pmu_generate_fdt_node(void *fdt, uint32_t cmask, char *pmu_name);

#endif /* RISCV_SCR_PMU_H */
