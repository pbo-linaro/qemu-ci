/*
 * Multifd VFIO migration
 *
 * Copyright (C) 2024,2025 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "hw/vfio/vfio-common.h"
#include "migration/misc.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/lockable.h"
#include "qemu/main-loop.h"
#include "qemu/thread.h"
#include "migration/qemu-file.h"
#include "migration-multifd.h"
#include "trace.h"

#define VFIO_DEVICE_STATE_CONFIG_STATE (1)

#define VFIO_DEVICE_STATE_PACKET_VER_CURRENT (0)

typedef struct VFIODeviceStatePacket {
    uint32_t version;
    uint32_t idx;
    uint32_t flags;
    uint8_t data[0];
} QEMU_PACKED VFIODeviceStatePacket;

/* type safety */
typedef struct VFIOStateBuffers {
    GArray *array;
} VFIOStateBuffers;

typedef struct VFIOStateBuffer {
    bool is_present;
    char *data;
    size_t len;
} VFIOStateBuffer;

static void vfio_state_buffer_clear(gpointer data)
{
    VFIOStateBuffer *lb = data;

    if (!lb->is_present) {
        return;
    }

    g_clear_pointer(&lb->data, g_free);
    lb->is_present = false;
}

static void vfio_state_buffers_init(VFIOStateBuffers *bufs)
{
    bufs->array = g_array_new(FALSE, TRUE, sizeof(VFIOStateBuffer));
    g_array_set_clear_func(bufs->array, vfio_state_buffer_clear);
}

static void vfio_state_buffers_destroy(VFIOStateBuffers *bufs)
{
    g_clear_pointer(&bufs->array, g_array_unref);
}

static void vfio_state_buffers_assert_init(VFIOStateBuffers *bufs)
{
    assert(bufs->array);
}

static guint vfio_state_buffers_size_get(VFIOStateBuffers *bufs)
{
    return bufs->array->len;
}

static void vfio_state_buffers_size_set(VFIOStateBuffers *bufs, guint size)
{
    g_array_set_size(bufs->array, size);
}

static VFIOStateBuffer *vfio_state_buffers_at(VFIOStateBuffers *bufs, guint idx)
{
    return &g_array_index(bufs->array, VFIOStateBuffer, idx);
}

bool vfio_multifd_transfer_supported(void)
{
    return multifd_device_state_supported() &&
        migrate_send_switchover_start();
}
