/*
 * vmfwupdate device fwcfg test.
 *
 * Copyright (c) 2025 Red Hat, Inc.
 *
 * Author:
 *   Ani Sinha <anisinha@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "libqtest.h"
#include "standard-headers/linux/qemu_fw_cfg.h"
#include "libqos/fw_cfg.h"
#include "qemu/bswap.h"
#include "hw/misc/vmfwupdate.h"

static void test_vmfwupdate_capability(void)
{
    QFWCFG *fw_cfg;
    QTestState *s;
    uint64_t capability = 0;
    size_t filesize;

    s = qtest_init("-device vmfwupdate");
    fw_cfg = pc_fw_cfg_init(s);

    filesize = qfw_cfg_get_file(fw_cfg, FILE_VMFWUPDATE_CAP,
                                &capability, sizeof(capability));
    g_assert_cmpint(filesize, ==, sizeof(capability));
    capability = le64_to_cpu(capability);
    g_assert_cmpint(capability, ==, 0);
    pc_fw_cfg_uninit(fw_cfg);
    qtest_quit(s);
}

static void test_vmfwupdate_bios_size(void)
{
    QFWCFG *fw_cfg;
    QTestState *s;
    uint64_t bios_size = 0;
    size_t filesize;

    s = qtest_init("-device vmfwupdate");
    fw_cfg = pc_fw_cfg_init(s);

    filesize = qfw_cfg_get_file(fw_cfg, FILE_VMFWUPDATE_BIOS_SIZE,
                                &bios_size, sizeof(bios_size));
    g_assert_cmpint(filesize, ==, sizeof(bios_size));
    bios_size = le64_to_cpu(bios_size);
    fprintf(stderr, "bios_size: %" PRIu64 "\n", bios_size);
    g_assert_cmpint(bios_size, !=, 0);
    pc_fw_cfg_uninit(fw_cfg);
    qtest_quit(s);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("vmfwupdate/cap", test_vmfwupdate_capability);
    qtest_add_func("vmfwupdate/bios_size", test_vmfwupdate_bios_size);

    return g_test_run();
}
