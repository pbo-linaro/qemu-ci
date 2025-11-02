/*
 * safe-syscall.c: C implementation using libc's syscall()
 * to handle signals occurring right before system calls.
 *
 * Written by Balint Reczey <balint@balintreczey.hu>
 *
 * Copyright (C) 2025 Balint Reczey
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#if defined(__linux__)
# include "special-errno.h"
#elif defined(__FreeBSD__)
# include "errno_defs.h"
#endif
#include "user/safe-syscall.h"
#include <stdarg.h>
#include <unistd.h>
#include <sys/syscall.h>
#include "qemu/atomic.h"

/* Global runtime toggle (default: false). */
bool qemu_use_libc_syscalls;

/*
 * libc-backed implementation: Make a system call via libc's syscall()
 * if no guest signal is pending.
 *
 * IMPORTANT: Unlike the assembly implementation, this approach
 * can't completely eliminate the race between checking signal_pending and
 * entering the syscall. The platform-specific assembly versions eliminate
 * the race as described in details in safe-syscall.h.
  *
 * In practice, this race window is extremely narrow (typically a few CPU cycles),
 * and guest signals are rare events. The primary use case for this implementation
 * is debugging and LD_PRELOAD interposition where perfect atomicity may
 * be less critical than observability.
 */
long safe_syscall_libc(int *pending, long number, ...)
{
    va_list ap;
    long arg1, arg2, arg3, arg4, arg5, arg6;
    long ret;

    va_start(ap, number);
    /* Extract up to 6 syscall arguments */
    arg1 = va_arg(ap, long);
    arg2 = va_arg(ap, long);
    arg3 = va_arg(ap, long);
    arg4 = va_arg(ap, long);
    arg5 = va_arg(ap, long);
    arg6 = va_arg(ap, long);
    va_end(ap);

    if (qatomic_read(pending)) {
        errno = QEMU_ERESTARTSYS;
        return -1;
    }

    /* Make the actual system call using libc's syscall() */
    ret = syscall(number, arg1, arg2, arg3, arg4, arg5, arg6);

    return ret;
}
