/*
 *  Common CPU TLB handling
 *
 *  Copyright (c) 2003 Fabrice Bellard
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

#ifndef CPUTLB_H
#define CPUTLB_H

#include "exec/cpu-common.h"

#if defined(CONFIG_TCG) && !defined(CONFIG_USER_ONLY)

/**
 * tlb_init - initialize a CPU's TLB
 * @cpu: CPU whose TLB should be initialized
 */
void tlb_init(CPUState *cpu);

/**
 * tlb_destroy - destroy a CPU's TLB
 * @cpu: CPU whose TLB should be destroyed
 */
void tlb_destroy(CPUState *cpu);

void tlb_protect_code(ram_addr_t ram_addr);
void tlb_unprotect_code(ram_addr_t ram_addr);

#else

static inline void tlb_init(CPUState *cpu)
{
}
static inline void tlb_destroy(CPUState *cpu)
{
}

#endif /* CONFIG_TCG && !CONFIG_USER_ONLY */

#ifndef CONFIG_USER_ONLY

void tlb_reset_dirty(CPUState *cpu, ram_addr_t start1, ram_addr_t length);
void tlb_reset_dirty_range_all(ram_addr_t start, ram_addr_t length);

#endif

#endif
