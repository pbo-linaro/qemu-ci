/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef I386_TARGET_PTRACE_H
#define I386_TARGET_PTRACE_H

/*
 * Compare linux arch/x86/include/uapi/asm/ptrace.h (struct pt_regs) and
 * arch/x86/include/asm/user_32.h (struct user_regs_struct).
 * The structure layouts are identical; the user_regs_struct names are better.
 */
struct target_pt_regs {
    abi_ulong bx;
    abi_ulong cx;
    abi_ulong dx;
    abi_ulong si;
    abi_ulong di;
    abi_ulong bp;
    abi_ulong ax;
    abi_ulong ds;
    abi_ulong es;
    abi_ulong fs;
    abi_ulong gs;
    abi_ulong orig_ax;
    abi_ulong ip;
    abi_ulong cs;
    abi_ulong flags;
    abi_ulong sp;
    abi_ulong ss;
};

#endif /* I386_TARGET_PTRACE_H */
