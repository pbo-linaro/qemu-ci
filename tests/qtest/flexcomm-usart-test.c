/*
 * Copyright (C) 2024 Google LLC
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include <glib/gstdio.h>

#include "io/channel-socket.h"
#include "qemu/config-file.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "qemu/sockets.h"
#include "sysemu/sysemu.h"
#include "qemu/main-loop.h"
#include "qemu/option.h"
#include "exec/memory.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"

#include "hw/misc/flexcomm.h"
#include "hw/arm/svd/flexcomm_usart.h"
#include "hw/arm/svd/rt500.h"
#include "reg-utils.h"

#define FLEXCOMM_BASE RT500_FLEXCOMM0_BASE
#define FLEXCOMM_USART_BASE RT500_FLEXCOMM0_BASE
#define DEVICE_NAME "/machine/soc/flexcomm0"

struct TestState {
    QTestState *qtest;
    QIOChannel *ioc;
};

static void polling_test(gconstpointer user_data)
{
    struct TestState *t = (struct TestState *)user_data;
    uint32_t tmp;
    char byte;
    int fifo_size;
    QDict *resp;

    resp = qmp("{\"execute\": \"system_reset\"}");
    qdict_unref(resp);

    /* select and lock USART */
    tmp = FIELD_DP32(FLEXCOMM_PERSEL_USART, FLEXCOMM_PSELID, LOCK, 1);
    REG32_WRITE(FLEXCOMM, PSELID, tmp);

    fifo_size = REG32_READ_FIELD(FLEXCOMM_USART, FIFOSIZE, FIFOSIZE);

    /* enable USART */
    REG32_WRITE_FIELD(FLEXCOMM_USART, CFG, ENABLE, 1);
    g_assert_cmpuint(REG32_READ_FIELD(FLEXCOMM_USART, CFG, ENABLE), ==, 1);

    /* enable TX and RX FIFO */
    REG32_WRITE_FIELD(FLEXCOMM_USART, FIFOCFG, ENABLETX, 1);
    g_assert_cmpuint(REG32_READ_FIELD(FLEXCOMM_USART, FIFOCFG, ENABLETX),
                     ==, 1);
    REG32_WRITE_FIELD(FLEXCOMM_USART, FIFOCFG, ENABLERX, 1);
    g_assert_cmpuint(REG32_READ_FIELD(FLEXCOMM_USART, FIFOCFG, ENABLERX),
                     ==, 1);

    /* test writes and fifo counters wrap */
    for (int i = 0; i < fifo_size / 2; i++) {
        /* check fifostat */
        g_assert_cmpuint(REG32_READ_FIELD(FLEXCOMM_USART, FIFOSTAT, RXFULL),
                         ==, 0);
        g_assert_cmpuint(REG32_READ_FIELD(FLEXCOMM_USART, FIFOSTAT, RXNOTEMPTY),
                         ==, 0);
        g_assert_cmpuint(REG32_READ_FIELD(FLEXCOMM_USART, FIFOSTAT, TXNOTFULL),
                         ==, 1);
        g_assert_cmpuint(REG32_READ_FIELD(FLEXCOMM_USART, FIFOSTAT, TXEMPTY),
                         ==, 1);

        REG32_WRITE(FLEXCOMM_USART, FIFOWR, 'a' + i);
        qio_channel_read(t->ioc, &byte, 1, &error_abort);
        g_assert_cmpuint(byte, ==, 'a' + i);
    }

    /* test reads and fifo level */

    for (int i = 0; i < fifo_size / 2; i++) {
        byte = 'A' + i;
        g_assert_cmpuint(qio_channel_write(t->ioc, &byte, 1, &error_abort),
                         ==, 1);
    }

    /* wait for the RXLVL to update */
    WAIT_REG32_FIELD(1000, FLEXCOMM_USART, FIFOSTAT, RXLVL,
                     fifo_size / 2);

    /* check fifo stat */
    g_assert_cmpuint(REG32_READ_FIELD(FLEXCOMM_USART, FIFOSTAT, RXFULL),
                     ==, 0);
    g_assert_cmpuint(REG32_READ_FIELD(FLEXCOMM_USART, FIFOSTAT, RXNOTEMPTY),
                     ==, 1);
    g_assert_cmpuint(REG32_READ_FIELD(FLEXCOMM_USART, FIFOSTAT, TXNOTFULL),
                     ==, 1);
    g_assert_cmpuint(REG32_READ_FIELD(FLEXCOMM_USART, FIFOSTAT, TXEMPTY),
                     ==, 1);

    /* send until FIFO is full */
    for (int i = fifo_size / 2; i < fifo_size; i++) {
        byte = 'A' + i;
        g_assert_cmpuint(qio_channel_write(t->ioc, &byte, 1, &error_abort),
                         ==, 1);
    }

    /* wait for the RXLVL to update */
    WAIT_REG32_FIELD(1000, FLEXCOMM_USART, FIFOSTAT, RXLVL, fifo_size);

    /* check fifo stat */
    g_assert_cmpuint(REG32_READ_FIELD(FLEXCOMM_USART, FIFOSTAT, RXFULL),
                     ==, 1);
    g_assert_cmpuint(REG32_READ_FIELD(FLEXCOMM_USART, FIFOSTAT, RXNOTEMPTY),
                     ==, 1);
    g_assert_cmpuint(REG32_READ_FIELD(FLEXCOMM_USART, FIFOSTAT, TXNOTFULL),
                     ==, 1);
    g_assert_cmpuint(REG32_READ_FIELD(FLEXCOMM_USART, FIFOSTAT, TXEMPTY),
                     ==, 1);

    /* check read no pop */
    g_assert_cmpuint(REG32_READ_FIELD(FLEXCOMM_USART, FIFORDNOPOP, RXDATA),
                     ==, 'A');

    /* now read from the fifo  */
    for (int i = 0; i < fifo_size; i++) {
        g_assert_cmpuint(REG32_READ_FIELD(FLEXCOMM_USART, FIFORD, RXDATA),
                         ==, 'A' + i);
    }

    /* check fifostat */
    g_assert_cmpuint(REG32_READ_FIELD(FLEXCOMM_USART, FIFOSTAT, RXFULL), ==, 0);
    g_assert_cmpuint(REG32_READ_FIELD(FLEXCOMM_USART, FIFOSTAT, RXNOTEMPTY),
                     ==, 0);
    g_assert_cmpuint(REG32_READ_FIELD(FLEXCOMM_USART, FIFOSTAT, TXNOTFULL),
                     ==, 1);
    g_assert_cmpuint(REG32_READ_FIELD(FLEXCOMM_USART, FIFOSTAT, TXEMPTY),
                     ==, 1);
}

