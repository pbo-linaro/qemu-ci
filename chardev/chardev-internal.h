/*
 * QEMU Character device internals
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
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

#ifndef CHARDEV_INTERNAL_H
#define CHARDEV_INTERNAL_H

#include "chardev/char.h"
#include "chardev/char-fe.h"
#include "qom/object.h"

#define MAX_MUX 4
#define MUX_BUFFER_SIZE 32 /* Must be a power of 2.  */
#define MUX_BUFFER_MASK (MUX_BUFFER_SIZE - 1)

struct MuxFeChardev {
    Chardev parent;
    /* Linked frontends */
    CharBackend *backends[MAX_MUX];
    /* Linked backend */
    CharBackend chr;
    unsigned long mux_bitset;
    int focus;
    bool term_got_escape;
    /* Intermediate input buffer catches escape sequences even if the
       currently active device is not accepting any input - but only until it
       is full as well. */
    unsigned char buffer[MAX_MUX][MUX_BUFFER_SIZE];
    unsigned int prod[MAX_MUX];
    unsigned int cons[MAX_MUX];
    int timestamps;

    /* Protected by the Chardev chr_write_lock.  */
    bool linestart;
    int64_t timestamps_start;
};
typedef struct MuxFeChardev MuxFeChardev;

struct MuxBeChardev {
    Chardev parent;
    /* Linked frontend */
    CharBackend *frontend;
    /* Linked backends */
    CharBackend backends[MAX_MUX];
    /*
     * Number of backends attached to this mux. Once attached, a
     * backend can't be detached, so the counter is only increasing.
     * To safely remove a backend, mux has to be removed first.
     */
    unsigned int be_cnt;
    /*
     * Counters of written bytes from a single frontend device
     * to multiple backend devices.
     */
    unsigned int be_written[MAX_MUX];
    unsigned int be_min_written;
};
typedef struct MuxBeChardev MuxBeChardev;

DECLARE_INSTANCE_CHECKER(MuxFeChardev, MUX_FE_CHARDEV,
                         TYPE_CHARDEV_MUX_FE)
DECLARE_INSTANCE_CHECKER(MuxBeChardev, MUX_BE_CHARDEV,
                         TYPE_CHARDEV_MUX_BE)

#define CHARDEV_IS_MUX_FE(chr)                              \
    object_dynamic_cast(OBJECT(chr), TYPE_CHARDEV_MUX_FE)
#define CHARDEV_IS_MUX_BE(chr)                              \
    object_dynamic_cast(OBJECT(chr), TYPE_CHARDEV_MUX_BE)

void mux_chr_send_all_event(Chardev *chr, QEMUChrEvent event);

/* Mux type dependent calls */
void mux_fe_chr_set_focus(Chardev *d, unsigned int focus);
void mux_fe_chr_send_all_event(MuxFeChardev *d, QEMUChrEvent event);
bool mux_fe_chr_attach_frontend(MuxFeChardev *d, CharBackend *b,
                                unsigned int *tag, Error **errp);
bool mux_fe_chr_detach_frontend(MuxFeChardev *d, unsigned int tag);
void mux_be_chr_send_all_event(MuxBeChardev *d, QEMUChrEvent event);
bool mux_be_chr_attach_chardev(MuxBeChardev *d, Chardev *chr, Error **errp);
bool mux_be_chr_attach_frontend(MuxBeChardev *d, CharBackend *b, Error **errp);
void mux_be_chr_detach_frontend(MuxBeChardev *d);

Object *get_chardevs_root(void);

#endif /* CHARDEV_INTERNAL_H */
