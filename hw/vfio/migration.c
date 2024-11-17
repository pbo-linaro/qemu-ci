/*
 * Migration support for VFIO devices
 *
 * Copyright NVIDIA, Inc. 2020
 *
 * This work is licensed under the terms of the GNU GPL, version 2. See
 * the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/main-loop.h"
#include "qemu/cutils.h"
#include "qemu/units.h"
#include "qemu/error-report.h"
#include <linux/vfio.h>
#include <sys/ioctl.h>

#include "io/channel-buffer.h"
#include "sysemu/runstate.h"
#include "hw/vfio/vfio-common.h"
#include "migration/misc.h"
#include "migration/savevm.h"
#include "migration/vmstate.h"
#include "migration/qemu-file.h"
#include "migration/register.h"
#include "migration/blocker.h"
#include "qapi/error.h"
#include "qapi/qapi-events-vfio.h"
#include "exec/ramlist.h"
#include "exec/ram_addr.h"
#include "pci.h"
#include "trace.h"
#include "hw/hw.h"

/*
 * Flags to be used as unique delimiters for VFIO devices in the migration
 * stream. These flags are composed as:
 * 0xffffffff => MSB 32-bit all 1s
 * 0xef10     => Magic ID, represents emulated (virtual) function IO
 * 0x0000     => 16-bits reserved for flags
 *
 * The beginning of state information is marked by _DEV_CONFIG_STATE,
 * _DEV_SETUP_STATE, or _DEV_DATA_STATE, respectively. The end of a
 * certain state information is marked by _END_OF_STATE.
 */
#define VFIO_MIG_FLAG_END_OF_STATE      (0xffffffffef100001ULL)
#define VFIO_MIG_FLAG_DEV_CONFIG_STATE  (0xffffffffef100002ULL)
#define VFIO_MIG_FLAG_DEV_SETUP_STATE   (0xffffffffef100003ULL)
#define VFIO_MIG_FLAG_DEV_DATA_STATE    (0xffffffffef100004ULL)
#define VFIO_MIG_FLAG_DEV_INIT_DATA_SENT (0xffffffffef100005ULL)

/*
 * This is an arbitrary size based on migration of mlx5 devices, where typically
 * total device migration size is on the order of 100s of MB. Testing with
 * larger values, e.g. 128MB and 1GB, did not show a performance improvement.
 */
#define VFIO_MIG_DEFAULT_DATA_BUFFER_SIZE (1 * MiB)

#define VFIO_DEVICE_STATE_CONFIG_STATE (1)

typedef struct VFIODeviceStatePacket {
    uint32_t version;
    uint32_t idx;
    uint32_t flags;
    uint8_t data[0];
} QEMU_PACKED VFIODeviceStatePacket;

static int64_t bytes_transferred;

static const char *mig_state_to_str(enum vfio_device_mig_state state)
{
    switch (state) {
    case VFIO_DEVICE_STATE_ERROR:
        return "ERROR";
    case VFIO_DEVICE_STATE_STOP:
        return "STOP";
    case VFIO_DEVICE_STATE_RUNNING:
        return "RUNNING";
    case VFIO_DEVICE_STATE_STOP_COPY:
        return "STOP_COPY";
    case VFIO_DEVICE_STATE_RESUMING:
        return "RESUMING";
    case VFIO_DEVICE_STATE_RUNNING_P2P:
        return "RUNNING_P2P";
    case VFIO_DEVICE_STATE_PRE_COPY:
        return "PRE_COPY";
    case VFIO_DEVICE_STATE_PRE_COPY_P2P:
        return "PRE_COPY_P2P";
    default:
        return "UNKNOWN STATE";
    }
}

static QapiVfioMigrationState
mig_state_to_qapi_state(enum vfio_device_mig_state state)
{
    switch (state) {
    case VFIO_DEVICE_STATE_STOP:
        return QAPI_VFIO_MIGRATION_STATE_STOP;
    case VFIO_DEVICE_STATE_RUNNING:
        return QAPI_VFIO_MIGRATION_STATE_RUNNING;
    case VFIO_DEVICE_STATE_STOP_COPY:
        return QAPI_VFIO_MIGRATION_STATE_STOP_COPY;
    case VFIO_DEVICE_STATE_RESUMING:
        return QAPI_VFIO_MIGRATION_STATE_RESUMING;
    case VFIO_DEVICE_STATE_RUNNING_P2P:
        return QAPI_VFIO_MIGRATION_STATE_RUNNING_P2P;
    case VFIO_DEVICE_STATE_PRE_COPY:
        return QAPI_VFIO_MIGRATION_STATE_PRE_COPY;
    case VFIO_DEVICE_STATE_PRE_COPY_P2P:
        return QAPI_VFIO_MIGRATION_STATE_PRE_COPY_P2P;
    default:
        g_assert_not_reached();
    }
}

static void vfio_migration_send_event(VFIODevice *vbasedev)
{
    VFIOMigration *migration = vbasedev->migration;
    DeviceState *dev = vbasedev->dev;
    g_autofree char *qom_path = NULL;
    Object *obj;

    if (!vbasedev->migration_events) {
        return;
    }

    g_assert(vbasedev->ops->vfio_get_object);
    obj = vbasedev->ops->vfio_get_object(vbasedev);
    g_assert(obj);
    qom_path = object_get_canonical_path(obj);

    qapi_event_send_vfio_migration(
        dev->id, qom_path, mig_state_to_qapi_state(migration->device_state));
}

static void vfio_migration_set_device_state(VFIODevice *vbasedev,
                                            enum vfio_device_mig_state state)
{
    VFIOMigration *migration = vbasedev->migration;

    trace_vfio_migration_set_device_state(vbasedev->name,
                                          mig_state_to_str(state));

    migration->device_state = state;
    vfio_migration_send_event(vbasedev);
}

