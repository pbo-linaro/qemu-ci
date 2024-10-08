/*
 * Copyright (c) 2024 Google LLC
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
#include "hw/clock.h"
#include "hw/irq.h"
#include "hw/qdev-clock.h"
#include "hw/qdev-properties.h"

#include "hw/misc/rt500_clkctl0.h"
#include "hw/misc/rt500_clkctl1.h"
#include "hw/misc/rt500_clk_freqs.h"
#include "hw/arm/svd/rt500.h"
#include "reg-utils.h"

#define SYSCLK_HZ 200000000
#define CLKCTL0_NAME "/machine/soc/clkctl0"
#define CLKCTL1_NAME "/machine/soc/clkctl1"

static void pscctl_test(gconstpointer user_data)
{
    /* rom controller clock should be enabled at reset */
    g_assert(REG32_READ_FIELD(RT500_CLKCTL0, PSCCTL0, ROM_CTRLR_CLK) == 1);

    /* DSP clk is disabled at reset */
    g_assert(REG32_READ_FIELD(RT500_CLKCTL0, PSCCTL0, DSP_CLK) == 0);

    /* check PSCTL_SET functionality */
    REG32_WRITE_FIELD_NOUPDATE(RT500_CLKCTL0, PSCCTL0_SET, DSP_CLK, 1);
    g_assert(REG32_READ_FIELD(RT500_CLKCTL0, PSCCTL0, DSP_CLK) == 1);

    /* check PSCTL_CLR functionality */
    REG32_WRITE_FIELD_NOUPDATE(RT500_CLKCTL0, PSCCTL0_CLR, DSP_CLK, 1);
    g_assert(REG32_READ_FIELD(RT500_CLKCTL0, PSCCTL0, DSP_CLK) == 0);

    /* FLEXIO clk is disabled at reset */
    g_assert(REG32_READ_FIELD(RT500_CLKCTL1, PSCCTL0, FlexIO) == 0);

    /* check PSCTL_SET functionality */
    REG32_WRITE_FIELD_NOUPDATE(RT500_CLKCTL1, PSCCTL0_SET, FlexIO, 1);
    g_assert(REG32_READ_FIELD(RT500_CLKCTL1, PSCCTL0, FlexIO) == 1);

    /* check PSCTL_CLR functionality */
    REG32_WRITE_FIELD_NOUPDATE(RT500_CLKCTL1, PSCCTL0_CLR, FlexIO, 1);
    g_assert(REG32_READ_FIELD(RT500_CLKCTL1, PSCCTL0, FlexIO) == 0);
}

static void audiopll0pfd_test(gconstpointer user_data)
{
    /*  audio plls are gated at boot */
    g_assert(REG32_READ_FIELD(RT500_CLKCTL1, AUDIOPLL0PFD, PFD3_CLKGATE) == 1);
    g_assert(REG32_READ_FIELD(RT500_CLKCTL1, AUDIOPLL0PFD, PFD2_CLKGATE) == 1);
    g_assert(REG32_READ_FIELD(RT500_CLKCTL1, AUDIOPLL0PFD, PFD1_CLKGATE) == 1);
    g_assert(REG32_READ_FIELD(RT500_CLKCTL1, AUDIOPLL0PFD, PFD0_CLKGATE) == 1);

    /*  ,,, and clocks are not ready */
    g_assert(REG32_READ_FIELD(RT500_CLKCTL1, AUDIOPLL0PFD, PFD3_CLKRDY) == 0);
    g_assert(REG32_READ_FIELD(RT500_CLKCTL1, AUDIOPLL0PFD, PFD2_CLKRDY) == 0);
    g_assert(REG32_READ_FIELD(RT500_CLKCTL1, AUDIOPLL0PFD, PFD1_CLKRDY) == 0);
    g_assert(REG32_READ_FIELD(RT500_CLKCTL1, AUDIOPLL0PFD, PFD0_CLKRDY) == 0);

    /* ungate all plls and check that clocks are ready */
    REG32_WRITE_FIELD(RT500_CLKCTL1, AUDIOPLL0PFD, PFD3_CLKGATE, 0);
    REG32_WRITE_FIELD(RT500_CLKCTL1, AUDIOPLL0PFD, PFD2_CLKGATE, 0);
    REG32_WRITE_FIELD(RT500_CLKCTL1, AUDIOPLL0PFD, PFD1_CLKGATE, 0);
    REG32_WRITE_FIELD(RT500_CLKCTL1, AUDIOPLL0PFD, PFD0_CLKGATE, 0);

    g_assert(REG32_READ_FIELD(RT500_CLKCTL1, AUDIOPLL0PFD, PFD3_CLKRDY) == 1);
    g_assert(REG32_READ_FIELD(RT500_CLKCTL1, AUDIOPLL0PFD, PFD2_CLKRDY) == 1);
    g_assert(REG32_READ_FIELD(RT500_CLKCTL1, AUDIOPLL0PFD, PFD1_CLKRDY) == 1);
    g_assert(REG32_READ_FIELD(RT500_CLKCTL1, AUDIOPLL0PFD, PFD0_CLKRDY) == 1);
}

