/*
 * Copyright (c) 2024 Google LLC.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"

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
#include "hw/qdev-core.h"

#include "hw/misc/flexcomm.h"
#include "hw/arm/svd/flexcomm_spi.h"
#include "hw/arm/svd/rt500.h"
#include "reg-utils.h"

/* The number of words sent on the SPI in loopback mode. */
#define SEQ_LOOPBACK_MODE   (8)

/* This value is used to set the cycle counter for the spi tester */
#define SPI_TESTER_CONFIG (0x10)

#define FLEXCOMM_BASE RT500_FLEXCOMM0_BASE
#define FLEXCOMM_SPI_BASE RT500_FLEXCOMM0_BASE
#define DEVICE_NAME "/machine/soc/flexcomm0"

static void configure_spi(bool master, bool is_loopback_mode)
{
    uint32_t tmp;

    /* Select and lock SPI */
    tmp = FLEXCOMM_PERSEL_SPI;
    FIELD_DP32(tmp, FLEXCOMM_PSELID, LOCK, 1);
    REG32_WRITE(FLEXCOMM, PSELID, tmp);

    /* Disable the FIFO */
    REG32_WRITE_FIELD(FLEXCOMM_SPI, CFG, ENABLE, 0);
    REG32_WRITE_FIELD(FLEXCOMM_SPI, FIFOCFG, ENABLETX, 0);
    REG32_WRITE_FIELD(FLEXCOMM_SPI, FIFOCFG, ENABLERX, 0);

    if (is_loopback_mode) {
        /* Set up SPI interface - loop mode, master mode */
        REG32_WRITE_FIELD(FLEXCOMM_SPI, CFG, LOOP, 1);
        g_assert(REG32_READ_FIELD(FLEXCOMM_SPI, CFG, LOOP) == 1);
    }

    if (master) {
        REG32_WRITE_FIELD(FLEXCOMM_SPI, CFG, MASTER, 1);
        g_assert(REG32_READ_FIELD(FLEXCOMM_SPI, CFG, MASTER) == 1);
    } else {
        REG32_WRITE_FIELD(FLEXCOMM_SPI, CFG, MASTER, 0);
        g_assert(REG32_READ_FIELD(FLEXCOMM_SPI, CFG, MASTER) == 0);
    }

    /* Enable the FIFO */
    REG32_WRITE_FIELD(FLEXCOMM_SPI, FIFOCFG, ENABLETX, 1);
    REG32_WRITE_FIELD(FLEXCOMM_SPI, FIFOCFG, ENABLERX, 1);

    /* Enable the SPI */
    REG32_WRITE_FIELD(FLEXCOMM_SPI, CFG, ENABLE, 1);
    g_assert(REG32_READ_FIELD(FLEXCOMM_SPI, CFG, ENABLE) == 1);
}

/* The SPI controller running in master mode can run in loopback mode for */
/* internal testing. Transmit and receive lines are connected together. */
static void loopback_test(gconstpointer user_data)
{
    configure_spi(true, true);

    /* Write a sequence */
    for (int i = 0; i < SEQ_LOOPBACK_MODE; i++) {
        REG32_WRITE(FLEXCOMM_SPI, FIFOWR, i);
    }

    /* Read the sequence back */
    for (int i = 0; i < SEQ_LOOPBACK_MODE; i++) {
        g_assert(REG32_READ_FIELD(FLEXCOMM_SPI, FIFORD, RXDATA) == i);
    }
}

static void master_test(gconstpointer user_data)
{
    uint32_t tmp;

    configure_spi(true, false);

    REG32_WRITE_FIELD(FLEXCOMM_SPI, CFG, LSBF, 1);

    /* single 16bit word transfer */

    tmp = FIELD_DP32(0x1122, FLEXCOMM_SPI_FIFOWR, EOT, 1);
    tmp = FIELD_DP32(tmp, FLEXCOMM_SPI_FIFOWR, TXSSEL0_N, 1);
    tmp = FIELD_DP32(tmp, FLEXCOMM_SPI_FIFOWR, LEN, 0xF);
    REG32_WRITE(FLEXCOMM_SPI, FIFOWR, tmp);
    g_assert(REG32_READ_FIELD(FLEXCOMM_SPI, FIFOSTAT, RXNOTEMPTY) == 1);
    g_assert_cmpuint(REG32_READ_FIELD(FLEXCOMM_SPI, FIFORD, RXDATA),
                     ==, 0x1122);
    g_assert(REG32_READ_FIELD(FLEXCOMM_SPI, FIFOSTAT, RXNOTEMPTY) == 0);

    /* multi word 8 bits transfer */

    tmp = FIELD_DP32(0x11, FLEXCOMM_SPI_FIFOWR, TXSSEL0_N, 1);
    tmp = FIELD_DP32(tmp, FLEXCOMM_SPI_FIFOWR, LEN, 0x7);
    REG32_WRITE(FLEXCOMM_SPI, FIFOWR, tmp);
    tmp = 0x22;
    FIELD_DP32(tmp, FLEXCOMM_SPI_FIFOWR, EOT, 1);
    FIELD_DP32(tmp, FLEXCOMM_SPI_FIFOWR, TXSSEL0_N, 1);
    FIELD_DP32(tmp, FLEXCOMM_SPI_FIFOWR, LEN, 0x7);
    REG32_WRITE(FLEXCOMM_SPI, FIFOWR, tmp);
    g_assert(REG32_READ_FIELD(FLEXCOMM_SPI, FIFOSTAT, RXNOTEMPTY) == 1);
    g_assert(REG32_READ_FIELD(FLEXCOMM_SPI, FIFORD, RXDATA) == 0x11);
    g_assert(REG32_READ_FIELD(FLEXCOMM_SPI, FIFOSTAT, RXNOTEMPTY) == 1);
    g_assert(REG32_READ_FIELD(FLEXCOMM_SPI, FIFORD, RXDATA) == 0x22);
    g_assert(REG32_READ_FIELD(FLEXCOMM_SPI, FIFOSTAT, RXNOTEMPTY) == 0);
}

int main(int argc, char **argv)
{
    int ret;

    module_call_init(MODULE_INIT_QOM);
    g_test_init(&argc, &argv, NULL);

    qtest_add_data_func("/flexcomm-spi/loopack", NULL, loopback_test);
    qtest_add_data_func("/flexcomm-spi/master", NULL, master_test);

    qtest_start("-M rt595-evk -device spi-tester,bus=/flexcomm0-spi");
    ret = g_test_run();
    qtest_end();

    return ret;
}
