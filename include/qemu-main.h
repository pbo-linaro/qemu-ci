/*
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#ifndef QEMU_MAIN_H
#define QEMU_MAIN_H

/*
 * The function to run on the main (initial) thread of the process.
 * NULL means QEMU's main event loop.
 * When non-NULL, QEMU's main event loop will run on a purposely created
 * thread, after which the provided function pointer will be invoked on
 * the initial thread.
 * This is useful on platforms which treat the main thread as special
 * (macOS/Darwin) and/or require all UI API calls to occur from a
 * specific thread.
 * Implementing this via a global function pointer variable is a bit
 * ugly, but it's probably worth investigating the existing UI thread rule
 * violations in the SDL (e.g. #2537) and GTK+ back-ends. Fixing those
 * issues might precipitate requirements similar but not identical to those
 * of the Cocoa UI; hopefully we'll see some kind of pattern emerge, which
 * can then be used as a basis for an overhaul. (In fact, it may turn
 * out to be simplest to split the UI/native platform event thread from the
 * QEMU main event loop on all platforms, with any UI or even none at all.)
 */
extern qemu_main_fn qemu_main;

#endif /* QEMU_MAIN_H */