static void syspll0pfd_test(gconstpointer user_data)
{
    /*  system plls are gated at boot */
    g_assert(REG32_READ_FIELD(RT500_CLKCTL0, SYSPLL0PFD, PFD3_CLKGATE) == 1);
    g_assert(REG32_READ_FIELD(RT500_CLKCTL0, SYSPLL0PFD, PFD2_CLKGATE) == 1);
    g_assert(REG32_READ_FIELD(RT500_CLKCTL0, SYSPLL0PFD, PFD1_CLKGATE) == 1);
    g_assert(REG32_READ_FIELD(RT500_CLKCTL0, SYSPLL0PFD, PFD0_CLKGATE) == 1);

    /*  ,,, and clocks are not ready */
    g_assert(REG32_READ_FIELD(RT500_CLKCTL0, SYSPLL0PFD, PFD3_CLKRDY) == 0);
    g_assert(REG32_READ_FIELD(RT500_CLKCTL0, SYSPLL0PFD, PFD2_CLKRDY) == 0);
    g_assert(REG32_READ_FIELD(RT500_CLKCTL0, SYSPLL0PFD, PFD1_CLKRDY) == 0);
    g_assert(REG32_READ_FIELD(RT500_CLKCTL0, SYSPLL0PFD, PFD0_CLKRDY) == 0);

    /* ungate all plls and check that clocks are ready */
    REG32_WRITE_FIELD(RT500_CLKCTL0, SYSPLL0PFD, PFD3_CLKGATE, 0);
    REG32_WRITE_FIELD(RT500_CLKCTL0, SYSPLL0PFD, PFD2_CLKGATE, 0);
    REG32_WRITE_FIELD(RT500_CLKCTL0, SYSPLL0PFD, PFD1_CLKGATE, 0);
    REG32_WRITE_FIELD(RT500_CLKCTL0, SYSPLL0PFD, PFD0_CLKGATE, 0);

    g_assert(REG32_READ_FIELD(RT500_CLKCTL0, SYSPLL0PFD, PFD3_CLKRDY) == 1);
    g_assert(REG32_READ_FIELD(RT500_CLKCTL0, SYSPLL0PFD, PFD2_CLKRDY) == 1);
    g_assert(REG32_READ_FIELD(RT500_CLKCTL0, SYSPLL0PFD, PFD1_CLKRDY) == 1);
    g_assert(REG32_READ_FIELD(RT500_CLKCTL0, SYSPLL0PFD, PFD0_CLKRDY) == 1);
}