static int vfio_migration_set_state(VFIODevice *vbasedev,
                                    enum vfio_device_mig_state new_state,
                                    enum vfio_device_mig_state recover_state,
                                    Error **errp)
{
    VFIOMigration *migration = vbasedev->migration;
    uint64_t buf[DIV_ROUND_UP(sizeof(struct vfio_device_feature) +
                              sizeof(struct vfio_device_feature_mig_state),
                              sizeof(uint64_t))] = {};
    struct vfio_device_feature *feature = (struct vfio_device_feature *)buf;
    struct vfio_device_feature_mig_state *mig_state =
        (struct vfio_device_feature_mig_state *)feature->data;
    int ret;
    g_autofree char *error_prefix =
        g_strdup_printf("%s: Failed setting device state to %s.",
                        vbasedev->name, mig_state_to_str(new_state));

    trace_vfio_migration_set_state(vbasedev->name, mig_state_to_str(new_state),
                                   mig_state_to_str(recover_state));

    if (new_state == migration->device_state) {
        return 0;
    }

    feature->argsz = sizeof(buf);
    feature->flags =
        VFIO_DEVICE_FEATURE_SET | VFIO_DEVICE_FEATURE_MIG_DEVICE_STATE;
    mig_state->device_state = new_state;
    if (ioctl(vbasedev->fd, VFIO_DEVICE_FEATURE, feature)) {
        /* Try to set the device in some good state */
        ret = -errno;

        if (recover_state == VFIO_DEVICE_STATE_ERROR) {
            error_setg_errno(errp, errno,
                             "%s Recover state is ERROR. Resetting device",
                             error_prefix);

            goto reset_device;
        }

        error_setg_errno(errp, errno,
                         "%s Setting device in recover state %s",
                         error_prefix, mig_state_to_str(recover_state));

        mig_state->device_state = recover_state;
        if (ioctl(vbasedev->fd, VFIO_DEVICE_FEATURE, feature)) {
            ret = -errno;
            /*
             * If setting the device in recover state fails, report
             * the error here and propagate the first error.
             */
            error_report(
                "%s: Failed setting device in recover state, err: %s. Resetting device",
                         vbasedev->name, strerror(errno));

            goto reset_device;
        }

        vfio_migration_set_device_state(vbasedev, recover_state);

        return ret;
    }

    vfio_migration_set_device_state(vbasedev, new_state);
    if (mig_state->data_fd != -1) {
        if (migration->data_fd != -1) {
            /*
             * This can happen if the device is asynchronously reset and
             * terminates a data transfer.
             */
            error_setg(errp, "%s: data_fd out of sync", vbasedev->name);
            close(mig_state->data_fd);

            return -EBADF;
        }

        migration->data_fd = mig_state->data_fd;
    }

    return 0;

reset_device:
    if (ioctl(vbasedev->fd, VFIO_DEVICE_RESET)) {
        hw_error("%s: Failed resetting device, err: %s", vbasedev->name,
                 strerror(errno));
    }

    vfio_migration_set_device_state(vbasedev, VFIO_DEVICE_STATE_RUNNING);

    return ret;
}

/*
 * Some device state transitions require resetting the device if they fail.
 * This function sets the device in new_state and resets the device if that
 * fails. Reset is done by using ERROR as the recover state.
 */
static int
vfio_migration_set_state_or_reset(VFIODevice *vbasedev,
                                  enum vfio_device_mig_state new_state,
                                  Error **errp)
{
    return vfio_migration_set_state(vbasedev, new_state,
                                    VFIO_DEVICE_STATE_ERROR, errp);
}

static int vfio_load_buffer(QEMUFile *f, VFIODevice *vbasedev,
                            uint64_t data_size)
{
    VFIOMigration *migration = vbasedev->migration;
    int ret;

    ret = qemu_file_get_to_fd(f, migration->data_fd, data_size);
    trace_vfio_load_state_device_data(vbasedev->name, data_size, ret);

    return ret;
}

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

static int vfio_load_state_buffer(void *opaque, char *data, size_t data_size,
                                  Error **errp)
{
    VFIODevice *vbasedev = opaque;
    VFIOMigration *migration = vbasedev->migration;
    VFIODeviceStatePacket *packet = (VFIODeviceStatePacket *)data;
    VFIOStateBuffer *lb;

    /*
     * Holding BQL here would violate the lock order and can cause
     * a deadlock once we attempt to lock load_bufs_mutex below.
     */
    assert(!bql_locked());

    if (!migration->multifd_transfer) {
        error_setg(errp,
                   "got device state packet but not doing multifd transfer");
        return -1;
    }

    if (data_size < sizeof(*packet)) {
        error_setg(errp, "packet too short at %zu (min is %zu)",
                   data_size, sizeof(*packet));
        return -1;
    }

    if (packet->version != 0) {
        error_setg(errp, "packet has unknown version %" PRIu32,
                   packet->version);
        return -1;
    }

    if (packet->idx == UINT32_MAX) {
        error_setg(errp, "packet has too high idx %" PRIu32,
                   packet->idx);
        return -1;
    }

    trace_vfio_load_state_device_buffer_incoming(vbasedev->name, packet->idx);

    QEMU_LOCK_GUARD(&migration->load_bufs_mutex);

    /* config state packet should be the last one in the stream */
    if (packet->flags & VFIO_DEVICE_STATE_CONFIG_STATE) {
        migration->load_buf_idx_last = packet->idx;
    }

    vfio_state_buffers_assert_init(&migration->load_bufs);
    if (packet->idx >= vfio_state_buffers_size_get(&migration->load_bufs)) {
        vfio_state_buffers_size_set(&migration->load_bufs, packet->idx + 1);
    }

    lb = vfio_state_buffers_at(&migration->load_bufs, packet->idx);
    if (lb->is_present) {
        error_setg(errp, "state buffer %" PRIu32 " already filled",
                   packet->idx);
        return -1;
    }

    assert(packet->idx >= migration->load_buf_idx);

    migration->load_buf_queued_pending_buffers++;
    if (migration->load_buf_queued_pending_buffers >
        vbasedev->migration_max_queued_buffers) {
        error_setg(errp,
                   "queuing state buffer %" PRIu32 " would exceed the max of %" PRIu64,
                   packet->idx, vbasedev->migration_max_queued_buffers);
        return -1;
    }

    lb->data = g_memdup2(&packet->data, data_size - sizeof(*packet));
    lb->len = data_size - sizeof(*packet);
    lb->is_present = true;

    qemu_cond_signal(&migration->load_bufs_buffer_ready_cond);

    return 0;
}

static int vfio_load_device_config_state(QEMUFile *f, void *opaque);

static int vfio_load_bufs_thread_load_config(VFIODevice *vbasedev)
{
    VFIOMigration *migration = vbasedev->migration;
    VFIOStateBuffer *lb;
    g_autoptr(QIOChannelBuffer) bioc = NULL;
    QEMUFile *f_out = NULL, *f_in = NULL;
    uint64_t mig_header;
    int ret;

    assert(migration->load_buf_idx == migration->load_buf_idx_last);
    lb = vfio_state_buffers_at(&migration->load_bufs, migration->load_buf_idx);
    assert(lb->is_present);

    bioc = qio_channel_buffer_new(lb->len);
    qio_channel_set_name(QIO_CHANNEL(bioc), "vfio-device-config-load");

    f_out = qemu_file_new_output(QIO_CHANNEL(bioc));
    qemu_put_buffer(f_out, (uint8_t *)lb->data, lb->len);

    ret = qemu_fflush(f_out);
    if (ret) {
        g_clear_pointer(&f_out, qemu_fclose);
        return ret;
    }

    qio_channel_io_seek(QIO_CHANNEL(bioc), 0, 0, NULL);
    f_in = qemu_file_new_input(QIO_CHANNEL(bioc));

    mig_header = qemu_get_be64(f_in);
    if (mig_header != VFIO_MIG_FLAG_DEV_CONFIG_STATE) {
        g_clear_pointer(&f_out, qemu_fclose);
        g_clear_pointer(&f_in, qemu_fclose);
        return -EINVAL;
    }

    bql_lock();
    ret = vfio_load_device_config_state(f_in, vbasedev);
    bql_unlock();

    g_clear_pointer(&f_out, qemu_fclose);
    g_clear_pointer(&f_in, qemu_fclose);
    if (ret < 0) {
        return ret;
    }

    return 0;
}

