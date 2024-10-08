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
#include "hw/arm/svd/flexcomm_i2c.h"
#include "hw/arm/svd/rt500.h"
#include "hw/misc/i2c_tester.h"
#include "reg-utils.h"

#define PERIPH_ADDR (0x50)
#define INVALID_ADDR (0x10)

#define REG_ADDR 11
#define REG_VALUE 0xAA

#define FLEXCOMM_BASE RT500_FLEXCOMM0_BASE
#define FLEXCOMM_I2C_BASE RT500_FLEXCOMM0_BASE
#define DEVICE_NAME "/machine/soc/flexcomm0"

struct TestState {
    QTestState *qtest;
};

static void master_test(gconstpointer user_data)
{
    struct TestState *t = (struct TestState *)user_data;
    uint32_t tmp;

    qtest_irq_intercept_out_named(t->qtest, DEVICE_NAME,
                                  SYSBUS_DEVICE_GPIO_IRQ);

    /* Select and lock I2C */
    tmp = FLEXCOMM_PERSEL_I2C;
    FIELD_DP32(tmp, FLEXCOMM_PSELID, LOCK, 1);
    REG32_WRITE(FLEXCOMM, PSELID, tmp);

    /* Enable master mode */
    REG32_WRITE_FIELD(FLEXCOMM_I2C, CFG, MSTEN, 1);
    g_assert(REG32_READ_FIELD(FLEXCOMM_I2C, CFG, MSTEN) == 1);

    g_assert(REG32_READ_FIELD(FLEXCOMM_I2C, STAT, MSTPENDING) == 1);
    g_assert(REG32_READ_FIELD(FLEXCOMM_I2C, STAT, MSTSTATE) ==
             MSTSTATE_IDLE);

    /* Enable interrupts */
    REG32_WRITE_FIELD(FLEXCOMM_I2C, INTENSET, MSTPENDINGEN, 1);
    g_assert_true(get_irq(0));

    /* start for invalid address  */
    REG32_WRITE(FLEXCOMM_I2C, MSTDAT, INVALID_ADDR);
    REG32_WRITE_FIELD_NOUPDATE(FLEXCOMM_I2C, MSTCTL, MSTSTART, 1);
    g_assert(REG32_READ_FIELD(FLEXCOMM_I2C, STAT, MSTSTATE) ==
             MSTSTATE_NAKADR);
    g_assert_true(get_irq(0));
    REG32_WRITE_FIELD_NOUPDATE(FLEXCOMM_I2C, MSTCTL, MSTSTOP, 1);

    /* write past the last register */
    REG32_WRITE_FIELD(FLEXCOMM_I2C, MSTDAT, DATA, PERIPH_ADDR);
    REG32_WRITE_FIELD_NOUPDATE(FLEXCOMM_I2C, MSTCTL, MSTSTART, 1);
    g_assert_true(get_irq(0));
    g_assert(REG32_READ_FIELD(FLEXCOMM_I2C, STAT, MSTSTATE) ==
             MSTSTATE_TXRDY);
    REG32_WRITE_FIELD(FLEXCOMM_I2C, MSTDAT, DATA, I2C_TESTER_NUM_REGS + 10);
    REG32_WRITE_FIELD_NOUPDATE(FLEXCOMM_I2C, MSTCTL, MSTCONTINUE, 1);
    g_assert_true(get_irq(0));
    g_assert(REG32_READ_FIELD(FLEXCOMM_I2C, STAT, MSTSTATE) ==
             MSTSTATE_TXRDY);
    REG32_WRITE_FIELD_NOUPDATE(FLEXCOMM_I2C, MSTCTL, MSTCONTINUE, 1);
    g_assert_true(get_irq(0));
    g_assert(REG32_READ_FIELD(FLEXCOMM_I2C, STAT, MSTSTATE) ==
             MSTSTATE_NAKDAT);
    REG32_WRITE_FIELD_NOUPDATE(FLEXCOMM_I2C, MSTCTL, MSTSTOP, 1);

    /* write value to register */
    REG32_WRITE_FIELD(FLEXCOMM_I2C, MSTDAT, DATA, PERIPH_ADDR);
    REG32_WRITE_FIELD_NOUPDATE(FLEXCOMM_I2C, MSTCTL, MSTSTART, 1);
    g_assert_true(get_irq(0));
    g_assert(REG32_READ_FIELD(FLEXCOMM_I2C, STAT, MSTSTATE) ==
             MSTSTATE_TXRDY);
    REG32_WRITE_FIELD(FLEXCOMM_I2C, MSTDAT, DATA, REG_ADDR);
    REG32_WRITE_FIELD_NOUPDATE(FLEXCOMM_I2C, MSTCTL, MSTCONTINUE, 1);
    g_assert_true(get_irq(0));
    g_assert(REG32_READ_FIELD(FLEXCOMM_I2C, STAT, MSTSTATE) ==
             MSTSTATE_TXRDY);
    REG32_WRITE_FIELD(FLEXCOMM_I2C, MSTDAT, DATA, REG_VALUE);
    REG32_WRITE_FIELD_NOUPDATE(FLEXCOMM_I2C, MSTCTL, MSTCONTINUE, 1);
    g_assert_true(get_irq(0));
    g_assert(REG32_READ_FIELD(FLEXCOMM_I2C, STAT, MSTSTATE) ==
             MSTSTATE_TXRDY);
    REG32_WRITE_FIELD_NOUPDATE(FLEXCOMM_I2C, MSTCTL, MSTSTOP, 1);
    g_assert_true(get_irq(0));
    g_assert(REG32_READ_FIELD(FLEXCOMM_I2C, STAT, MSTSTATE) ==
             MSTSTATE_IDLE);

    /* read value back from register */
    REG32_WRITE_FIELD(FLEXCOMM_I2C, MSTDAT, DATA, PERIPH_ADDR);
    REG32_WRITE_FIELD_NOUPDATE(FLEXCOMM_I2C, MSTCTL, MSTSTART, 1);
    g_assert_true(get_irq(0));
    g_assert(REG32_READ_FIELD(FLEXCOMM_I2C, STAT, MSTSTATE) ==
             MSTSTATE_TXRDY);
    REG32_WRITE_FIELD(FLEXCOMM_I2C, MSTDAT, DATA, REG_ADDR);
    REG32_WRITE_FIELD_NOUPDATE(FLEXCOMM_I2C, MSTCTL, MSTCONTINUE, 1);
    g_assert_true(get_irq(0));
    g_assert(REG32_READ_FIELD(FLEXCOMM_I2C, STAT, MSTSTATE) ==
             MSTSTATE_TXRDY);
    REG32_WRITE_FIELD(FLEXCOMM_I2C, MSTDAT, DATA, (PERIPH_ADDR + 1));
    REG32_WRITE_FIELD_NOUPDATE(FLEXCOMM_I2C, MSTCTL, MSTSTART, 1);
    g_assert_true(get_irq(0));
    g_assert(REG32_READ_FIELD(FLEXCOMM_I2C, STAT, MSTSTATE) ==
             MSTSTATE_RXRDY);
    g_assert_cmpuint(REG32_READ_FIELD(FLEXCOMM_I2C, MSTDAT, DATA), ==,
                     REG_VALUE);
    REG32_WRITE_FIELD_NOUPDATE(FLEXCOMM_I2C, MSTCTL, MSTSTOP, 1);

    /*
     * Check that the master ended the transaction (i.e. i2c_end_transfer was
     * called). If the master does not properly end the transaction this would
     * be seen as a restart and it would not be NACKed.
     */
    REG32_WRITE_FIELD(FLEXCOMM_I2C, MSTDAT, DATA, INVALID_ADDR);
    REG32_WRITE_FIELD_NOUPDATE(FLEXCOMM_I2C, MSTCTL, MSTSTART, 1);

    g_assert(REG32_READ_FIELD(FLEXCOMM_I2C, STAT, MSTSTATE) ==
             MSTSTATE_NAKADR);
    g_assert_true(get_irq(0));
    REG32_WRITE_FIELD_NOUPDATE(FLEXCOMM_I2C, MSTCTL, MSTSTOP, 1);

    /* Disable interrupts */
    REG32_WRITE_FIELD(FLEXCOMM_I2C, INTENCLR, MSTPENDINGCLR, 1);
    g_assert_false(get_irq(0));
}

int main(int argc, char **argv)
{
    int ret;
    struct TestState test;

    module_call_init(MODULE_INIT_QOM);
    g_test_init(&argc, &argv, NULL);

    qtest_add_data_func("/flexcomm-i2c/master", &test, master_test);

    test.qtest = qtest_start("-M rt595-evk "
                          "-device i2c-tester,address=0x50,bus=/flexcomm0-i2c");
    ret = g_test_run();
    qtest_end();

    return ret;
}
