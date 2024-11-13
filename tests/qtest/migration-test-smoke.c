/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "migration/test-framework.h"
#include "qemu/module.h"

int main(int argc, char **argv)
{
    MigrationTestEnv *env;
    int ret;

    g_test_init(&argc, &argv, NULL);
    env = migration_get_env();
    module_call_init(MODULE_INIT_QOM);

    if (env->has_kvm) {
        g_test_message(
            "Smoke tests already run as part of the full suite on KVM hosts");
        goto out;
    }

    migration_test_add_tls_smoke(env);
    migration_test_add_compression_smoke(env);
    migration_test_add_postcopy_smoke(env);
    migration_test_add_file_smoke(env);
    migration_test_add_precopy_smoke(env);
    migration_test_add_cpr_smoke(env);
    migration_test_add_misc_smoke(env);

out:
    ret = g_test_run();

    g_assert_cmpint(ret, ==, 0);

    ret = migration_env_clean(env);

    return ret;
}