static bool vfio_load_bufs_thread_want_abort(VFIODevice *vbasedev,
                                             bool *abort_flag)
{
    VFIOMigration *migration = vbasedev->migration;

    return migration->load_bufs_thread_want_exit || qatomic_read(abort_flag);
}

static int vfio_load_bufs_thread(bool *abort_flag, void *opaque)
{
    VFIODevice *vbasedev = opaque;
    VFIOMigration *migration = vbasedev->migration;
    QEMU_LOCK_GUARD(&migration->load_bufs_mutex);
    int ret;

    assert(migration->load_bufs_thread_running);

    while (!vfio_load_bufs_thread_want_abort(vbasedev, abort_flag)) {
        VFIOStateBuffer *lb;
        guint bufs_len;
        bool starved;

        assert(migration->load_buf_idx <= migration->load_buf_idx_last);

        bufs_len = vfio_state_buffers_size_get(&migration->load_bufs);
        if (migration->load_buf_idx >= bufs_len) {
            assert(migration->load_buf_idx == bufs_len);
            starved = true;
        } else {
            lb = vfio_state_buffers_at(&migration->load_bufs,
                                       migration->load_buf_idx);
            starved = !lb->is_present;
        }

        if (starved) {
            trace_vfio_load_state_device_buffer_starved(vbasedev->name,
                                                        migration->load_buf_idx);
            qemu_cond_wait(&migration->load_bufs_buffer_ready_cond,
                           &migration->load_bufs_mutex);
            continue;
        }

        if (migration->load_buf_idx == migration->load_buf_idx_last) {
            break;
        }

        if (migration->load_buf_idx == 0) {
            trace_vfio_load_state_device_buffer_start(vbasedev->name);
        }

        if (lb->len) {
            g_autofree char *buf = NULL;
            size_t buf_len;
            ssize_t wr_ret;
            int errno_save;

            trace_vfio_load_state_device_buffer_load_start(vbasedev->name,
                                                           migration->load_buf_idx);

            /* lb might become re-allocated when we drop the lock */
            buf = g_steal_pointer(&lb->data);
            buf_len = lb->len;

            /*
             * Loading data to the device takes a while,
             * drop the lock during this process.
             */
            qemu_mutex_unlock(&migration->load_bufs_mutex);
            wr_ret = write(migration->data_fd, buf, buf_len);
            errno_save = errno;
            qemu_mutex_lock(&migration->load_bufs_mutex);

            if (wr_ret < 0) {
                ret = -errno_save;
                goto ret_signal;
            } else if (wr_ret < buf_len) {
                ret = -EINVAL;
                goto ret_signal;
            }

            trace_vfio_load_state_device_buffer_load_end(vbasedev->name,
                                                         migration->load_buf_idx);
        }

        assert(migration->load_buf_queued_pending_buffers > 0);
        migration->load_buf_queued_pending_buffers--;

        if (migration->load_buf_idx == migration->load_buf_idx_last - 1) {
            trace_vfio_load_state_device_buffer_end(vbasedev->name);
        }

        migration->load_buf_idx++;
    }

    if (vfio_load_bufs_thread_want_abort(vbasedev, abort_flag)) {
        ret = -ECANCELED;
        goto ret_signal;
    }

    ret = vfio_load_bufs_thread_load_config(vbasedev);

ret_signal:
    migration->load_bufs_thread_running = false;
    qemu_cond_signal(&migration->load_bufs_thread_finished_cond);

    return ret;
}

static int vfio_save_device_config_state(QEMUFile *f, void *opaque,
                                         Error **errp)
{
    VFIODevice *vbasedev = opaque;
    int ret;

    qemu_put_be64(f, VFIO_MIG_FLAG_DEV_CONFIG_STATE);

    if (vbasedev->ops && vbasedev->ops->vfio_save_config) {
        ret = vbasedev->ops->vfio_save_config(vbasedev, f, errp);
        if (ret) {
            return ret;
        }
    }

    qemu_put_be64(f, VFIO_MIG_FLAG_END_OF_STATE);

    trace_vfio_save_device_config_state(vbasedev->name);

    ret = qemu_file_get_error(f);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Failed to save state");
    }
    return ret;
}

static int vfio_load_device_config_state(QEMUFile *f, void *opaque)
{
    VFIODevice *vbasedev = opaque;
    uint64_t data;

    trace_vfio_load_device_config_state_start(vbasedev->name);

    if (vbasedev->ops && vbasedev->ops->vfio_load_config) {
        int ret;

        ret = vbasedev->ops->vfio_load_config(vbasedev, f);
        if (ret) {
            error_report("%s: Failed to load device config space",
                         vbasedev->name);
            return ret;
        }
    }

    data = qemu_get_be64(f);
    if (data != VFIO_MIG_FLAG_END_OF_STATE) {
        error_report("%s: Failed loading device config space, "
                     "end flag incorrect 0x%"PRIx64, vbasedev->name, data);
        return -EINVAL;
    }

    trace_vfio_load_device_config_state_end(vbasedev->name);
    return qemu_file_get_error(f);
}

static void vfio_migration_cleanup(VFIODevice *vbasedev)
{
    VFIOMigration *migration = vbasedev->migration;

    close(migration->data_fd);
    migration->data_fd = -1;
}

static int vfio_query_stop_copy_size(VFIODevice *vbasedev,
                                     uint64_t *stop_copy_size)
{
    uint64_t buf[DIV_ROUND_UP(sizeof(struct vfio_device_feature) +
                              sizeof(struct vfio_device_feature_mig_data_size),
                              sizeof(uint64_t))] = {};
    struct vfio_device_feature *feature = (struct vfio_device_feature *)buf;
    struct vfio_device_feature_mig_data_size *mig_data_size =
        (struct vfio_device_feature_mig_data_size *)feature->data;

    feature->argsz = sizeof(buf);
    feature->flags =
        VFIO_DEVICE_FEATURE_GET | VFIO_DEVICE_FEATURE_MIG_DATA_SIZE;

    if (ioctl(vbasedev->fd, VFIO_DEVICE_FEATURE, feature)) {
        return -errno;
    }

    *stop_copy_size = mig_data_size->stop_copy_length;

    return 0;
}