static void systick_clk_test(gconstpointer user_data)
{
    /* systick is not running at reset */
    g_assert_cmpuint(dev_clock_out_get_hz(CLKCTL0_NAME, "systick_clk"), ==, 0);

    /* select divout no divisor */
    REG32_WRITE_FIELD(RT500_CLKCTL0, SYSTICKFCLKSEL, SEL,
                      SYSTICKFCLKSEL_DIVOUT);
    g_assert_cmpuint(dev_clock_out_get_hz(CLKCTL0_NAME, "systick_clk"),
                     ==, SYSCLK_HZ);

    /* change divisor to 2 */
    REG32_WRITE_FIELD(RT500_CLKCTL0, SYSTICKFCLKDIV, DIV, 1);
    g_assert_cmpuint(dev_clock_out_get_hz(CLKCTL0_NAME, "systick_clk"),
                     ==, SYSCLK_HZ / 2);

    /* select lpsoc */
    REG32_WRITE_FIELD(RT500_CLKCTL0, SYSTICKFCLKSEL, SEL,
                      SYSTICKFCLKSEL_LPOSC);
    g_assert_cmpuint(dev_clock_out_get_hz(CLKCTL0_NAME, "systick_clk"),
                     ==, LPOSC_CLK_HZ);

    /* select lpsoc */
    REG32_WRITE_FIELD(RT500_CLKCTL0, SYSTICKFCLKSEL, SEL,
                      SYSTICKFCLKSEL_32KHZRTC);
    g_assert_cmpuint(dev_clock_out_get_hz(CLKCTL0_NAME, "systick_clk"),
                     ==, RTC32KHZ_CLK_HZ);

    /* disable clock */
    REG32_WRITE_FIELD(RT500_CLKCTL0, SYSTICKFCLKSEL, SEL,
                      SYSTICKFCLKSEL_NONE);
    g_assert_cmpuint(dev_clock_out_get_hz(CLKCTL0_NAME, "systick_clk"),
                     ==, 0);
}

static void ostimer_clk_test(gconstpointer user_data)
{
    /* systick is not running at reset */
    g_assert_cmpuint(dev_clock_out_get_hz(CLKCTL1_NAME, "ostimer_clk"), ==, 0);

    /* select lpsoc */
    REG32_WRITE_FIELD(RT500_CLKCTL1, OSEVENTTFCLKSEL, SEL,
                      OSEVENTTFCLKSEL_LPOSC);
    g_assert_cmpuint(dev_clock_out_get_hz(CLKCTL1_NAME, "ostimer_clk"), ==,
                     LPOSC_CLK_HZ);

    /* select 32khz RTC */
    REG32_WRITE_FIELD(RT500_CLKCTL1, OSEVENTTFCLKSEL, SEL,
                      OSEVENTTFCLKSEL_32KHZRTC);
    g_assert_cmpuint(dev_clock_out_get_hz(CLKCTL1_NAME, "ostimer_clk"), ==,
                     RTC32KHZ_CLK_HZ);

    /* select hclk */
    REG32_WRITE_FIELD(RT500_CLKCTL1, OSEVENTTFCLKSEL, SEL,
                      OSEVENTTFCLKSEL_HCLK);
    g_assert_cmpuint(dev_clock_out_get_hz(CLKCTL1_NAME, "ostimer_clk"), ==,
                     SYSCLK_HZ);

    /* disable clock */
    REG32_WRITE_FIELD(RT500_CLKCTL1, OSEVENTTFCLKSEL, SEL,
                      OSEVENTTFCLKSEL_NONE);
    g_assert_cmpuint(dev_clock_out_get_hz(CLKCTL1_NAME, "ostimer_clk"), ==, 0);
}

int main(int argc, char **argv)
{
    int ret;

    g_test_init(&argc, &argv, NULL);

    qtest_add_data_func("/rt500-clkctl/pscctl-test", NULL, pscctl_test);
    qtest_add_data_func("/rt500-clkctl/syspll0pfd-test", NULL,
                        syspll0pfd_test);
    qtest_add_data_func("/rt500-clkctl/audiopll0pfd-test", NULL,
                        audiopll0pfd_test);
    g_test_add_data_func("/rt500-clkctl/systick-test", NULL,
                         systick_clk_test);
    g_test_add_data_func("/rt500-clkctl/ostimer-clk-test", NULL,
                         ostimer_clk_test);

    qtest_start("-M rt595-evk");
    ret = g_test_run();
    qtest_end();

    return ret;
}
