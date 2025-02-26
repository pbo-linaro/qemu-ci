/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * LoongArch emulation helpers for CSRs
 *
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/main-loop.h"
#include "cpu.h"
#include "internals.h"
#include "qemu/host-utils.h"
#include "exec/helper-proto.h"
#include "exec/exec-all.h"
#include "exec/cpu_ldst.h"
#include "hw/irq.h"
#include "cpu-csr.h"


void check_tlb_ps(CPULoongArchState *env)
{
    uint8_t ptbase = FIELD_EX64(env->CSR_PWCL, CSR_PWCL, PTBASE);
    uint8_t default_ps = ctz32(env->CSR_PRCFG2);

    /* check  CSR_PWCL.PTBASE bits */
    if (ptbase < default_ps) {
         qemu_log_mask(LOG_GUEST_ERROR,
                      "Attrmpted set ptbase 2^%d\n", ptbase);
         env->CSR_PWCL = FIELD_DP64(env->CSR_PWCL, CSR_PWCL, PTBASE, default_ps);
    }

    /* check CSR_STLBPS.PS bits */
    uint8_t tlb_ps = FIELD_EX64(env->CSR_STLBPS, CSR_STLBPS, PS);

    if (tlb_ps < default_ps) {
        env->CSR_STLBPS= FIELD_DP64(env->CSR_STLBPS, CSR_STLBPS, PS, default_ps);
    }
}

target_ulong helper_csrwr_crmd(CPULoongArchState *env, target_ulong val)
{
    uint8_t pg,old_pg;
    int64_t old_v = env->CSR_CRMD;

    pg = FIELD_EX64(val, CSR_CRMD, PG);
    old_pg = FIELD_EX64(old_v, CSR_CRMD,PG);
    if (pg&& !old_pg) {
        check_tlb_ps(env);
    }
    env->CSR_CRMD = val;
    return old_v;
}

target_ulong helper_csrwr_stlbps(CPULoongArchState *env, target_ulong val)
{
    int64_t old_v = env->CSR_STLBPS;
    uint8_t default_ps = ctz32(env->CSR_PRCFG2);

    /*
     * The real hardware only supports the min tlb_ps is 12
     * tlb_ps=0 may cause undefined-behavior.
     */
    uint8_t tlb_ps = FIELD_EX64(env->CSR_STLBPS, CSR_STLBPS, PS);
    if (tlb_ps  < default_ps) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "Attempted set ps %d\n",tlb_ps);
        val = FIELD_DP64(val, CSR_STLBPS, PS, default_ps);
    }
    env->CSR_STLBPS = val;
    return old_v;
}

target_ulong helper_csrrd_pgd(CPULoongArchState *env)
{
    int64_t v;

    if (env->CSR_TLBRERA & 0x1) {
        v = env->CSR_TLBRBADV;
    } else {
        v = env->CSR_BADV;
    }

    if ((v >> 63) & 0x1) {
        v = env->CSR_PGDH;
    } else {
        v = env->CSR_PGDL;
    }

    return v;
}

target_ulong helper_csrrd_cpuid(CPULoongArchState *env)
{
    LoongArchCPU *lac = env_archcpu(env);

    env->CSR_CPUID = CPU(lac)->cpu_index;

    return env->CSR_CPUID;
}

target_ulong helper_csrrd_tval(CPULoongArchState *env)
{
    LoongArchCPU *cpu = env_archcpu(env);

    return cpu_loongarch_get_constant_timer_ticks(cpu);
}

target_ulong helper_csrwr_estat(CPULoongArchState *env, target_ulong val)
{
    int64_t old_v = env->CSR_ESTAT;

    /* Only IS[1:0] can be written */
    env->CSR_ESTAT = deposit64(env->CSR_ESTAT, 0, 2, val);

    return old_v;
}

target_ulong helper_csrwr_asid(CPULoongArchState *env, target_ulong val)
{
    int64_t old_v = env->CSR_ASID;

    /* Only ASID filed of CSR_ASID can be written */
    env->CSR_ASID = deposit64(env->CSR_ASID, 0, 10, val);
    if (old_v != env->CSR_ASID) {
        tlb_flush(env_cpu(env));
    }
    return old_v;
}

target_ulong helper_csrwr_tcfg(CPULoongArchState *env, target_ulong val)
{
    LoongArchCPU *cpu = env_archcpu(env);
    int64_t old_v = env->CSR_TCFG;

    cpu_loongarch_store_constant_timer_config(cpu, val);

    return old_v;
}

target_ulong helper_csrwr_ticlr(CPULoongArchState *env, target_ulong val)
{
    LoongArchCPU *cpu = env_archcpu(env);
    int64_t old_v = 0;

    if (val & 0x1) {
        bql_lock();
        loongarch_cpu_set_irq(cpu, IRQ_TIMER, 0);
        bql_unlock();
    }
    return old_v;
}

target_ulong helper_csrwr_pwcl(CPULoongArchState *env, target_ulong val)
{
    int shift, ptbase;
    int64_t old_v = env->CSR_PWCL;
    uint8_t default_ps = ctz32(env->CSR_PRCFG2);

    /*
     * The real hardware only supports 64bit PTE width now, 128bit or others
     * treated as illegal.
     */
    shift = FIELD_EX64(val, CSR_PWCL, PTEWIDTH);
    ptbase = FIELD_EX64(val, CSR_PWCL, PTBASE);
    if (shift) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "Attempted set pte width with %d bit\n", 64 << shift);
        val = FIELD_DP64(val, CSR_PWCL, PTEWIDTH, 0);
    }
    if (ptbase < default_ps) {
         qemu_log_mask(LOG_GUEST_ERROR,
                      "Attrmpted set ptbase 2^%d\n", ptbase);
         val = FIELD_DP64(val, CSR_PWCL, PTBASE, default_ps);
    }

    env->CSR_PWCL = val;
    return old_v;
}
