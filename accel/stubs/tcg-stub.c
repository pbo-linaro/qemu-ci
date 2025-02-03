/*
 * QEMU TCG accelerator stub
 *
 * Copyright Red Hat, Inc. 2013
 *
 * Author: Paolo Bonzini     <pbonzini@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "exec/tb-flush.h"
#include "exec/exec-all.h"
#include "qapi/error.h"

/*
 * This file *ought* to be built once and linked only when required.
 * However, it is built per-target, which means qemu/osdep.h has already
 * undef'ed CONFIG_TCG, which hides the auto-generated declaration.
 */
#define CONFIG_TCG
#include "qapi/qapi-commands-machine.h"


const bool tcg_allowed = false;

void tb_flush(CPUState *cpu)
{
}

G_NORETURN void cpu_loop_exit(CPUState *cpu)
{
    g_assert_not_reached();
}

G_NORETURN void cpu_loop_exit_restore(CPUState *cpu, uintptr_t pc)
{
    g_assert_not_reached();
}

HumanReadableText *qmp_x_query_jit(Error **errp)
{
    error_setg(errp, "JIT information is only available with accel=tcg");
    return NULL;
}

HumanReadableText *qmp_x_query_opcount(Error **errp)
{
    error_setg(errp, "Opcode count information is only available with accel=tcg");
    return NULL;
}
