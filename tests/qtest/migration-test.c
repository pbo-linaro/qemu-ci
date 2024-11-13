/*
 * QTest testcase for migration
 *
 * Copyright (c) 2016-2018 Red Hat, Inc. and/or its affiliates
 *   based on the vhost-user-test.c that is:
 *      Copyright (c) 2014 Virtual Open Systems Sarl.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "migration/test-framework.h"
#include "migration/migration-qmp.h"
#include "migration/migration-util.h"
#include "qemu/module.h"

int main(int argc, char **argv)
{
    MigrationTestEnv *env;
    int ret;

    g_test_init(&argc, &argv, NULL);
    env = migration_get_env();
    module_call_init(MODULE_INIT_QOM);

    /*
     * Restrict the full set of tests to KVM hosts only. For tests
     * that run in all platforms, see migration-test-smoke.c. Ignore
     * the restriction if -m thorough was passed in the command line.
     */
    if (!g_test_thorough() && !env->has_kvm) {
        g_test_message("Full test suite only runs on KVM hosts "
                       "(override with -m thorough)");
        goto out;
    }

    migration_test_add_tls(env);
    migration_test_add_compression(env);
    migration_test_add_postcopy(env);
    migration_test_add_file(env);
    migration_test_add_precopy(env);
    migration_test_add_cpr(env);
    migration_test_add_misc(env);

out:
    ret = g_test_run();

    g_assert_cmpint(ret, ==, 0);

    ret = migration_env_clean(env);

    return ret;
}
