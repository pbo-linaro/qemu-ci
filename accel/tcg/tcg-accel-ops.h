/*
 * QEMU TCG vCPU common functionality
 *
 * Functionality common to all TCG vcpu variants: mttcg, rr and icount.
 *
 * Copyright 2020 SUSE LLC
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef TCG_ACCEL_OPS_H
#define TCG_ACCEL_OPS_H

#include "system/cpus.h"
#include "hw/core/cpu.h"

void tcg_cpu_destroy(CPUState *cpu);
int tcg_cpu_exec(CPUState *cpu);
void tcg_handle_interrupt(CPUState *cpu, int mask);
void tcg_cpu_init_cflags(CPUState *cpu, bool parallel);

#ifdef CONFIG_USER_ONLY
#define tcg_cpus_queue cpus_queue
#else
/* Guard with qemu_cpu_list_lock */
extern CPUTailQ tcg_cpus_queue;
#endif

#define CPU_FOREACH_TCG(cpu) QTAILQ_FOREACH_RCU(cpu, &tcg_cpus_queue, node)

#endif /* TCG_ACCEL_OPS_H */
