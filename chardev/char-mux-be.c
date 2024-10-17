/*
 * QEMU Character Backend Multiplexer
 *
 * Author: Roman Penyaev <r.peniaev@gmail.com>
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
#include "qapi/error.h"
#include "qemu/module.h"
#include "qemu/option.h"
#include "qemu/cutils.h"
#include "chardev/char.h"
#include "sysemu/block-backend.h"
#include "qapi/qapi-commands-control.h"
#include "qapi/clone-visitor.h"
#include "qapi/qapi-builtin-visit.h"
#include "chardev-internal.h"

/*
 * MUX-BE driver for multiplexing 1 frontend device with N backend devices
 */

/*
 * Write to all backends. Different backend devices accept data with
 * various rate, so it is quite possible that one device returns less,
 * then others. In this case we return minimum to the caller,
 * expecting caller will repeat operation soon. When repeat happens
 * send to the devices which consume data faster must be avoided
 * for obvious reasons not to send data, which was already sent.
 */
static int mux_be_chr_write_to_all(MuxBeChardev *d, const uint8_t *buf, int len)
{
    int r, i, ret = len;
    unsigned int written;

    for (i = 0; i < d->be_cnt; i++) {
        written = d->be_written[i] - d->be_min_written;
        if (written) {
            /* Written in the previous call so take into account */
            ret = MIN(written, ret);
            continue;
        }
        r = qemu_chr_fe_write(&d->backends[i], buf, len);
        if (r < 0 && errno == EAGAIN) {
            /*
             * Fail immediately if write would block. Expect to be called
             * soon on watch wake up.
             */
            return r;
        } else if (r < 0) {
            /*
             * Ignore all other errors and pretend the entire buffer is
             * written to avoid this chardev being watched. This device
             * becomes disabled until the following write succeeds, but
             * writing continues to others.
             */
            r = len;
        }
        d->be_written[i] += r;
        ret = MIN(r, ret);
    }
    d->be_min_written += ret;

    return ret;
}

/* Called with chr_write_lock held.  */
static int mux_be_chr_write(Chardev *chr, const uint8_t *buf, int len)
{
    MuxBeChardev *d = MUX_BE_CHARDEV(chr);
    return mux_be_chr_write_to_all(d, buf, len);
}

static void mux_be_chr_send_event(MuxBeChardev *d, QEMUChrEvent event)
{
    CharBackend *fe = d->frontend;

    if (fe && fe->chr_event) {
        fe->chr_event(fe->opaque, event);
    }
}

static void mux_be_chr_be_event(Chardev *chr, QEMUChrEvent event)
{
    MuxBeChardev *d = MUX_BE_CHARDEV(chr);

    mux_be_chr_send_event(d, event);
}

static int mux_be_chr_can_read(void *opaque)
{
    MuxBeChardev *d = MUX_BE_CHARDEV(opaque);
    CharBackend *fe = d->frontend;

    if (fe && fe->chr_can_read) {
        return fe->chr_can_read(fe->opaque);
    }

    return 0;
}

static void mux_be_chr_read(void *opaque, const uint8_t *buf, int size)
{
    MuxBeChardev *d = MUX_BE_CHARDEV(opaque);
    CharBackend *fe = d->frontend;

    if (fe && fe->chr_read) {
        fe->chr_read(fe->opaque, buf, size);
    }
}

void mux_be_chr_send_all_event(MuxBeChardev *d, QEMUChrEvent event)
{
    mux_be_chr_send_event(d, event);
}

static void mux_be_chr_event(void *opaque, QEMUChrEvent event)
{
    mux_chr_send_all_event(CHARDEV(opaque), event);
}

static GSource *mux_be_chr_add_watch(Chardev *s, GIOCondition cond)
{
    MuxBeChardev *d = MUX_BE_CHARDEV(s);
    Chardev *chr;
    ChardevClass *cc;
    unsigned int written;
    int i;

    for (i = 0; i < d->be_cnt; i++) {
        written = d->be_written[i] - d->be_min_written;
        if (written) {
            /* We skip the device with already written buffer */
            continue;
        }

        /*
         * The first device that has no data written to it must be
         * the device that recently returned EAGAIN and should be
         * watched.
         */

        chr = qemu_chr_fe_get_driver(&d->backends[i]);
        cc = CHARDEV_GET_CLASS(chr);

        if (!cc->chr_add_watch) {
            return NULL;
        }

        return cc->chr_add_watch(chr, cond);
    }

    return NULL;
}