static int vfio_query_precopy_size(VFIOMigration *migration)
{
    struct vfio_precopy_info precopy = {
        .argsz = sizeof(precopy),
    };

    migration->precopy_init_size = 0;
    migration->precopy_dirty_size = 0;

    if (ioctl(migration->data_fd, VFIO_MIG_GET_PRECOPY_INFO, &precopy)) {
        return -errno;
    }

    migration->precopy_init_size = precopy.initial_bytes;
    migration->precopy_dirty_size = precopy.dirty_bytes;

    return 0;
}

/* Returns the size of saved data on success and -errno on error */
static ssize_t vfio_save_block(QEMUFile *f, VFIOMigration *migration)
{
    ssize_t data_size;

    data_size = read(migration->data_fd, migration->data_buffer,
                     migration->data_buffer_size);
    if (data_size < 0) {
        /*
         * Pre-copy emptied all the device state for now. For more information,
         * please refer to the Linux kernel VFIO uAPI.
         */
        if (errno == ENOMSG) {
            if (!migration->event_precopy_empty_hit) {
                trace_vfio_save_block_precopy_empty_hit(migration->vbasedev->name);
                migration->event_precopy_empty_hit = true;
            }
            return 0;
        }

        return -errno;
    }
    if (data_size == 0) {
        return 0;
    }

    /* Non-empty read: re-arm the trace event */
    migration->event_precopy_empty_hit = false;

    qemu_put_be64(f, VFIO_MIG_FLAG_DEV_DATA_STATE);
    qemu_put_be64(f, data_size);
    qemu_put_buffer(f, migration->data_buffer, data_size);
    qatomic_add(&bytes_transferred, data_size);

    trace_vfio_save_block(migration->vbasedev->name, data_size);

    return qemu_file_get_error(f) ?: data_size;
}

static void vfio_update_estimated_pending_data(VFIOMigration *migration,
                                               uint64_t data_size)
{
    if (!data_size) {
        /*
         * Pre-copy emptied all the device state for now, update estimated sizes
         * accordingly.
         */
        migration->precopy_init_size = 0;
        migration->precopy_dirty_size = 0;

        return;
    }

    if (migration->precopy_init_size) {
        uint64_t init_size = MIN(migration->precopy_init_size, data_size);

        migration->precopy_init_size -= init_size;
        data_size -= init_size;
    }

    migration->precopy_dirty_size -= MIN(migration->precopy_dirty_size,
                                         data_size);
}

static bool vfio_precopy_supported(VFIODevice *vbasedev)
{
    VFIOMigration *migration = vbasedev->migration;

    return migration->mig_flags & VFIO_MIGRATION_PRE_COPY;
}

static bool vfio_multifd_transfer_supported(void)
{
    return migration_has_device_state_support() &&
        migrate_send_switchover_start();
}

/* ---------------------------------------------------------------------- */

static int vfio_save_prepare(void *opaque, Error **errp)
{
    VFIODevice *vbasedev = opaque;

    /*
     * Snapshot doesn't use postcopy nor background snapshot, so allow snapshot
     * even if they are on.
     */
    if (runstate_check(RUN_STATE_SAVE_VM)) {
        return 0;
    }

    if (migrate_postcopy_ram()) {
        error_setg(
            errp, "%s: VFIO migration is not supported with postcopy migration",
            vbasedev->name);
        return -EOPNOTSUPP;
    }

    if (migrate_background_snapshot()) {
        error_setg(
            errp,
            "%s: VFIO migration is not supported with background snapshot",
            vbasedev->name);
        return -EOPNOTSUPP;
    }

    return 0;
}

static int vfio_save_setup(QEMUFile *f, void *opaque, Error **errp)
{
    VFIODevice *vbasedev = opaque;
    VFIOMigration *migration = vbasedev->migration;
    uint64_t stop_copy_size = VFIO_MIG_DEFAULT_DATA_BUFFER_SIZE;
    int ret;

    /*
     * Make a copy of this setting at the start in case it is changed
     * mid-migration.
     */
    if (vbasedev->migration_multifd_transfer == ON_OFF_AUTO_AUTO) {
        migration->multifd_transfer = vfio_multifd_transfer_supported();
    } else {
        migration->multifd_transfer =
            vbasedev->migration_multifd_transfer == ON_OFF_AUTO_ON;
    }

    if (migration->multifd_transfer && !vfio_multifd_transfer_supported()) {
        error_setg(errp,
                   "%s: Multifd device transfer requested but unsupported in the current config",
                   vbasedev->name);
        return -EINVAL;
    }

    qemu_put_be64(f, VFIO_MIG_FLAG_DEV_SETUP_STATE);

    vfio_query_stop_copy_size(vbasedev, &stop_copy_size);
    migration->data_buffer_size = MIN(VFIO_MIG_DEFAULT_DATA_BUFFER_SIZE,
                                      stop_copy_size);
    migration->data_buffer = g_try_malloc0(migration->data_buffer_size);
    if (!migration->data_buffer) {
        error_setg(errp, "%s: Failed to allocate migration data buffer",
                   vbasedev->name);
        return -ENOMEM;
    }

    migration->event_save_iterate_started = false;
    migration->event_precopy_empty_hit = false;

    if (vfio_precopy_supported(vbasedev)) {
        switch (migration->device_state) {
        case VFIO_DEVICE_STATE_RUNNING:
            ret = vfio_migration_set_state(vbasedev, VFIO_DEVICE_STATE_PRE_COPY,
                                           VFIO_DEVICE_STATE_RUNNING, errp);
            if (ret) {
                return ret;
            }

            vfio_query_precopy_size(migration);

            break;
        case VFIO_DEVICE_STATE_STOP:
            /* vfio_save_complete_precopy() will go to STOP_COPY */
            break;
        default:
            error_setg(errp, "%s: Invalid device state %d", vbasedev->name,
                       migration->device_state);
            return -EINVAL;
        }
    }

    trace_vfio_save_setup(vbasedev->name, migration->data_buffer_size);

    qemu_put_be64(f, VFIO_MIG_FLAG_END_OF_STATE);

    ret = qemu_file_get_error(f);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "%s: save setup failed", vbasedev->name);
    }

    return ret;
}

