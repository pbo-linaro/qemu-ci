/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU LoongArch TCG interface
 *
 * Copyright (c) 2025 Loongson Technology Corporation Limited
 */
#ifndef TARGET_LOONGARCH_TCG_LOONGARCH_H
#define TARGET_LOONGARCH_TCG_LOONGARCH_H
#include "cpu.h"

void loongarch_csr_translate_init(void);

#ifdef CONFIG_TCG
int loongarch_get_addr_from_tlb(CPULoongArchState *env, hwaddr *physical,
                                int *prot, target_ulong address,
                                MMUAccessType access_type, int mmu_idx);
#else
static inline int loongarch_get_addr_from_tlb(CPULoongArchState *env,
                                              hwaddr *physical,
                                              int *prot, target_ulong address,
                                              MMUAccessType access_type,
                                              int mmu_idx)
{
    return TLBRET_NOMATCH;
}
#endif

#endif  /* TARGET_LOONGARCH_TCG_LOONGARCH_H */
