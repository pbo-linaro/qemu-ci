/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "migration/framework.h"
#include "migration/migration-qmp.h"
#include "migration/migration-util.h"
#include "qapi/qapi-types-migration.h"

char *disk_path;

static char *savevm_make_cmdline(void)
{
    MigrationTestEnv *env = migration_get_env();
    g_autofree char *drive_opts = NULL;
    g_autofree char *arch_opts = NULL;
    g_autofree char *machine_opts = NULL;
    g_autofree char *machine = NULL;

    disk_path = g_strdup_printf("%s/qtest-savevm-%d.qcow2", g_get_tmp_dir(),
                                getpid());
    drive_opts = g_strdup_printf("-drive if=none,file=%s,format=qcow2,node-name=disk0 ",
                                disk_path);

    g_assert(mkimg(disk_path, "qcow2", 100));

    machine = migrate_resolve_alias(env->arch);

    if (machine) {
        machine_opts = g_strdup_printf("-machine %s", machine);
    }

    return g_strdup_printf("%s %s %s",
                           drive_opts,
                           arch_opts ?: "",
                           machine_opts ?: "");
}

static void teardown_savevm_test(void)
{
    unlink(disk_path);
    g_free(disk_path);
}

/*
 * Enabling capabilities before savevm/loadvm should either apply the
 * appropriate feature or reject the command. Crashing or ignoring the
 * capability is not acceptable. Most (all?) migration capabilities
 * are incompatible with snapshots, but they've historically not been
 * rejected. Since there are compatibility concerns with simply
 * rejecting all caps, for now this test only validates that nothing
 * crashes.
 */
static void test_savevm_caps(void)
{
    MigrationTestEnv *env = migration_get_env();
    g_autofree char *cmdline = savevm_make_cmdline();
    QTestState *vm;

    /*
     * Only one VM to avoid having to shutdown the machine several
     * times to release the disks lock.
     */
    if (env->qemu_src || env->qemu_dst) {
        g_test_skip("Only one QEMU binary is supported");
        return;
    }

    vm = qtest_init(cmdline);

    for (int i = 0; i < MIGRATION_CAPABILITY__MAX; i++) {
        const char *cap = MigrationCapability_str(i);
        g_autofree char *error_str = NULL;
        bool ret;

        switch (i) {
        case MIGRATION_CAPABILITY_ZERO_BLOCKS:          /* deprecated */
        case MIGRATION_CAPABILITY_ZERO_COPY_SEND:       /* requires multifd */
        case MIGRATION_CAPABILITY_POSTCOPY_PREEMPT:     /* requires postcopy-ram */
        case MIGRATION_CAPABILITY_SWITCHOVER_ACK:       /* requires return-path */
        case MIGRATION_CAPABILITY_DIRTY_LIMIT:          /* requires dirty ring setup */
        case MIGRATION_CAPABILITY_BACKGROUND_SNAPSHOT:  /* requires uffd setup */
            continue;
        default:
            break;
        }

        if (getenv("QTEST_LOG")) {
            g_test_message("%s", MigrationCapability_str(i));
        }
        migrate_set_capability(vm, MigrationCapability_str(i), true);

        ret = snapshot_save_qmp_sync(vm, &error_str);

        if (ret) {
            g_assert(snapshot_load_qmp_sync(vm, NULL));
            g_assert(snapshot_delete_qmp_sync(vm, NULL));
        } else {
            g_autofree char *msg = g_strdup_printf(
                "Snapshots are not compatible with %s", cap);

            g_assert(error_str);
            g_assert(g_str_equal(msg, error_str));
        }

        migrate_set_capability(vm, MigrationCapability_str(i), false);
    }

    qtest_quit(vm);
    teardown_savevm_test();
}

static void test_savevm_loadvm(void)
{
    g_autofree char *cmdline = savevm_make_cmdline();
    QTestState *src, *dst;
    bool ret;

    src = qtest_init_with_env(QEMU_ENV_SRC, cmdline, true);

    ret = snapshot_save_qmp_sync(src, NULL);
    qtest_quit(src);

    if (ret) {
        dst = qtest_init_with_env(QEMU_ENV_DST, cmdline, true);

        g_assert(snapshot_load_qmp_sync(dst, NULL));
        g_assert(snapshot_delete_qmp_sync(dst, NULL));
        qtest_quit(dst);
    }

    teardown_savevm_test();
}

void migration_test_add_savevm(MigrationTestEnv *env)
{
    if (!getenv("QTEST_QEMU_IMG")) {
        g_test_message("savevm tests require QTEST_QEMU_IMG");
        return;
    }

    migration_test_add("/migration/savevm/save-load", test_savevm_loadvm);
    migration_test_add("/migration/savevm/capabilities", test_savevm_caps);
}
