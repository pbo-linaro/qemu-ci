/* SPDX-License-Identifier: GPL-2.0-or-later */
/* TCG perf stubs */

#include "qemu/osdep.h"
#include "tcg/perf.h"

void perf_enable_perfmap(void)
{
}

void perf_enable_jitdump(void)
{
}

void perf_report_prologue(const void *start, size_t size)
{
}

void perf_report_code(uint64_t guest_pc, TranslationBlock *tb,
                                    const void *start)
{
}

void perf_exit(void)
{
}
