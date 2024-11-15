/*
 *  Translated block handling
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
#ifndef TRANSLATE_ALL_H
#define TRANSLATE_ALL_H

#include "exec/exec-all.h"

/**
 * cpu_unwind_state_data:
 * @host_pc: the host pc within the translation
 * @data: output data
 *
 * Attempt to load the the unwind state for a host pc occurring in
 * translated code.  If @host_pc is not in translated code, the
 * function returns false; otherwise @data is loaded.
 * This is the same unwind info as given to restore_state_to_opc.
 */
bool cpu_unwind_state_data(uintptr_t host_pc, uint64_t *data);

/* translate-all.c */
void tb_check_watchpoint(CPUState *cpu, uintptr_t retaddr);

#ifdef CONFIG_USER_ONLY
void page_protect(tb_page_addr_t page_addr);
int page_unprotect(target_ulong address, uintptr_t pc);
#endif

#endif /* TRANSLATE_ALL_H */
