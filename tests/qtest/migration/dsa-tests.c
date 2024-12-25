/*
 * QTest testcases for DSA accelerator
 *
 * Copyright (C) Bytedance Ltd.
 *   based on the vhost-user-test.c that is:
 *      Copyright (c) 2014 Virtual Open Systems Sarl.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "migration/framework.h"
#include "migration/migration-qmp.h"
#include "migration/migration-util.h"

/*
 * It requires separate steps to configure and enable DSA device.
 * This test assumes that the configuration is done already.
 */
static const char *dsa_dev_path_p = "['dsa:/dev/dsa/wq4.0']";
static const char *dsa_dev_path = "/dev/dsa/wq4.0";
static int test_dsa_setup(void)
{
    int fd;
    fd = open(dsa_dev_path, O_RDWR);
    if (fd < 0) {
        return -1;
    }
    close(fd);
    return 0;
}

static void *test_migrate_precopy_tcp_multifd_start_dsa(QTestState *from,
                                                        QTestState *to)
{
    migrate_set_parameter_str(from, "zero-page-detection", "dsa-accel");
    migrate_set_parameter_str(from, "accel-path", dsa_dev_path_p);
    return migrate_hook_start_precopy_tcp_multifd_common(from, to, "none");
}

static void test_multifd_tcp_zero_page_dsa(void)
{
    MigrateCommon args = {
        .listen_uri = "defer",
        .start_hook = test_migrate_precopy_tcp_multifd_start_dsa,
    };

    test_precopy_common(&args);
}

void migration_test_add_dsa(MigrationTestEnv *env)
{
    if (test_dsa_setup() == 0) {
        migration_test_add("/migration/multifd/tcp/plain/zero-page/dsa",
                       test_multifd_tcp_zero_page_dsa);
    }
}
