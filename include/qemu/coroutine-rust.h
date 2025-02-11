/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Helpers to run Rust futures using QEMU coroutines
 *
 * Copyright Red Hat
 *
 * Author:
 *   Kevin Wolf <kwolf@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 */

#ifndef QEMU_COROUTINE_RUST_H
#define QEMU_COROUTINE_RUST_H

typedef struct RustBoxedFuture RustBoxedFuture;
typedef void coroutine_fn RunFuture(RustBoxedFuture *future, void *opaque);

void no_coroutine_fn rust_run_future(RustBoxedFuture *future,
                                     RunFuture *entry,
                                     void *opaque);

#endif
