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
#include "migration/misc.h"
#include "multifd.h"

static QemuMutex queue_job_mutex;

static MultiFDSendData *device_state_send;

void multifd_device_state_send_setup(void)
{
    qemu_mutex_init(&queue_job_mutex);

    device_state_send = multifd_send_data_alloc();
}

void multifd_device_state_clear(MultiFDDeviceState_t *device_state)
{
    g_clear_pointer(&device_state->idstr, g_free);
    g_clear_pointer(&device_state->buf, g_free);
}

void multifd_device_state_send_cleanup(void)
{
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