static void irq_test(gconstpointer user_data)
{
    struct TestState *t = (struct TestState *)user_data;
    char buf[256] = { 0, };
    uint32_t tmp;
    QDict *resp;

    resp = qmp("{\"execute\": \"system_reset\"}");
    qdict_unref(resp);

    qtest_irq_intercept_out_named(t->qtest, DEVICE_NAME,
                                  SYSBUS_DEVICE_GPIO_IRQ);

    /* select and lock FLEXCOMM_USART */
    tmp = FIELD_DP32(FLEXCOMM_PERSEL_USART, FLEXCOMM_PSELID, LOCK, 1);
    REG32_WRITE(FLEXCOMM, PSELID, tmp);

    /*
     * set RX IRQ/DMA trigger level to 4 bytes - value 3 in FIFOTRIG
     *
     * 0000 - Trigger when the RX FIFO has received 1 entry (is no longer empty)
     * 0001 - Trigger when the RX FIFO has received 2 entries
     * 1111 - Trigger when the RX FIFO has received 16 entries (has become full)
     */
    REG32_WRITE_FIELD(FLEXCOMM_USART, FIFOTRIG, RXLVL, 3);
    g_assert_cmpuint(REG32_READ_FIELD(FLEXCOMM_USART, FIFOTRIG, RXLVL),
                     ==, 3);

    /* enable RX trigger for IRQ/DMA  */
    REG32_WRITE_FIELD(FLEXCOMM_USART, FIFOTRIG, RXLVLENA, 1);
    g_assert_cmpuint(REG32_READ_FIELD(FLEXCOMM_USART, FIFOTRIG, RXLVLENA),
                     ==, 1);

    /* enable RXLVL interrupt */
    REG32_WRITE_FIELD(FLEXCOMM_USART, FIFOINTENSET, RXLVL, 1);
    g_assert_cmpuint(REG32_READ_FIELD(FLEXCOMM_USART, FIFOINTENSET, RXLVL),
                     ==, 1);

    /* enable FLEXCOMM_USART */
    REG32_WRITE_FIELD(FLEXCOMM_USART, CFG, ENABLE, 1);
    g_assert_cmpuint(REG32_READ_FIELD(FLEXCOMM_USART, CFG, ENABLE),
                     ==, 1);

    /* enable TX and RX FIFO */
    REG32_WRITE_FIELD(FLEXCOMM_USART, FIFOCFG, ENABLETX, 1);
    g_assert_cmpuint(REG32_READ_FIELD(FLEXCOMM_USART, FIFOCFG, ENABLETX),
                     ==, 1);
    REG32_WRITE_FIELD(FLEXCOMM_USART, FIFOCFG, ENABLERX, 1);
    g_assert_cmpuint(REG32_READ_FIELD(FLEXCOMM_USART, FIFOCFG, ENABLERX),
                     ==, 1);

    /* check interrupt status */
    g_assert_cmpuint(REG32_READ_FIELD(FLEXCOMM_USART, FIFOINTSTAT, RXLVL),
                     ==, 0);
    g_assert_cmpuint(REG32_READ_FIELD(FLEXCOMM_USART, FIFOINTSTAT, TXLVL),
                     ==, 0);
    g_assert_false(get_irq(0));

    /* enable TX trigger for IRQ/DMA  */
    REG32_WRITE_FIELD(FLEXCOMM_USART, FIFOTRIG, TXLVLENA, 1);
    g_assert_cmpuint(REG32_READ_FIELD(FLEXCOMM_USART, FIFOTRIG, TXLVLENA),
                     ==, 1);

    /* enable irq for TX */
    REG32_WRITE_FIELD(FLEXCOMM_USART, FIFOINTENSET, TXLVL, 1);
    g_assert_cmpuint(REG32_READ_FIELD(FLEXCOMM_USART, FIFOINTENSET, TXLVL),
                     ==, 1);

    /* check TX irq */
    g_assert_cmpuint(REG32_READ_FIELD(FLEXCOMM_USART, FIFOINTSTAT, TXLVL),
                     ==, 1);
    g_assert_true(get_irq(0));

    /* disable irq for TX */
    REG32_WRITE_FIELD(FLEXCOMM_USART, FIFOTRIG, TXLVLENA, 0);
    g_assert_cmpuint(REG32_READ_FIELD(FLEXCOMM_USART, FIFOTRIG, TXLVLENA),
                     ==, 0);
    g_assert_cmpuint(REG32_READ_FIELD(FLEXCOMM_USART, FIFOINTSTAT, TXLVL),
                     ==, 0);
    g_assert_false(get_irq(0));

    /* send 3 bytes */
    g_assert_cmpuint(qio_channel_write(t->ioc, buf, 3, &error_abort),
                     ==, 3);

    /* check that we have 3 bytes in the fifo */
    WAIT_REG32_FIELD(1000, FLEXCOMM_USART, FIFOSTAT, RXLVL, 3);

    /* and no interrupt has been triggered yet */
    g_assert_cmpuint(REG32_READ_FIELD(FLEXCOMM_USART, FIFOINTSTAT, RXLVL),
                     ==, 0);
    g_assert_false(get_irq(0));

    /* push it over the edge */
    g_assert_cmpuint(qio_channel_write(t->ioc, buf, 1, &error_abort), ==, 1);

    /* check that we have 4 bytes in the fifo */
    WAIT_REG32_FIELD(1000, FLEXCOMM_USART, FIFOSTAT, RXLVL, 4);

    /* and the interrupt has been triggered */
    g_assert_cmpuint(REG32_READ_FIELD(FLEXCOMM_USART, FIFOINTSTAT, RXLVL),
                     ==, 1);
    g_assert_true(get_irq(0));

    /* read one byte from the fifo */
    g_assert_cmpuint(REG32_READ_FIELD(FLEXCOMM_USART, FIFORD, RXDATA),
                     ==, 0);

    /* we should have 3 bytes in the FIFO */
    g_assert_cmpuint(REG32_READ_FIELD(FLEXCOMM_USART, FIFOSTAT, RXLVL),
                     ==, 3);

    /* and no interrupts active */
    g_assert_cmpuint(REG32_READ_FIELD(FLEXCOMM_USART, FIFOINTSTAT, RXLVL),
             ==, 0);
    g_assert_false(get_irq(0));
}

