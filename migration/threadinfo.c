/*
 *  Migration Threads info
 *
 *  Copyright (c) 2022 HUAWEI TECHNOLOGIES CO., LTD.
 *
 *  Authors:
 *  Jiang Jiacheng <jiangjiacheng@huawei.com>
 *
 *  This work is licensed under the terms of the GNU GPL, version 2 or later.
 *  See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/queue.h"
#include "qemu/lockable.h"
#include "migration.h"
#include "qapi/qapi-commands-migration.h"
#include "qapi/qapi-visit-migration.h"
#include "qapi/clone-visitor.h"

QemuMutex migration_threads_lock;
static MigrationThreadInfoList *migration_threads;

static void __attribute__((constructor)) migration_threads_init(void)
{
    qemu_mutex_init(&migration_threads_lock);
}

void migration_threads_add(const char *name)
{
    MigrationThreadInfo *thread = g_new0(MigrationThreadInfo, 1);

    thread->name = g_strdup(name);
    thread->thread_id = qemu_get_thread_id();

    WITH_QEMU_LOCK_GUARD(&migration_threads_lock) {
        QAPI_LIST_PREPEND(migration_threads, thread);
    }
}

void migration_threads_remove(void)
{
    int tid = qemu_get_thread_id();
    MigrationThreadInfoList *thread, *prev;

    QEMU_LOCK_GUARD(&migration_threads_lock);

    prev = NULL;
    thread = migration_threads;

    while (thread) {
        if (tid == thread->value->thread_id) {
            if (!prev) {
                migration_threads = thread->next;
            } else {
                prev->next = thread->next;
            }
            /* Terminate this single object to not free the rest */
            thread->next = NULL;
            qapi_free_MigrationThreadInfoList(thread);
            return;
        }
        prev = thread;
        thread = thread->next;
    }

    g_assert_not_reached();
}

MigrationThreadInfoList *qmp_query_migrationthreads(Error **errp)
{
    QEMU_LOCK_GUARD(&migration_threads_lock);

    return QAPI_CLONE(MigrationThreadInfoList, migration_threads);
}