static void vfio_save_cleanup(void *opaque)
{
    VFIODevice *vbasedev = opaque;
    VFIOMigration *migration = vbasedev->migration;
    Error *local_err = NULL;
    int ret;

    /*
     * Changing device state from STOP_COPY to STOP can take time. Do it here,
     * after migration has completed, so it won't increase downtime.
     */
    if (migration->device_state == VFIO_DEVICE_STATE_STOP_COPY) {
        ret = vfio_migration_set_state_or_reset(vbasedev,
                                                VFIO_DEVICE_STATE_STOP,
                                                &local_err);
        if (ret) {
            error_report_err(local_err);
        }
    }

    g_free(migration->data_buffer);
    migration->data_buffer = NULL;
    migration->precopy_init_size = 0;
    migration->precopy_dirty_size = 0;
    migration->initial_data_sent = false;
    vfio_migration_cleanup(vbasedev);
    trace_vfio_save_cleanup(vbasedev->name);
}

static void vfio_state_pending_estimate(void *opaque, uint64_t *must_precopy,
                                        uint64_t *can_postcopy)
{
    VFIODevice *vbasedev = opaque;
    VFIOMigration *migration = vbasedev->migration;

    if (!vfio_device_state_is_precopy(vbasedev)) {
        return;
    }

    *must_precopy +=
        migration->precopy_init_size + migration->precopy_dirty_size;

    trace_vfio_state_pending_estimate(vbasedev->name, *must_precopy,
                                      *can_postcopy,
                                      migration->precopy_init_size,
                                      migration->precopy_dirty_size);
}

/*
 * Migration size of VFIO devices can be as little as a few KBs or as big as
 * many GBs. This value should be big enough to cover the worst case.
 */
#define VFIO_MIG_STOP_COPY_SIZE (100 * GiB)

static void vfio_state_pending_exact(void *opaque, uint64_t *must_precopy,
                                     uint64_t *can_postcopy)
{
    VFIODevice *vbasedev = opaque;
    VFIOMigration *migration = vbasedev->migration;
    uint64_t stop_copy_size = VFIO_MIG_STOP_COPY_SIZE;

    /*
     * If getting pending migration size fails, VFIO_MIG_STOP_COPY_SIZE is
     * reported so downtime limit won't be violated.
     */
    vfio_query_stop_copy_size(vbasedev, &stop_copy_size);
    *must_precopy += stop_copy_size;

    if (vfio_device_state_is_precopy(vbasedev)) {
        vfio_query_precopy_size(migration);
    }

    trace_vfio_state_pending_exact(vbasedev->name, *must_precopy, *can_postcopy,
                                   stop_copy_size, migration->precopy_init_size,
                                   migration->precopy_dirty_size);
}

static bool vfio_is_active_iterate(void *opaque)
{
    VFIODevice *vbasedev = opaque;

    return vfio_device_state_is_precopy(vbasedev);
}

/*
 * Note about migration rate limiting: VFIO migration buffer size is currently
 * limited to 1MB, so there is no need to check if migration rate exceeded (as
 * in the worst case it will exceed by 1MB). However, if the buffer size is
 * later changed to a bigger value, migration rate should be enforced here.
 */
static int vfio_save_iterate(QEMUFile *f, void *opaque)
{
    VFIODevice *vbasedev = opaque;
    VFIOMigration *migration = vbasedev->migration;
    ssize_t data_size;

    if (!migration->event_save_iterate_started) {
        trace_vfio_save_iterate_start(vbasedev->name);
        migration->event_save_iterate_started = true;
    }

    data_size = vfio_save_block(f, migration);
    if (data_size < 0) {
        return data_size;
    }

    vfio_update_estimated_pending_data(migration, data_size);

    if (migrate_switchover_ack() && !migration->precopy_init_size &&
        !migration->initial_data_sent) {
        qemu_put_be64(f, VFIO_MIG_FLAG_DEV_INIT_DATA_SENT);
        migration->initial_data_sent = true;
    } else {
        qemu_put_be64(f, VFIO_MIG_FLAG_END_OF_STATE);
    }

    trace_vfio_save_iterate(vbasedev->name, migration->precopy_init_size,
                            migration->precopy_dirty_size);

    return !migration->precopy_init_size && !migration->precopy_dirty_size;
}

static void vfio_save_multifd_emit_dummy_eos(VFIODevice *vbasedev, QEMUFile *f)
{
    VFIOMigration *migration = vbasedev->migration;

    assert(migration->multifd_transfer);

    /*
     * Emit dummy NOP data on the main migration channel since the actual
     * device state transfer is done via multifd channels.
     */
    qemu_put_be64(f, VFIO_MIG_FLAG_END_OF_STATE);
}

static int vfio_save_complete_precopy(QEMUFile *f, void *opaque)
{
    VFIODevice *vbasedev = opaque;
    VFIOMigration *migration = vbasedev->migration;
    ssize_t data_size;
    int ret;
    Error *local_err = NULL;

    if (migration->multifd_transfer) {
        vfio_save_multifd_emit_dummy_eos(vbasedev, f);
        return 0;
    }

    trace_vfio_save_complete_precopy_start(vbasedev->name);

    /* We reach here with device state STOP or STOP_COPY only */
    ret = vfio_migration_set_state(vbasedev, VFIO_DEVICE_STATE_STOP_COPY,
                                   VFIO_DEVICE_STATE_STOP, &local_err);
    if (ret) {
        error_report_err(local_err);
        return ret;
    }

    do {
        data_size = vfio_save_block(f, vbasedev->migration);
        if (data_size < 0) {
            return data_size;
        }
    } while (data_size);

    qemu_put_be64(f, VFIO_MIG_FLAG_END_OF_STATE);
    ret = qemu_file_get_error(f);

    trace_vfio_save_complete_precopy(vbasedev->name, ret);

    return ret;
}

static int
vfio_save_complete_precopy_async_thread_config_state(VFIODevice *vbasedev,
                                                     char *idstr,
                                                     uint32_t instance_id,
                                                     uint32_t idx)
{
    g_autoptr(QIOChannelBuffer) bioc = NULL;
    g_autoptr(QEMUFile) f = NULL;
    int ret;
    g_autofree VFIODeviceStatePacket *packet = NULL;
    size_t packet_len;

    bioc = qio_channel_buffer_new(0);
    qio_channel_set_name(QIO_CHANNEL(bioc), "vfio-device-config-save");

    f = qemu_file_new_output(QIO_CHANNEL(bioc));

    ret = vfio_save_device_config_state(f, vbasedev, NULL);
    if (ret) {
        return ret;
    }

    ret = qemu_fflush(f);
    if (ret) {
        return ret;
    }

    packet_len = sizeof(*packet) + bioc->usage;
    packet = g_malloc0(packet_len);
    packet->idx = idx;
    packet->flags = VFIO_DEVICE_STATE_CONFIG_STATE;
    memcpy(&packet->data, bioc->data, bioc->usage);

    if (!multifd_queue_device_state(idstr, instance_id,
                                    (char *)packet, packet_len)) {
        return -1;
    }

    qatomic_add(&bytes_transferred, packet_len);

    return 0;
}