bool mux_be_chr_attach_chardev(MuxBeChardev *d, Chardev *chr, Error **errp)
{
    bool ret;

    if (d->be_cnt >= MAX_MUX) {
        error_setg(errp, "too many uses of multiplexed chardev '%s'"
                   " (maximum is " stringify(MAX_MUX) ")",
                   d->parent.label);
        return false;
    }
    ret = qemu_chr_fe_init(&d->backends[d->be_cnt], chr, errp);
    if (ret) {
        /* Catch up with what was already written */
        d->be_written[d->be_cnt] = d->be_min_written;
        d->be_cnt += 1;
    }

    return ret;
}

static void char_mux_be_finalize(Object *obj)
{
    MuxBeChardev *d = MUX_BE_CHARDEV(obj);
    CharBackend *fe = d->frontend;
    int i;

    if (fe) {
        fe->chr = NULL;
    }
    for (i = 0; i < d->be_cnt; i++) {
        qemu_chr_fe_deinit(&d->backends[i], false);
    }
}

static void mux_be_chr_update_read_handlers(Chardev *chr)
{
    MuxBeChardev *d = MUX_BE_CHARDEV(chr);
    int i;

    for (i = 0; i < d->be_cnt; i++) {
        /* Fix up the real driver with mux routines */
        qemu_chr_fe_set_handlers_full(&d->backends[i],
                                      mux_be_chr_can_read,
                                      mux_be_chr_read,
                                      mux_be_chr_event,
                                      NULL,
                                      chr,
                                      chr->gcontext, true, false);
    }
}

bool mux_be_chr_attach_frontend(MuxBeChardev *d, CharBackend *b, Error **errp)
{
    if (d->frontend) {
        error_setg(errp,
                   "multiplexed chardev '%s' is already used "
                   "for multiplexing", d->parent.label);
        return false;
    }
    d->frontend = b;

    return true;
}

void mux_be_chr_detach_frontend(MuxBeChardev *d)
{
    d->frontend = NULL;
}

static void qemu_chr_open_mux_be(Chardev *chr,
                                 ChardevBackend *backend,
                                 bool *be_opened,
                                 Error **errp)
{
    /*
     * Only default to opened state if we've realized the initial
     * set of muxes
     */
    *be_opened = mux_is_opened();
}

static void qemu_chr_parse_mux_be(QemuOpts *opts, ChardevBackend *backend,
                                  Error **errp)
{
    ChardevMuxBe *mux;

    backend->type = CHARDEV_BACKEND_KIND_MUX_BE;
    mux = backend->u.mux_be.data = g_new0(ChardevMuxBe, 1);
    qemu_chr_parse_common(opts, qapi_ChardevMuxBe_base(mux));
}

static void char_mux_be_class_init(ObjectClass *oc, void *data)
{
    ChardevClass *cc = CHARDEV_CLASS(oc);

    cc->parse = qemu_chr_parse_mux_be;
    cc->open = qemu_chr_open_mux_be;
    cc->chr_write = mux_be_chr_write;
    cc->chr_add_watch = mux_be_chr_add_watch;
    cc->chr_be_event = mux_be_chr_be_event;
    cc->chr_update_read_handler = mux_be_chr_update_read_handlers;
}

static const TypeInfo char_mux_be_type_info = {
    .name = TYPE_CHARDEV_MUX_BE,
    .parent = TYPE_CHARDEV,
    .class_init = char_mux_be_class_init,
    .instance_size = sizeof(MuxBeChardev),
    .instance_finalize = char_mux_be_finalize,
};

static void register_types(void)
{
    type_register_static(&char_mux_be_type_info);
}

type_init(register_types);
