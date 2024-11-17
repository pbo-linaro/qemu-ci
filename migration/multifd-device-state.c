/*
 * Multifd device state migration
 *
 * Copyright (C) 2024 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/lockable.h"
#include "block/thread-pool.h"
#include "migration/misc.h"
#include "multifd.h"
#include "options.h"

static QemuMutex queue_job_mutex;

static ThreadPool *send_threads;
static int send_threads_ret;
static bool send_threads_abort;

static MultiFDSendData *device_state_send;

void multifd_device_state_send_setup(void)
{
    qemu_mutex_init(&queue_job_mutex);

    device_state_send = multifd_send_data_alloc();

    send_threads = thread_pool_new();
    send_threads_ret = 0;
    send_threads_abort = false;
}

void multifd_device_state_clear(MultiFDDeviceState_t *device_state)
{
    g_clear_pointer(&device_state->idstr, g_free);
    g_clear_pointer(&device_state->buf, g_free);
}

void multifd_device_state_send_cleanup(void)
{
    g_clear_pointer(&send_threads, thread_pool_free);
    g_clear_pointer(&device_state_send, multifd_send_data_free);

    qemu_mutex_destroy(&queue_job_mutex);
}

static void multifd_device_state_fill_packet(MultiFDSendParams *p)
{
    MultiFDDeviceState_t *device_state = &p->data->u.device_state;
    MultiFDPacketDeviceState_t *packet = p->packet_device_state;

    packet->hdr.flags = cpu_to_be32(p->flags);
    strncpy(packet->idstr, device_state->idstr, sizeof(packet->idstr));
    packet->instance_id = cpu_to_be32(device_state->instance_id);
    packet->next_packet_size = cpu_to_be32(p->next_packet_size);
}

static void multifd_prepare_header_device_state(MultiFDSendParams *p)
{
    p->iov[0].iov_len = sizeof(*p->packet_device_state);
    p->iov[0].iov_base = p->packet_device_state;
    p->iovs_num++;
}

void multifd_device_state_send_prepare(MultiFDSendParams *p)
{
    MultiFDDeviceState_t *device_state = &p->data->u.device_state;

    assert(multifd_payload_device_state(p->data));

    multifd_prepare_header_device_state(p);

    assert(!(p->flags & MULTIFD_FLAG_SYNC));

    p->next_packet_size = device_state->buf_len;
    if (p->next_packet_size > 0) {
        p->iov[p->iovs_num].iov_base = device_state->buf;
        p->iov[p->iovs_num].iov_len = p->next_packet_size;
        p->iovs_num++;
    }

    p->flags |= MULTIFD_FLAG_NOCOMP | MULTIFD_FLAG_DEVICE_STATE;

    multifd_device_state_fill_packet(p);
}

bool multifd_queue_device_state(char *idstr, uint32_t instance_id,
                                char *data, size_t len)
{
    /* Device state submissions can come from multiple threads */
    QEMU_LOCK_GUARD(&queue_job_mutex);
    MultiFDDeviceState_t *device_state;

    assert(multifd_payload_empty(device_state_send));

    multifd_set_payload_type(device_state_send, MULTIFD_PAYLOAD_DEVICE_STATE);
    device_state = &device_state_send->u.device_state;
    device_state->idstr = g_strdup(idstr);
    device_state->instance_id = instance_id;
    device_state->buf = g_memdup2(data, len);
    device_state->buf_len = len;

    if (!multifd_send(&device_state_send)) {
        multifd_send_data_clear(device_state_send);
        return false;
    }

    return true;
}

bool migration_has_device_state_support(void)
{
    return migrate_multifd() && !migrate_mapped_ram() &&
        migrate_multifd_compression() == MULTIFD_COMPRESSION_NONE;
}

struct MultiFDDSSaveThreadData {
    SaveLiveCompletePrecopyThreadHandler hdlr;
    char *idstr;
    uint32_t instance_id;
    void *handler_opaque;
};

static void multifd_device_state_save_thread_data_free(void *opaque)
{
    struct MultiFDDSSaveThreadData *data = opaque;

    g_clear_pointer(&data->idstr, g_free);
    g_free(data);
}

static int multifd_device_state_save_thread(void *opaque)
{
    struct MultiFDDSSaveThreadData *data = opaque;
    int ret;

    ret = data->hdlr(data->idstr, data->instance_id, &send_threads_abort,
                     data->handler_opaque);
    if (ret && !qatomic_read(&send_threads_ret)) {
        /*
         * Racy with the above read but that's okay - which thread error
         * return we report is purely arbitrary anyway.
         */
        qatomic_set(&send_threads_ret, ret);
    }

    return 0;
}

void
multifd_spawn_device_state_save_thread(SaveLiveCompletePrecopyThreadHandler hdlr,
                                       char *idstr, uint32_t instance_id,
                                       void *opaque)
{
    struct MultiFDDSSaveThreadData *data;

    assert(migration_has_device_state_support());

    data = g_new(struct MultiFDDSSaveThreadData, 1);
    data->hdlr = hdlr;
    data->idstr = g_strdup(idstr);
    data->instance_id = instance_id;
    data->handler_opaque = opaque;

    thread_pool_submit(send_threads,
                       multifd_device_state_save_thread,
                       data, multifd_device_state_save_thread_data_free);

    /*
     * Make sure that this new thread is actually spawned immediately so it
     * can start its work right now.
     */
    thread_pool_adjust_max_threads_to_work(send_threads);
}

void multifd_abort_device_state_save_threads(void)
{
    assert(migration_has_device_state_support());

    qatomic_set(&send_threads_abort, true);
}

int multifd_join_device_state_save_threads(void)
{
    assert(migration_has_device_state_support());

    thread_pool_wait(send_threads);

    return send_threads_ret;
}
