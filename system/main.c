/*
 * QEMU System Emulator
 *
 * Copyright (c) 2003-2020 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "qemu-main.h"
#include "qemu/main-loop.h"
#include "sysemu/sysemu.h"

#ifdef CONFIG_DARWIN
#include <CoreFoundation/CoreFoundation.h>
#endif

static int qemu_default_main(void)
{
    int status;

    status = qemu_main_loop();
    qemu_cleanup(status);

    return status;
}

/*
 * Various macOS system libraries, including the Cocoa UI and anything using
 * libdispatch, such as ParavirtualizedGraphics.framework, requires that the
 * main runloop, on the main (initial) thread be running or at least regularly
 * polled for events. A special mode is therefore supported, where the QEMU
 * main loop runs on a separate thread and the main thread handles the
 * CF/Cocoa runloop.
 */

static void *call_qemu_default_main(void *opaque)
{
    int status;

    bql_lock();
    status = qemu_default_main();
    bql_unlock();

    exit(status);
}

static void qemu_run_default_main_on_new_thread(void)
{
    QemuThread thread;

    qemu_thread_create(&thread, "qemu_main", call_qemu_default_main,
                       NULL, QEMU_THREAD_DETACHED);
}


#ifdef CONFIG_DARWIN
static int os_darwin_cfrunloop_main(void)
{
    CFRunLoopRun();
    abort();
}

qemu_main_fn qemu_main = os_darwin_cfrunloop_main;
#else
qemu_main_fn qemu_main;
#endif

int main(int argc, char **argv)
{
    qemu_init(argc, argv);
    if (qemu_main) {
        qemu_run_default_main_on_new_thread();
        bql_unlock();
        return qemu_main();
    } else {
        qemu_default_main();
    }
}
