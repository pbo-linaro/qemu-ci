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
#include "threadinfo.h"

QemuMutex migration_threads_lock;
static QLIST_HEAD(, MigrationThread) migration_threads;

static void __attribute__((constructor)) migration_threads_init(void)
{
    qemu_mutex_init(&migration_threads_lock);
}

void migration_threads_add(const char *name)
{
    MigrationThread *thread =  g_new0(MigrationThread, 1);

    thread->name = name;
    thread->thread_id = qemu_get_thread_id();

    WITH_QEMU_LOCK_GUARD(&migration_threads_lock) {
        QLIST_INSERT_HEAD(&migration_threads, thread, node);
    }
}

void migration_threads_remove(void)
{
    int tid = qemu_get_thread_id();
    MigrationThread *thread;

    QEMU_LOCK_GUARD(&migration_threads_lock);

    QLIST_FOREACH(thread, &migration_threads, node) {
        if (tid != thread->thread_id) {
            continue;
        }

        QLIST_REMOVE(thread, node);
        g_free(thread);
        return;
    }

    g_assert_not_reached();
}

MigrationThreadInfoList *qmp_query_migrationthreads(Error **errp)
{
    MigrationThreadInfoList *head = NULL;
    MigrationThreadInfoList **tail = &head;
    MigrationThread *thread = NULL;

    QEMU_LOCK_GUARD(&migration_threads_lock);
    QLIST_FOREACH(thread, &migration_threads, node) {
        MigrationThreadInfo *info = g_new0(MigrationThreadInfo, 1);
        info->name = g_strdup(thread->name);
        info->thread_id = thread->thread_id;

        QAPI_LIST_APPEND(tail, info);
    }

    return head;
}
