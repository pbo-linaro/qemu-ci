/*
 * Copyright (C) 2024 Google LLC
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/main-loop.h"
#include "exec/memory.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"

#include "hw/misc/flexcomm.h"
#include "hw/arm/svd/flexcomm.h"
#include "hw/arm/svd/rt500.h"
#include "reg-utils.h"

#define FLEXCOMM_BASE RT500_FLEXCOMM0_BASE

static void select_test(gconstpointer data)
{
    static const unsigned persel[] = {
        FLEXCOMM_PERSEL_USART,
        FLEXCOMM_PERSEL_SPI,
        FLEXCOMM_PERSEL_I2C,
    };

    g_assert(REG32_READ_FIELD(FLEXCOMM, PSELID, PERSEL) == 0);

    /* no register access until a function is selected  */
    readl_fail(FLEXCOMM_BASE);
    writel_fail(FLEXCOMM_BASE, 0);

    for (int i = 0; i < ARRAY_SIZE(persel); i++) {

        REG32_WRITE(FLEXCOMM, PSELID, persel[i]);
        g_assert_cmpuint(REG32_READ_FIELD(FLEXCOMM, PSELID, PERSEL), ==,
                         persel[i]);

        /* test that we can access function registers */
        writel(FLEXCOMM_BASE, 0xabcd);
        readl(FLEXCOMM_BASE);
    }

    /* try to select something invalid */
    REG32_WRITE(FLEXCOMM, PSELID, 7);
    /* check for no function selected */
    g_assert_cmpuint(REG32_READ_FIELD(FLEXCOMM, PSELID, PERSEL), ==, 0);

    /* now select and lock USART */
    REG32_WRITE(FLEXCOMM, PSELID,
                FIELD_DP32(FLEXCOMM_PERSEL_USART, FLEXCOMM_PSELID, LOCK, 1));
    g_assert_cmpuint(REG32_READ_FIELD(FLEXCOMM, PSELID, PERSEL), ==,
                     FLEXCOMM_PERSEL_USART);
    g_assert_cmpuint(REG32_READ_FIELD(FLEXCOMM, PSELID, LOCK), ==, 1);

    /* try to change the selection to spi */
    REG32_WRITE(FLEXCOMM, PSELID, FLEXCOMM_PERSEL_SPI);
    /* it should still be locked USART */
    g_assert_cmpuint(REG32_READ_FIELD(FLEXCOMM, PSELID, PERSEL), ==,
                     FLEXCOMM_PERSEL_USART);
    g_assert_cmpuint(REG32_READ_FIELD(FLEXCOMM, PSELID, LOCK), ==, 1);
}

int main(int argc, char **argv)
{
    int ret;

    g_test_init(&argc, &argv, NULL);

    qtest_add_data_func("/flexcomm/select", NULL, select_test);
    qtest_start("-M rt595-evk");
    ret = g_test_run();
    qtest_end();

    return ret;
}
