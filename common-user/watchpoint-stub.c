/*
 * CPU watchpoint stubs
 *
 * Copyright (c) 2003 Fabrice Bellard
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "qemu/osdep.h"
#include "hw/core/cpu.h"

int cpu_watchpoint_insert(CPUState *cpu, vaddr addr, vaddr len,
                          int flags, CPUWatchpoint **watchpoint)
{
    return -ENOSYS;
}

int cpu_watchpoint_remove(CPUState *cpu, vaddr addr, vaddr len, int flags)
{
    return -ENOSYS;
}

void cpu_watchpoint_remove_by_ref(CPUState *cpu, CPUWatchpoint *wp)
{
}

void cpu_watchpoint_remove_all(CPUState *cpu, int mask)
{
}
