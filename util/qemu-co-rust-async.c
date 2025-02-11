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

#include "qemu/osdep.h"

#include "block/aio-wait.h"
#include "qemu/coroutine.h"
#include "qemu/coroutine-rust.h"
#include "qemu/main-loop.h"

typedef struct FutureCo {
    RustBoxedFuture *future;
    RunFuture *entry;
    void *opaque;
    bool done;
} FutureCo;

static void coroutine_fn rust_co_run_future_entry(void *opaque)
{
    FutureCo *data = opaque;

    data->entry(data->future, data->opaque);
    data->done = true;
    aio_wait_kick();
}

void no_coroutine_fn rust_run_future(RustBoxedFuture *future,
                                     RunFuture *entry,
                                     void *opaque)
{
    AioContext *ctx = qemu_get_current_aio_context();
    Coroutine *co;
    FutureCo data = {
        .future = future,
        .entry = entry,
        .opaque = opaque,
        .done = false,
    };

    GLOBAL_STATE_CODE();

    co = qemu_coroutine_create(rust_co_run_future_entry, &data);
    aio_co_enter(ctx, co);
    AIO_WAIT_WHILE(ctx, !data.done);
}