static int vfio_save_complete_precopy_thread(char *idstr,
                                             uint32_t instance_id,
                                             bool *abort_flag,
                                             void *opaque)
{
    VFIODevice *vbasedev = opaque;
    VFIOMigration *migration = vbasedev->migration;
    int ret;
    g_autofree VFIODeviceStatePacket *packet = NULL;
    uint32_t idx;

    if (!migration->multifd_transfer) {
        /* Nothing to do, vfio_save_complete_precopy() does the transfer. */
        return 0;
    }

    trace_vfio_save_complete_precopy_thread_start(vbasedev->name,
                                                  idstr, instance_id);

    /* We reach here with device state STOP or STOP_COPY only */
    ret = vfio_migration_set_state(vbasedev, VFIO_DEVICE_STATE_STOP_COPY,
                                   VFIO_DEVICE_STATE_STOP, NULL);
    if (ret) {
        goto ret_finish;
    }

    packet = g_malloc0(sizeof(*packet) + migration->data_buffer_size);

    for (idx = 0; ; idx++) {
        ssize_t data_size;
        size_t packet_size;

        if (qatomic_read(abort_flag)) {
            ret = -ECANCELED;
            goto ret_finish;
        }

        data_size = read(migration->data_fd, &packet->data,
                         migration->data_buffer_size);
        if (data_size < 0) {
            ret = -errno;
            goto ret_finish;
        } else if (data_size == 0) {
            break;
        }

        packet->idx = idx;
        packet_size = sizeof(*packet) + data_size;

        if (!multifd_queue_device_state(idstr, instance_id,
                                        (char *)packet, packet_size)) {
            ret = -1;
            goto ret_finish;
        }

        qatomic_add(&bytes_transferred, packet_size);
    }

    ret = vfio_save_complete_precopy_async_thread_config_state(vbasedev, idstr,
                                                               instance_id,
                                                               idx);

ret_finish:
    trace_vfio_save_complete_precopy_thread_end(vbasedev->name, ret);

    return ret;
}

static void vfio_save_state(QEMUFile *f, void *opaque)
{
    VFIODevice *vbasedev = opaque;
    VFIOMigration *migration = vbasedev->migration;
    Error *local_err = NULL;
    int ret;

    if (migration->multifd_transfer) {
        vfio_save_multifd_emit_dummy_eos(vbasedev, f);
        return;
    }

    ret = vfio_save_device_config_state(f, opaque, &local_err);
    if (ret) {
        error_prepend(&local_err,
                      "vfio: Failed to save device config space of %s - ",
                      vbasedev->name);
        qemu_file_set_error_obj(f, ret, local_err);
    }
}

static int vfio_load_setup(QEMUFile *f, void *opaque, Error **errp)
{
    VFIODevice *vbasedev = opaque;
    VFIOMigration *migration = vbasedev->migration;
    int ret;

    assert(!migration->load_setup);

    /*
     * Make a copy of this setting at the start in case it is changed
     * mid-migration.
     */
    if (vbasedev->migration_multifd_transfer == ON_OFF_AUTO_AUTO) {
        migration->multifd_transfer = vfio_multifd_transfer_supported();
    } else {
        migration->multifd_transfer =
            vbasedev->migration_multifd_transfer == ON_OFF_AUTO_ON;
    }

    if (migration->multifd_transfer && !vfio_multifd_transfer_supported()) {
        error_setg(errp,
                   "%s: Multifd device transfer requested but unsupported in the current config",
                   vbasedev->name);
        return -EINVAL;
    }

    ret = vfio_migration_set_state(vbasedev, VFIO_DEVICE_STATE_RESUMING,
                                   migration->device_state, errp);
    if (ret) {
        return ret;
    }

    if (migration->multifd_transfer) {
        assert(!migration->load_bufs.array);
        vfio_state_buffers_init(&migration->load_bufs);

        qemu_mutex_init(&migration->load_bufs_mutex);

        migration->load_buf_idx = 0;
        migration->load_buf_idx_last = UINT32_MAX;
        migration->load_buf_queued_pending_buffers = 0;
        qemu_cond_init(&migration->load_bufs_buffer_ready_cond);

        migration->load_bufs_thread_running = false;
        migration->load_bufs_thread_want_exit = false;
        qemu_cond_init(&migration->load_bufs_thread_finished_cond);
    }

    migration->load_setup = true;

    return 0;
}

static void vfio_load_cleanup_load_bufs_thread(VFIODevice *vbasedev)
{
    VFIOMigration *migration = vbasedev->migration;

    /* The lock order is load_bufs_mutex -> BQL so unlock BQL here first */
    bql_unlock();
    WITH_QEMU_LOCK_GUARD(&migration->load_bufs_mutex) {
        if (!migration->load_bufs_thread_running) {
            break;
        }

        migration->load_bufs_thread_want_exit = true;

        qemu_cond_signal(&migration->load_bufs_buffer_ready_cond);
        qemu_cond_wait(&migration->load_bufs_thread_finished_cond,
                       &migration->load_bufs_mutex);

        assert(!migration->load_bufs_thread_running);
    }
    bql_lock();
}

static int vfio_load_cleanup(void *opaque)
{
    VFIODevice *vbasedev = opaque;
    VFIOMigration *migration = vbasedev->migration;

    if (!migration->load_setup) {
        return 0;
    }

    if (migration->multifd_transfer) {
        vfio_load_cleanup_load_bufs_thread(vbasedev);
    }

    vfio_migration_cleanup(vbasedev);

    if (migration->multifd_transfer) {
        qemu_cond_destroy(&migration->load_bufs_thread_finished_cond);
        vfio_state_buffers_destroy(&migration->load_bufs);
        qemu_cond_destroy(&migration->load_bufs_buffer_ready_cond);
        qemu_mutex_destroy(&migration->load_bufs_mutex);
    }

    migration->load_setup = false;
    trace_vfio_load_cleanup(vbasedev->name);

    return 0;
}