static void close_ioc(void *ioc)
{
    qio_channel_close(ioc, NULL);
}

int main(int argc, char **argv)
{
    int ret;
    struct TestState test;
    char *tmp_path = g_dir_make_tmp("qemu-flexcomm-usart-test.XXXXXX", NULL);
    SocketAddress addr = {
        .type = SOCKET_ADDRESS_TYPE_UNIX,
        .u.q_unix.path = g_build_filename(tmp_path, "sock", NULL),
    };
    char *args;
    QIOChannelSocket *lioc;

    module_call_init(MODULE_INIT_QOM);
    g_test_init(&argc, &argv, NULL);

    lioc = qio_channel_socket_new();
    qio_channel_socket_listen_sync(lioc, &addr, 1, &error_abort);

    qtest_add_data_func("/flexcomm-usart/polling", &test, polling_test);
    qtest_add_data_func("/flexcomm-usart/irq", &test, irq_test);

    args = g_strdup_printf("-M rt595-evk "
                           "-chardev socket,id=flexcomm0-usart,path=%s",
                           addr.u.q_unix.path);
    test.qtest = qtest_start(args);

    qio_channel_wait(QIO_CHANNEL(lioc), G_IO_IN);
    test.ioc = QIO_CHANNEL(qio_channel_socket_accept(lioc, &error_abort));
    g_assert(test.ioc);
    qtest_add_abrt_handler(close_ioc, test.ioc);

    ret = g_test_run();

    qtest_end();

    qtest_remove_abrt_handler(test.ioc);
    g_unlink(addr.u.q_unix.path);
    g_free(addr.u.q_unix.path);
    g_rmdir(tmp_path);
    g_free(tmp_path);
    g_free(args);
    object_unref(OBJECT(test.ioc));
    object_unref(OBJECT(lioc));

    return ret;
}