static int vfio_load_state(QEMUFile *f, void *opaque, int version_id)
{
    VFIODevice *vbasedev = opaque;
    VFIOMigration *migration = vbasedev->migration;
    int ret = 0;
    uint64_t data;

    data = qemu_get_be64(f);
    while (data != VFIO_MIG_FLAG_END_OF_STATE) {

        trace_vfio_load_state(vbasedev->name, data);

        switch (data) {
        case VFIO_MIG_FLAG_DEV_CONFIG_STATE:
        {
            if (migration->multifd_transfer) {
                error_report("%s: got DEV_CONFIG_STATE but doing multifd transfer",
                             vbasedev->name);
                return -EINVAL;
            }

            return vfio_load_device_config_state(f, opaque);
        }
        case VFIO_MIG_FLAG_DEV_SETUP_STATE:
        {
            data = qemu_get_be64(f);
            if (data == VFIO_MIG_FLAG_END_OF_STATE) {
                return ret;
            } else {
                error_report("%s: SETUP STATE: EOS not found 0x%"PRIx64,
                             vbasedev->name, data);
                return -EINVAL;
            }
            break;
        }
        case VFIO_MIG_FLAG_DEV_DATA_STATE:
        {
            uint64_t data_size = qemu_get_be64(f);

            if (data_size) {
                ret = vfio_load_buffer(f, vbasedev, data_size);
                if (ret < 0) {
                    return ret;
                }
            }
            break;
        }
        case VFIO_MIG_FLAG_DEV_INIT_DATA_SENT:
        {
            if (!vfio_precopy_supported(vbasedev) ||
                !migrate_switchover_ack()) {
                error_report("%s: Received INIT_DATA_SENT but switchover ack "
                             "is not used", vbasedev->name);
                return -EINVAL;
            }

            ret = qemu_loadvm_approve_switchover();
            if (ret) {
                error_report(
                    "%s: qemu_loadvm_approve_switchover failed, err=%d (%s)",
                    vbasedev->name, ret, strerror(-ret));
            }

            return ret;
        }
        default:
            error_report("%s: Unknown tag 0x%"PRIx64, vbasedev->name, data);
            return -EINVAL;
        }

        data = qemu_get_be64(f);
        ret = qemu_file_get_error(f);
        if (ret) {
            return ret;
        }
    }
    return ret;
}

static bool vfio_switchover_ack_needed(void *opaque)
{
    VFIODevice *vbasedev = opaque;

    return vfio_precopy_supported(vbasedev);
}

static int vfio_switchover_start(void *opaque)
{
    VFIODevice *vbasedev = opaque;
    VFIOMigration *migration = vbasedev->migration;

    if (!migration->multifd_transfer) {
        /* Load thread is only used for multifd transfer */
        return 0;
    }

    /* The lock order is load_bufs_mutex -> BQL so unlock BQL here first */
    bql_unlock();
    WITH_QEMU_LOCK_GUARD(&migration->load_bufs_mutex) {
        assert(!migration->load_bufs_thread_running);
        migration->load_bufs_thread_running = true;
    }
    bql_lock();

    qemu_loadvm_start_load_thread(vfio_load_bufs_thread, vbasedev);

    return 0;
}

static const SaveVMHandlers savevm_vfio_handlers = {
    .save_prepare = vfio_save_prepare,
    .save_setup = vfio_save_setup,
    .save_cleanup = vfio_save_cleanup,
    .state_pending_estimate = vfio_state_pending_estimate,
    .state_pending_exact = vfio_state_pending_exact,
    .is_active_iterate = vfio_is_active_iterate,
    .save_live_iterate = vfio_save_iterate,
    .save_live_complete_precopy = vfio_save_complete_precopy,
    .save_live_complete_precopy_thread = vfio_save_complete_precopy_thread,
    .save_state = vfio_save_state,
    .load_setup = vfio_load_setup,
    .load_cleanup = vfio_load_cleanup,
    .load_state = vfio_load_state,
    .load_state_buffer = vfio_load_state_buffer,
    .switchover_ack_needed = vfio_switchover_ack_needed,
    .switchover_start = vfio_switchover_start,
};

/* ---------------------------------------------------------------------- */

static void vfio_vmstate_change_prepare(void *opaque, bool running,
                                        RunState state)
{
    VFIODevice *vbasedev = opaque;
    VFIOMigration *migration = vbasedev->migration;
    enum vfio_device_mig_state new_state;
    Error *local_err = NULL;
    int ret;

    new_state = migration->device_state == VFIO_DEVICE_STATE_PRE_COPY ?
                    VFIO_DEVICE_STATE_PRE_COPY_P2P :
                    VFIO_DEVICE_STATE_RUNNING_P2P;

    ret = vfio_migration_set_state_or_reset(vbasedev, new_state, &local_err);
    if (ret) {
        /*
         * Migration should be aborted in this case, but vm_state_notify()
         * currently does not support reporting failures.
         */
        migration_file_set_error(ret, local_err);
    }

    trace_vfio_vmstate_change_prepare(vbasedev->name, running,
                                      RunState_str(state),
                                      mig_state_to_str(new_state));
}

static void vfio_vmstate_change(void *opaque, bool running, RunState state)
{
    VFIODevice *vbasedev = opaque;
    enum vfio_device_mig_state new_state;
    Error *local_err = NULL;
    int ret;

    if (running) {
        new_state = VFIO_DEVICE_STATE_RUNNING;
    } else {
        new_state =
            (vfio_device_state_is_precopy(vbasedev) &&
             (state == RUN_STATE_FINISH_MIGRATE || state == RUN_STATE_PAUSED)) ?
                VFIO_DEVICE_STATE_STOP_COPY :
                VFIO_DEVICE_STATE_STOP;
    }

    ret = vfio_migration_set_state_or_reset(vbasedev, new_state, &local_err);
    if (ret) {
        /*
         * Migration should be aborted in this case, but vm_state_notify()
         * currently does not support reporting failures.
         */
        migration_file_set_error(ret, local_err);
    }

    trace_vfio_vmstate_change(vbasedev->name, running, RunState_str(state),
                              mig_state_to_str(new_state));
}

static int vfio_migration_state_notifier(NotifierWithReturn *notifier,
                                         MigrationEvent *e, Error **errp)
{
    VFIOMigration *migration = container_of(notifier, VFIOMigration,
                                            migration_state);
    VFIODevice *vbasedev = migration->vbasedev;
    Error *local_err = NULL;
    int ret;

    trace_vfio_migration_state_notifier(vbasedev->name, e->type);

    if (e->type == MIG_EVENT_PRECOPY_FAILED) {
        /*
         * MigrationNotifyFunc may not return an error code and an Error
         * object for MIG_EVENT_PRECOPY_FAILED. Hence, report the error
         * locally and ignore the errp argument.
         */
        ret = vfio_migration_set_state_or_reset(vbasedev,
                                                VFIO_DEVICE_STATE_RUNNING,
                                                &local_err);
        if (ret) {
            error_report_err(local_err);
        }
    }
    return 0;
}

static void vfio_migration_free(VFIODevice *vbasedev)
{
    g_free(vbasedev->migration);
    vbasedev->migration = NULL;
}

static int vfio_migration_query_flags(VFIODevice *vbasedev, uint64_t *mig_flags)
{
    uint64_t buf[DIV_ROUND_UP(sizeof(struct vfio_device_feature) +
                                  sizeof(struct vfio_device_feature_migration),
                              sizeof(uint64_t))] = {};
    struct vfio_device_feature *feature = (struct vfio_device_feature *)buf;
    struct vfio_device_feature_migration *mig =
        (struct vfio_device_feature_migration *)feature->data;

    feature->argsz = sizeof(buf);
    feature->flags = VFIO_DEVICE_FEATURE_GET | VFIO_DEVICE_FEATURE_MIGRATION;
    if (ioctl(vbasedev->fd, VFIO_DEVICE_FEATURE, feature)) {
        return -errno;
    }

    *mig_flags = mig->flags;

    return 0;
}

static bool vfio_dma_logging_supported(VFIODevice *vbasedev)
{
    uint64_t buf[DIV_ROUND_UP(sizeof(struct vfio_device_feature),
                              sizeof(uint64_t))] = {};
    struct vfio_device_feature *feature = (struct vfio_device_feature *)buf;

    feature->argsz = sizeof(buf);
    feature->flags = VFIO_DEVICE_FEATURE_PROBE |
                     VFIO_DEVICE_FEATURE_DMA_LOGGING_START;

    return !ioctl(vbasedev->fd, VFIO_DEVICE_FEATURE, feature);
}

static int vfio_migration_init(VFIODevice *vbasedev)
{
    int ret;
    Object *obj;
    VFIOMigration *migration;
    char id[256] = "";
    g_autofree char *path = NULL, *oid = NULL;
    uint64_t mig_flags = 0;
    VMChangeStateHandler *prepare_cb;

    if (!vbasedev->ops->vfio_get_object) {
        return -EINVAL;
    }

    obj = vbasedev->ops->vfio_get_object(vbasedev);
    if (!obj) {
        return -EINVAL;
    }

    ret = vfio_migration_query_flags(vbasedev, &mig_flags);
    if (ret) {
        return ret;
    }

    /* Basic migration functionality must be supported */
    if (!(mig_flags & VFIO_MIGRATION_STOP_COPY)) {
        return -EOPNOTSUPP;
    }

    vbasedev->migration = g_new0(VFIOMigration, 1);
    migration = vbasedev->migration;
    migration->vbasedev = vbasedev;
    migration->device_state = VFIO_DEVICE_STATE_RUNNING;
    migration->data_fd = -1;
    migration->mig_flags = mig_flags;

    vbasedev->dirty_pages_supported = vfio_dma_logging_supported(vbasedev);

    oid = vmstate_if_get_id(VMSTATE_IF(DEVICE(obj)));
    if (oid) {
        path = g_strdup_printf("%s/vfio", oid);
    } else {
        path = g_strdup("vfio");
    }
    strpadcpy(id, sizeof(id), path, '\0');

    register_savevm_live(id, VMSTATE_INSTANCE_ID_ANY, 1, &savevm_vfio_handlers,
                         vbasedev);

    prepare_cb = migration->mig_flags & VFIO_MIGRATION_P2P ?
                     vfio_vmstate_change_prepare :
                     NULL;
    migration->vm_state = qdev_add_vm_change_state_handler_full(
        vbasedev->dev, vfio_vmstate_change, prepare_cb, vbasedev);
    migration_add_notifier(&migration->migration_state,
                           vfio_migration_state_notifier);

    return 0;
}

static void vfio_migration_deinit(VFIODevice *vbasedev)
{
    VFIOMigration *migration = vbasedev->migration;

    migration_remove_notifier(&migration->migration_state);
    qemu_del_vm_change_state_handler(migration->vm_state);
    unregister_savevm(VMSTATE_IF(vbasedev->dev), "vfio", vbasedev);
    vfio_migration_free(vbasedev);
    vfio_unblock_multiple_devices_migration();
}

static int vfio_block_migration(VFIODevice *vbasedev, Error *err, Error **errp)
{
    if (vbasedev->enable_migration == ON_OFF_AUTO_ON) {
        error_propagate(errp, err);
        return -EINVAL;
    }

    vbasedev->migration_blocker = error_copy(err);
    error_free(err);

    return migrate_add_blocker_normal(&vbasedev->migration_blocker, errp);
}

/* ---------------------------------------------------------------------- */

int64_t vfio_mig_bytes_transferred(void)
{
    return qatomic_read(&bytes_transferred);
}

void vfio_reset_bytes_transferred(void)
{
    qatomic_set(&bytes_transferred, 0);
}

/*
 * Return true when either migration initialized or blocker registered.
 * Currently only return false when adding blocker fails which will
 * de-register vfio device.
 */
bool vfio_migration_realize(VFIODevice *vbasedev, Error **errp)
{
    Error *err = NULL;
    int ret;

    if (vbasedev->enable_migration == ON_OFF_AUTO_OFF) {
        error_setg(&err, "%s: Migration is disabled for VFIO device",
                   vbasedev->name);
        return !vfio_block_migration(vbasedev, err, errp);
    }

    ret = vfio_migration_init(vbasedev);
    if (ret) {
        if (ret == -ENOTTY) {
            error_setg(&err, "%s: VFIO migration is not supported in kernel",
                       vbasedev->name);
        } else {
            error_setg(&err,
                       "%s: Migration couldn't be initialized for VFIO device, "
                       "err: %d (%s)",
                       vbasedev->name, ret, strerror(-ret));
        }

        return !vfio_block_migration(vbasedev, err, errp);
    }

    if ((!vbasedev->dirty_pages_supported ||
         vbasedev->device_dirty_page_tracking == ON_OFF_AUTO_OFF) &&
        !vbasedev->iommu_dirty_tracking) {
        if (vbasedev->enable_migration == ON_OFF_AUTO_AUTO) {
            error_setg(&err,
                       "%s: VFIO device doesn't support device and "
                       "IOMMU dirty tracking", vbasedev->name);
            goto add_blocker;
        }

        warn_report("%s: VFIO device doesn't support device and "
                    "IOMMU dirty tracking", vbasedev->name);
    }

    ret = vfio_block_multiple_devices_migration(vbasedev, errp);
    if (ret) {
        goto out_deinit;
    }

    if (vfio_viommu_preset(vbasedev)) {
        error_setg(&err, "%s: Migration is currently not supported "
                   "with vIOMMU enabled", vbasedev->name);
        goto add_blocker;
    }

    trace_vfio_migration_realize(vbasedev->name);
    return true;

add_blocker:
    ret = vfio_block_migration(vbasedev, err, errp);
out_deinit:
    if (ret) {
        vfio_migration_deinit(vbasedev);
    }
    return !ret;
}

void vfio_migration_exit(VFIODevice *vbasedev)
{
    if (vbasedev->migration) {
        vfio_migration_deinit(vbasedev);
    }

    migrate_del_blocker(&vbasedev->migration_blocker);
}
