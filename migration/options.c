/*
 * QEMU migration capabilities
 *
 * Copyright (c) 2012-2023 Red Hat Inc
 *
 * Authors:
 *   Orit Wasserman <owasserm@redhat.com>
 *   Juan Quintela <quintela@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "exec/target_page.h"
#include "qapi/clone-visitor.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-migration.h"
#include "qapi/qapi-visit-migration.h"
#include "qapi/qmp/qerror.h"
#include "qobject/qnull.h"
#include "system/runstate.h"
#include "migration/colo.h"
#include "migration/cpr.h"
#include "migration/misc.h"
#include "migration.h"
#include "migration-stats.h"
#include "qemu-file.h"
#include "ram.h"
#include "options.h"
#include "system/kvm.h"

/* Maximum migrate downtime set to 2000 seconds */
#define MAX_MIGRATE_DOWNTIME_SECONDS 2000
#define MAX_MIGRATE_DOWNTIME (MAX_MIGRATE_DOWNTIME_SECONDS * 1000)

#define MAX_THROTTLE  (128 << 20)      /* Migration transfer speed throttling */

/* Time in milliseconds we are allowed to stop the source,
 * for sending the last part */
#define DEFAULT_MIGRATE_SET_DOWNTIME 300

/* Define default autoconverge cpu throttle migration options */
#define DEFAULT_MIGRATE_THROTTLE_TRIGGER_THRESHOLD 50
#define DEFAULT_MIGRATE_CPU_THROTTLE_INITIAL 20
#define DEFAULT_MIGRATE_CPU_THROTTLE_INCREMENT 10
#define DEFAULT_MIGRATE_MAX_CPU_THROTTLE 99

/* Migration XBZRLE default cache size */
#define DEFAULT_MIGRATE_XBZRLE_CACHE_SIZE (64 * 1024 * 1024)

/* The delay time (in ms) between two COLO checkpoints */
#define DEFAULT_MIGRATE_X_CHECKPOINT_DELAY (200 * 100)
#define DEFAULT_MIGRATE_MULTIFD_CHANNELS 2
#define DEFAULT_MIGRATE_MULTIFD_COMPRESSION MULTIFD_COMPRESSION_NONE
/* 0: means nocompress, 1: best speed, ... 9: best compress ratio */
#define DEFAULT_MIGRATE_MULTIFD_ZLIB_LEVEL 1
/*
 * 1: best speed, ... 9: best compress ratio
 * There is some nuance here. Refer to QATzip documentation to understand
 * the mapping of QATzip levels to standard deflate levels.
 */
#define DEFAULT_MIGRATE_MULTIFD_QATZIP_LEVEL 1

/* 0: means nocompress, 1: best speed, ... 20: best compress ratio */
#define DEFAULT_MIGRATE_MULTIFD_ZSTD_LEVEL 1

/* Background transfer rate for postcopy, 0 means unlimited, note
 * that page requests can still exceed this limit.
 */
#define DEFAULT_MIGRATE_MAX_POSTCOPY_BANDWIDTH 0

/*
 * Defaults for self_announce_delay giving a stream of RARP/ARP
 * packets after migration.
 */
#define DEFAULT_MIGRATE_ANNOUNCE_INITIAL  50
#define DEFAULT_MIGRATE_ANNOUNCE_MAX     550
#define DEFAULT_MIGRATE_ANNOUNCE_ROUNDS    5
#define DEFAULT_MIGRATE_ANNOUNCE_STEP    100

#define DEFAULT_MIGRATE_VCPU_DIRTY_LIMIT_PERIOD     1000    /* milliseconds */
#define DEFAULT_MIGRATE_VCPU_DIRTY_LIMIT            1       /* MB/s */

const Property migration_properties[] = {
    DEFINE_PROP_BOOL("store-global-state", MigrationState,
                     store_global_state, true),
    DEFINE_PROP_BOOL("send-configuration", MigrationState,
                     send_configuration, true),
    DEFINE_PROP_BOOL("send-section-footer", MigrationState,
                     send_section_footer, true),
    DEFINE_PROP_BOOL("send-switchover-start", MigrationState,
                     send_switchover_start, true),
    DEFINE_PROP_BOOL("multifd-flush-after-each-section", MigrationState,
                      multifd_flush_after_each_section, false),
    DEFINE_PROP_UINT8("x-clear-bitmap-shift", MigrationState,
                      clear_bitmap_shift, CLEAR_BITMAP_SHIFT_DEFAULT),
    DEFINE_PROP_BOOL("x-preempt-pre-7-2", MigrationState,
                     preempt_pre_7_2, false),
    DEFINE_PROP_BOOL("multifd-clean-tls-termination", MigrationState,
                     multifd_clean_tls_termination, true),

    DEFINE_PROP_UINT8("x-throttle-trigger-threshold", MigrationState,
                      config.throttle_trigger_threshold,
                      DEFAULT_MIGRATE_THROTTLE_TRIGGER_THRESHOLD),
    DEFINE_PROP_UINT8("x-cpu-throttle-initial", MigrationState,
                      config.cpu_throttle_initial,
                      DEFAULT_MIGRATE_CPU_THROTTLE_INITIAL),
    DEFINE_PROP_UINT8("x-cpu-throttle-increment", MigrationState,
                      config.cpu_throttle_increment,
                      DEFAULT_MIGRATE_CPU_THROTTLE_INCREMENT),
    DEFINE_PROP_BOOL("x-cpu-throttle-tailslow", MigrationState,
                      config.cpu_throttle_tailslow, false),
    DEFINE_PROP_SIZE("x-max-bandwidth", MigrationState,
                      config.max_bandwidth, MAX_THROTTLE),
    DEFINE_PROP_SIZE("avail-switchover-bandwidth", MigrationState,
                      config.avail_switchover_bandwidth, 0),
    DEFINE_PROP_UINT64("x-downtime-limit", MigrationState,
                      config.downtime_limit,
                      DEFAULT_MIGRATE_SET_DOWNTIME),
    DEFINE_PROP_UINT32("x-checkpoint-delay", MigrationState,
                      config.x_checkpoint_delay,
                      DEFAULT_MIGRATE_X_CHECKPOINT_DELAY),
    DEFINE_PROP_UINT8("multifd-channels", MigrationState,
                      config.multifd_channels,
                      DEFAULT_MIGRATE_MULTIFD_CHANNELS),
    DEFINE_PROP_MULTIFD_COMPRESSION("multifd-compression", MigrationState,
                      config.multifd_compression,
                      DEFAULT_MIGRATE_MULTIFD_COMPRESSION),
    DEFINE_PROP_UINT8("multifd-zlib-level", MigrationState,
                      config.multifd_zlib_level,
                      DEFAULT_MIGRATE_MULTIFD_ZLIB_LEVEL),
    DEFINE_PROP_UINT8("multifd-qatzip-level", MigrationState,
                      config.multifd_qatzip_level,
                      DEFAULT_MIGRATE_MULTIFD_QATZIP_LEVEL),
    DEFINE_PROP_UINT8("multifd-zstd-level", MigrationState,
                      config.multifd_zstd_level,
                      DEFAULT_MIGRATE_MULTIFD_ZSTD_LEVEL),
    DEFINE_PROP_SIZE("xbzrle-cache-size", MigrationState,
                      config.xbzrle_cache_size,
                      DEFAULT_MIGRATE_XBZRLE_CACHE_SIZE),
    DEFINE_PROP_SIZE("max-postcopy-bandwidth", MigrationState,
                      config.max_postcopy_bandwidth,
                      DEFAULT_MIGRATE_MAX_POSTCOPY_BANDWIDTH),
    DEFINE_PROP_UINT8("max-cpu-throttle", MigrationState,
                      config.max_cpu_throttle,
                      DEFAULT_MIGRATE_MAX_CPU_THROTTLE),
    DEFINE_PROP_SIZE("announce-initial", MigrationState,
                      config.announce_initial,
                      DEFAULT_MIGRATE_ANNOUNCE_INITIAL),
    DEFINE_PROP_SIZE("announce-max", MigrationState,
                      config.announce_max,
                      DEFAULT_MIGRATE_ANNOUNCE_MAX),
    DEFINE_PROP_SIZE("announce-rounds", MigrationState,
                      config.announce_rounds,
                      DEFAULT_MIGRATE_ANNOUNCE_ROUNDS),
    DEFINE_PROP_SIZE("announce-step", MigrationState,
                      config.announce_step,
                      DEFAULT_MIGRATE_ANNOUNCE_STEP),
    DEFINE_PROP_STRING("tls-creds", MigrationState, config.tls_creds),
    DEFINE_PROP_STRING("tls-hostname", MigrationState, config.tls_hostname),
    DEFINE_PROP_STRING("tls-authz", MigrationState, config.tls_authz),
    DEFINE_PROP_UINT64("x-vcpu-dirty-limit-period", MigrationState,
                       config.x_vcpu_dirty_limit_period,
                       DEFAULT_MIGRATE_VCPU_DIRTY_LIMIT_PERIOD),
    DEFINE_PROP_UINT64("vcpu-dirty-limit", MigrationState,
                       config.vcpu_dirty_limit,
                       DEFAULT_MIGRATE_VCPU_DIRTY_LIMIT),
    DEFINE_PROP_MIG_MODE("mode", MigrationState,
                      config.mode,
                      MIG_MODE_NORMAL),
    DEFINE_PROP_ZERO_PAGE_DETECTION("zero-page-detection", MigrationState,
                       config.zero_page_detection,
                       ZERO_PAGE_DETECTION_MULTIFD),
    DEFINE_PROP_BOOL("x-xbzrle",
                     MigrationState, config.xbzrle, false),
    DEFINE_PROP_BOOL("x-rdma-pin-all",
                     MigrationState, config.rdma_pin_all, false),
    DEFINE_PROP_BOOL("x-auto-converge",
                     MigrationState, config.auto_converge, false),
    DEFINE_PROP_BOOL("x-zero-blocks",
                     MigrationState, config.zero_blocks, false),
    DEFINE_PROP_BOOL("x-events",
                     MigrationState, config.events, false),
    DEFINE_PROP_BOOL("x-postcopy-ram",
                     MigrationState, config.postcopy_ram, false),
    DEFINE_PROP_BOOL("x-postcopy-preempt",
                     MigrationState, config.postcopy_preempt, false),
    DEFINE_PROP_BOOL("x-colo",
                     MigrationState, config.x_colo, false),
    DEFINE_PROP_BOOL("x-release-ram",
                     MigrationState, config.release_ram, false),
    DEFINE_PROP_BOOL("x-return-path",
                     MigrationState, config.return_path, false),
    DEFINE_PROP_BOOL("x-multifd",
                     MigrationState, config.multifd, false),
    DEFINE_PROP_BOOL("x-background-snapshot",
                     MigrationState, config.background_snapshot, false),
#ifdef CONFIG_LINUX
    DEFINE_PROP_BOOL("x-zero-copy-send",
                     MigrationState, config.zero_copy_send, false),
#endif
    DEFINE_PROP_BOOL("x-switchover-ack",
                     MigrationState, config.switchover_ack, false),
    DEFINE_PROP_BOOL("x-dirty-limit",
                     MigrationState, config.dirty_limit, false),
    DEFINE_PROP_BOOL("mapped-ram",
                     MigrationState, config.mapped_ram, false),
};
const size_t migration_properties_count = ARRAY_SIZE(migration_properties);

bool migrate_auto_converge(void)
{
    MigrationState *s = migrate_get_current();

    return s->config.auto_converge;
}

bool migrate_send_switchover_start(void)
{
    MigrationState *s = migrate_get_current();

    return s->send_switchover_start;
}

bool migrate_background_snapshot(void)
{
    MigrationState *s = migrate_get_current();

    return s->config.background_snapshot;
}

bool migrate_colo(void)
{
    MigrationState *s = migrate_get_current();

    return s->config.x_colo;
}

bool migrate_dirty_bitmaps(void)
{
    MigrationState *s = migrate_get_current();

    return s->config.dirty_bitmaps;
}

bool migrate_dirty_limit(void)
{
    MigrationState *s = migrate_get_current();

    return s->config.dirty_limit;
}

bool migrate_events(void)
{
    MigrationState *s = migrate_get_current();

    return s->config.events;
}

bool migrate_mapped_ram(void)
{
    MigrationState *s = migrate_get_current();

    return s->config.mapped_ram;
}

bool migrate_ignore_shared(void)
{
    MigrationState *s = migrate_get_current();

    return s->config.x_ignore_shared;
}

bool migrate_late_block_activate(void)
{
    MigrationState *s = migrate_get_current();

    return s->config.late_block_activate;
}

bool migrate_multifd(void)
{
    MigrationState *s = migrate_get_current();

    return s->config.multifd;
}

bool migrate_pause_before_switchover(void)
{
    MigrationState *s = migrate_get_current();

    return s->config.pause_before_switchover;
}

bool migrate_postcopy_blocktime(void)
{
    MigrationState *s = migrate_get_current();

    return s->config.postcopy_blocktime;
}

bool migrate_postcopy_preempt(void)
{
    MigrationState *s = migrate_get_current();

    return s->config.postcopy_preempt;
}

bool migrate_postcopy_ram(void)
{
    MigrationState *s = migrate_get_current();

    return s->config.postcopy_ram;
}

bool migrate_rdma_pin_all(void)
{
    MigrationState *s = migrate_get_current();

    return s->config.rdma_pin_all;
}

bool migrate_release_ram(void)
{
    MigrationState *s = migrate_get_current();

    return s->config.release_ram;
}

bool migrate_return_path(void)
{
    MigrationState *s = migrate_get_current();

    return s->config.return_path;
}

bool migrate_switchover_ack(void)
{
    MigrationState *s = migrate_get_current();

    return s->config.switchover_ack;
}

bool migrate_validate_uuid(void)
{
    MigrationState *s = migrate_get_current();

    return s->config.validate_uuid;
}

bool migrate_xbzrle(void)
{
    MigrationState *s = migrate_get_current();

    return s->config.xbzrle;
}

bool migrate_zero_copy_send(void)
{
    MigrationState *s = migrate_get_current();

    return s->config.zero_copy_send;
}

bool migrate_multifd_flush_after_each_section(void)
{
    MigrationState *s = migrate_get_current();

    return s->multifd_flush_after_each_section;
}

bool migrate_postcopy(void)
{
    return migrate_postcopy_ram() || migrate_dirty_bitmaps();
}

bool migrate_rdma(void)
{
    MigrationState *s = migrate_get_current();

    return s->rdma_migration;
}

typedef enum WriteTrackingSupport {
    WT_SUPPORT_UNKNOWN = 0,
    WT_SUPPORT_ABSENT,
    WT_SUPPORT_AVAILABLE,
    WT_SUPPORT_COMPATIBLE
} WriteTrackingSupport;

static
WriteTrackingSupport migrate_query_write_tracking(void)
{
    /* Check if kernel supports required UFFD features */
    if (!ram_write_tracking_available()) {
        return WT_SUPPORT_ABSENT;
    }
    /*
     * Check if current memory configuration is
     * compatible with required UFFD features.
     */
    if (!ram_write_tracking_compatible()) {
        return WT_SUPPORT_AVAILABLE;
    }

    return WT_SUPPORT_COMPATIBLE;
}

static bool migrate_incoming_started(void)
{
    return !!migration_incoming_get_current()->transport_data;
}

bool migrate_caps_check(MigrationConfig *new, Error **errp)
{
    MigrationState *s = migrate_get_current();
    MigrationIncomingState *mis = migration_incoming_get_current();
    bool postcopy_already_on = s->config.postcopy_ram;
    ERRP_GUARD();


    if (new->zero_blocks) {
        warn_report("zero-blocks capability is deprecated");
    }

#ifndef CONFIG_REPLICATION
    if (new->x_colo) {
        error_setg(errp, "QEMU compiled without replication module"
                   " can't enable COLO");
        error_append_hint(errp, "Please enable replication before COLO.\n");
        return false;
    }
#endif

    if (new->postcopy_ram) {
        /* This check is reasonably expensive, so only when it's being
         * set the first time, also it's only the destination that needs
         * special support.
         */
        if (!postcopy_already_on &&
            runstate_check(RUN_STATE_INMIGRATE) &&
            !postcopy_ram_supported_by_host(mis, errp)) {
            error_prepend(errp, "Postcopy is not supported: ");
            return false;
        }

        if (new->x_ignore_shared) {
            error_setg(errp, "Postcopy is not compatible with ignore-shared");
            return false;
        }

        if (new->multifd) {
            error_setg(errp, "Postcopy is not yet compatible with multifd");
            return false;
        }
    }

    if (new->background_snapshot) {
        WriteTrackingSupport wt_support;

        /*
         * Check if 'background-snapshot' capability is supported by
         * host kernel and compatible with guest memory configuration.
         */
        wt_support = migrate_query_write_tracking();
        if (wt_support < WT_SUPPORT_AVAILABLE) {
            error_setg(errp, "Background-snapshot is not supported by host kernel");
            return false;
        }
        if (wt_support < WT_SUPPORT_COMPATIBLE) {
            error_setg(errp, "Background-snapshot is not compatible "
                    "with guest memory configuration");
            return false;
        }

        if (new->postcopy_ram ||
            new->dirty_bitmaps ||
            new->postcopy_blocktime ||
            new->late_block_activate ||
            new->return_path ||
            new->multifd ||
            new->pause_before_switchover ||
            new->auto_converge ||
            new->release_ram ||
            new->rdma_pin_all ||
            new->xbzrle ||
            new->x_colo ||
            new->validate_uuid ||
            new->zero_copy_send) {
            error_setg(errp,
                       "Background-snapshot is not compatible with "
                       "currently set capabilities");
            return false;
        }
    }

#ifdef CONFIG_LINUX
    if (new->zero_copy_send &&
        (!new->multifd || new->xbzrle ||
         migrate_multifd_compression() || migrate_tls())) {
        error_setg(errp,
                   "Zero copy only available for non-compressed non-TLS multifd migration");
        return false;
    }
#else
    if (new->zero_copy_send) {
        error_setg(errp,
                   "Zero copy currently only available on Linux");
        return false;
    }
#endif

    if (new->postcopy_preempt) {
        if (!new->postcopy_ram) {
            error_setg(errp, "Postcopy preempt requires postcopy-ram");
            return false;
        }

        if (migrate_incoming_started()) {
            error_setg(errp,
                       "Postcopy preempt must be set before incoming starts");
            return false;
        }
    }

    if (new->multifd) {
        if (migrate_incoming_started()) {
            error_setg(errp, "Multifd must be set before incoming starts");
            return false;
        }
    }

    if (new->switchover_ack) {
        if (!new->return_path) {
            error_setg(errp, "Capability 'switchover-ack' requires capability "
                             "'return-path'");
            return false;
        }
    }
    if (new->dirty_limit) {
        if (new->auto_converge) {
            error_setg(errp, "dirty-limit conflicts with auto-converge"
                       " either of then available currently");
            return false;
        }

        if (!kvm_enabled() || !kvm_dirty_ring_enabled()) {
            error_setg(errp, "dirty-limit requires KVM with accelerator"
                   " property 'dirty-ring-size' set");
            return false;
        }
    }

    if (new->multifd) {
        if (new->xbzrle) {
            error_setg(errp, "Multifd is not compatible with xbzrle");
            return false;
        }
    }

    if (new->mapped_ram) {
        if (new->xbzrle) {
            error_setg(errp,
                       "Mapped-ram migration is incompatible with xbzrle");
            return false;
        }

        if (new->postcopy_ram) {
            error_setg(errp,
                       "Mapped-ram migration is incompatible with postcopy");
            return false;
        }
    }

    return true;
}

MigrationCapabilityStatusList *qmp_query_migrate_capabilities(Error **errp)
{
    MigrationCapabilityStatusList *head = NULL, **tail = &head;
    MigrationCapabilityStatus *caps;
    MigrationState *s = migrate_get_current();
    int i;

    for (i = 0; i < MIGRATION_CAPABILITY__MAX; i++) {
        caps = g_malloc0(sizeof(*caps));
        caps->capability = i;
        caps->state = migrate_config_get_cap_compat(&s->config, i);
        QAPI_LIST_APPEND(tail, caps);
    }

    return head;
}

static bool *migrate_config_get_cap_addr(MigrationConfig *config, int i)
{
    bool *cap_addr = NULL;

    switch (i) {
    case MIGRATION_CAPABILITY_XBZRLE:
        cap_addr = &config->xbzrle;
        break;
    case MIGRATION_CAPABILITY_RDMA_PIN_ALL:
        cap_addr = &config->rdma_pin_all;
        break;
    case MIGRATION_CAPABILITY_AUTO_CONVERGE:
        cap_addr = &config->auto_converge;
        break;
    case MIGRATION_CAPABILITY_ZERO_BLOCKS:
        cap_addr = &config->zero_blocks;
        break;
    case MIGRATION_CAPABILITY_EVENTS:
        cap_addr = &config->events;
        break;
    case MIGRATION_CAPABILITY_POSTCOPY_RAM:
        cap_addr = &config->postcopy_ram;
        break;
    case MIGRATION_CAPABILITY_X_COLO:
        cap_addr = &config->x_colo;
        break;
    case MIGRATION_CAPABILITY_RELEASE_RAM:
        cap_addr = &config->release_ram;
        break;
    case MIGRATION_CAPABILITY_RETURN_PATH:
        cap_addr = &config->return_path;
        break;
    case MIGRATION_CAPABILITY_PAUSE_BEFORE_SWITCHOVER:
        cap_addr = &config->pause_before_switchover;
        break;
    case MIGRATION_CAPABILITY_MULTIFD:
        cap_addr = &config->multifd;
        break;
    case MIGRATION_CAPABILITY_DIRTY_BITMAPS:
        cap_addr = &config->dirty_bitmaps;
        break;
    case MIGRATION_CAPABILITY_POSTCOPY_BLOCKTIME:
        cap_addr = &config->postcopy_blocktime;
        break;
    case MIGRATION_CAPABILITY_LATE_BLOCK_ACTIVATE:
        cap_addr = &config->late_block_activate;
        break;
    case MIGRATION_CAPABILITY_X_IGNORE_SHARED:
        cap_addr = &config->x_ignore_shared;
        break;
    case MIGRATION_CAPABILITY_VALIDATE_UUID:
        cap_addr = &config->validate_uuid;
        break;
    case MIGRATION_CAPABILITY_BACKGROUND_SNAPSHOT:
        cap_addr = &config->background_snapshot;
        break;
    case MIGRATION_CAPABILITY_ZERO_COPY_SEND:
        cap_addr = &config->zero_copy_send;
        break;
    case MIGRATION_CAPABILITY_POSTCOPY_PREEMPT:
        cap_addr = &config->postcopy_preempt;
        break;
    case MIGRATION_CAPABILITY_SWITCHOVER_ACK:
        cap_addr = &config->switchover_ack;
        break;
    case MIGRATION_CAPABILITY_DIRTY_LIMIT:
        cap_addr = &config->dirty_limit;
        break;
    case MIGRATION_CAPABILITY_MAPPED_RAM:
        cap_addr = &config->mapped_ram;
        break;
    default:
        g_assert_not_reached();
    }

    return cap_addr;
}

/* Compatibility for code that reads capabilities in a loop */
bool migrate_config_get_cap_compat(MigrationConfig *config, int i)
{
    return *(migrate_config_get_cap_addr(config, i));
}

/* Compatibility for code that writes capabilities in a loop */
static void migrate_config_set_cap_compat(MigrationConfig *config, int i,
                                          bool val)
{
    *(migrate_config_get_cap_addr(config, i)) = val;
}

/*
 * Set capabilities for compatibility with the old
 * migrate-set-capabilities command.
 */
static void migrate_config_set_caps_compat(MigrationConfig *config,
                                           MigrationCapabilityStatusList *caps)
{
    MigrationCapabilityStatusList *cap;

    for (cap = caps; cap; cap = cap->next) {
        migrate_config_set_cap_compat(config, cap->value->capability,
                                      cap->value->state);
    }
}

void qmp_migrate_set_capabilities(MigrationCapabilityStatusList *params,
                                  Error **errp)
{
    MigrationState *s = migrate_get_current();
    g_autoptr(MigrationConfig) new = NULL;

    if (migration_is_running() || migration_in_colo_state()) {
        error_setg(errp, "There's a migration process in progress");
        return;
    }

    /*
     * Capabilities validation needs to first copy from s->config in
     * case the incoming config has a capability that conflicts with
     * another that's already set.
     */
    new = QAPI_CLONE(MigrationConfig, &s->config);
    migrate_config_set_caps_compat(new, params);

    if (!migrate_caps_check(new, errp)) {
        return;
    }

    migrate_config_set_caps_compat(&s->config, params);
}

const BitmapMigrationNodeAliasList *migrate_block_bitmap_mapping(void)
{
    MigrationState *s = migrate_get_current();

    return s->config.block_bitmap_mapping;
}

bool migrate_has_block_bitmap_mapping(void)
{
    MigrationState *s = migrate_get_current();

    return s->config.has_block_bitmap_mapping;
}

uint32_t migrate_checkpoint_delay(void)
{
    MigrationState *s = migrate_get_current();

    return s->config.x_checkpoint_delay;
}

uint8_t migrate_cpu_throttle_increment(void)
{
    MigrationState *s = migrate_get_current();

    return s->config.cpu_throttle_increment;
}

uint8_t migrate_cpu_throttle_initial(void)
{
    MigrationState *s = migrate_get_current();

    return s->config.cpu_throttle_initial;
}

bool migrate_cpu_throttle_tailslow(void)
{
    MigrationState *s = migrate_get_current();

    return s->config.cpu_throttle_tailslow;
}

bool migrate_direct_io(void)
{
    MigrationState *s = migrate_get_current();

    /*
     * O_DIRECT is only supported with mapped-ram and multifd.
     *
     * mapped-ram is needed because filesystems impose restrictions on
     * O_DIRECT IO alignment (see MAPPED_RAM_FILE_OFFSET_ALIGNMENT).
     *
     * multifd is needed to keep the unaligned portion of the stream
     * isolated to the main migration thread while multifd channels
     * process the aligned data with O_DIRECT enabled.
     */
    return s->config.direct_io && s->config.mapped_ram && s->config.multifd;
}

uint64_t migrate_downtime_limit(void)
{
    MigrationState *s = migrate_get_current();

    return s->config.downtime_limit;
}

uint8_t migrate_max_cpu_throttle(void)
{
    MigrationState *s = migrate_get_current();

    return s->config.max_cpu_throttle;
}

uint64_t migrate_max_bandwidth(void)
{
    MigrationState *s = migrate_get_current();

    return s->config.max_bandwidth;
}

uint64_t migrate_avail_switchover_bandwidth(void)
{
    MigrationState *s = migrate_get_current();

    return s->config.avail_switchover_bandwidth;
}

uint64_t migrate_max_postcopy_bandwidth(void)
{
    MigrationState *s = migrate_get_current();

    return s->config.max_postcopy_bandwidth;
}

MigMode migrate_mode(void)
{
    MigMode mode = cpr_get_incoming_mode();

    if (mode == MIG_MODE_NONE) {
        mode = migrate_get_current()->config.mode;
    }

    assert(mode >= 0 && mode < MIG_MODE__MAX);
    return mode;
}

int migrate_multifd_channels(void)
{
    MigrationState *s = migrate_get_current();

    return s->config.multifd_channels;
}

MultiFDCompression migrate_multifd_compression(void)
{
    MigrationState *s = migrate_get_current();

    assert(s->config.multifd_compression < MULTIFD_COMPRESSION__MAX);
    return s->config.multifd_compression;
}

int migrate_multifd_zlib_level(void)
{
    MigrationState *s = migrate_get_current();

    return s->config.multifd_zlib_level;
}

int migrate_multifd_qatzip_level(void)
{
    MigrationState *s = migrate_get_current();

    return s->config.multifd_qatzip_level;
}

int migrate_multifd_zstd_level(void)
{
    MigrationState *s = migrate_get_current();

    return s->config.multifd_zstd_level;
}

uint8_t migrate_throttle_trigger_threshold(void)
{
    MigrationState *s = migrate_get_current();

    return s->config.throttle_trigger_threshold;
}

const char *migrate_tls_authz(void)
{
    MigrationState *s = migrate_get_current();

    if (s->config.tls_authz &&
        *s->config.tls_authz) {
        return s->config.tls_authz;
    }

    return NULL;
}

const char *migrate_tls_creds(void)
{
    MigrationState *s = migrate_get_current();

    if (s->config.tls_creds &&
        *s->config.tls_creds) {
        return s->config.tls_creds;
    }

    return NULL;
}

const char *migrate_tls_hostname(void)
{
    MigrationState *s = migrate_get_current();

    if (s->config.tls_hostname &&
        *s->config.tls_hostname) {
        return s->config.tls_hostname;
    }

    return NULL;
}

bool migrate_tls(void)
{
    return !!migrate_tls_creds();
}

uint64_t migrate_vcpu_dirty_limit_period(void)
{
    MigrationState *s = migrate_get_current();

    return s->config.x_vcpu_dirty_limit_period;
}

uint64_t migrate_xbzrle_cache_size(void)
{
    MigrationState *s = migrate_get_current();

    return s->config.xbzrle_cache_size;
}

ZeroPageDetection migrate_zero_page_detection(void)
{
    MigrationState *s = migrate_get_current();

    return s->config.zero_page_detection;
}

AnnounceParameters *migrate_announce_params(void)
{
    static AnnounceParameters ap;

    MigrationState *s = migrate_get_current();

    ap.initial = s->config.announce_initial;
    ap.max = s->config.announce_max;
    ap.rounds = s->config.announce_rounds;
    ap.step = s->config.announce_step;

    return &ap;
}

MigrationParameters *qmp_query_migrate_parameters(Error **errp)
{
    MigrationParameters *params;
    MigrationState *s = migrate_get_current();

    /* TODO use QAPI_CLONE() instead of duplicating it inline */
    params = g_malloc0(sizeof(*params));
    params->has_throttle_trigger_threshold = true;
    params->throttle_trigger_threshold = s->config.throttle_trigger_threshold;
    params->has_cpu_throttle_initial = true;
    params->cpu_throttle_initial = s->config.cpu_throttle_initial;
    params->has_cpu_throttle_increment = true;
    params->cpu_throttle_increment = s->config.cpu_throttle_increment;
    params->has_cpu_throttle_tailslow = true;
    params->cpu_throttle_tailslow = s->config.cpu_throttle_tailslow;
    params->tls_creds = g_strdup(s->config.tls_creds ?
                                 s->config.tls_creds : "");
    params->tls_hostname = g_strdup(s->config.tls_hostname ?
                                    s->config.tls_hostname : "");
    params->tls_authz = g_strdup(s->config.tls_authz ?
                                 s->config.tls_authz : "");
    params->has_max_bandwidth = true;
    params->max_bandwidth = s->config.max_bandwidth;
    params->has_avail_switchover_bandwidth = true;
    params->avail_switchover_bandwidth = s->config.avail_switchover_bandwidth;
    params->has_downtime_limit = true;
    params->downtime_limit = s->config.downtime_limit;
    params->has_x_checkpoint_delay = true;
    params->x_checkpoint_delay = s->config.x_checkpoint_delay;
    params->has_multifd_channels = true;
    params->multifd_channels = s->config.multifd_channels;
    params->has_multifd_compression = true;
    params->multifd_compression = s->config.multifd_compression;
    params->has_multifd_zlib_level = true;
    params->multifd_zlib_level = s->config.multifd_zlib_level;
    params->has_multifd_qatzip_level = true;
    params->multifd_qatzip_level = s->config.multifd_qatzip_level;
    params->has_multifd_zstd_level = true;
    params->multifd_zstd_level = s->config.multifd_zstd_level;
    params->has_xbzrle_cache_size = true;
    params->xbzrle_cache_size = s->config.xbzrle_cache_size;
    params->has_max_postcopy_bandwidth = true;
    params->max_postcopy_bandwidth = s->config.max_postcopy_bandwidth;
    params->has_max_cpu_throttle = true;
    params->max_cpu_throttle = s->config.max_cpu_throttle;
    params->has_announce_initial = true;
    params->announce_initial = s->config.announce_initial;
    params->has_announce_max = true;
    params->announce_max = s->config.announce_max;
    params->has_announce_rounds = true;
    params->announce_rounds = s->config.announce_rounds;
    params->has_announce_step = true;
    params->announce_step = s->config.announce_step;

    if (s->config.has_block_bitmap_mapping) {
        params->has_block_bitmap_mapping = true;
        params->block_bitmap_mapping =
            QAPI_CLONE(BitmapMigrationNodeAliasList,
                       s->config.block_bitmap_mapping);
    }

    params->has_x_vcpu_dirty_limit_period = true;
    params->x_vcpu_dirty_limit_period = s->config.x_vcpu_dirty_limit_period;
    params->has_vcpu_dirty_limit = true;
    params->vcpu_dirty_limit = s->config.vcpu_dirty_limit;
    params->has_mode = true;
    params->mode = s->config.mode;
    params->has_zero_page_detection = true;
    params->zero_page_detection = s->config.zero_page_detection;
    params->has_direct_io = true;
    params->direct_io = s->config.direct_io;

    return params;
}

void migrate_config_init(MigrationConfig *params)
{
    /* these should match the parameters in migration_properties */
    params->has_throttle_trigger_threshold = true;
    params->has_cpu_throttle_initial = true;
    params->has_cpu_throttle_increment = true;
    params->has_cpu_throttle_tailslow = true;
    params->has_max_bandwidth = true;
    params->has_downtime_limit = true;
    params->has_x_checkpoint_delay = true;
    params->has_multifd_channels = true;
    params->has_multifd_compression = true;
    params->has_multifd_zlib_level = true;
    params->has_multifd_qatzip_level = true;
    params->has_multifd_zstd_level = true;
    params->has_xbzrle_cache_size = true;
    params->has_max_postcopy_bandwidth = true;
    params->has_max_cpu_throttle = true;
    params->has_announce_initial = true;
    params->has_announce_max = true;
    params->has_announce_rounds = true;
    params->has_announce_step = true;
    params->has_x_vcpu_dirty_limit_period = true;
    params->has_vcpu_dirty_limit = true;
    params->has_mode = true;
    params->has_zero_page_detection = true;
    params->has_direct_io = true;
    params->has_avail_switchover_bandwidth = true;
    params->has_block_bitmap_mapping = true;
}

/*
 * Check whether the options are valid. Error will be put into errp
 * (if provided). Return true if valid, otherwise false.
 */
bool migrate_config_check(MigrationConfig *params, Error **errp)
{
    ERRP_GUARD();

    if (params->has_throttle_trigger_threshold &&
        (params->throttle_trigger_threshold < 1 ||
         params->throttle_trigger_threshold > 100)) {
        error_setg(errp, "Option throttle_trigger_threshold expects "
                   "an integer in the range of 1 to 100");
        return false;
    }

    if (params->has_cpu_throttle_initial &&
        (params->cpu_throttle_initial < 1 ||
         params->cpu_throttle_initial > 99)) {
        error_setg(errp, "Option cpu_throttle_initial expects "
                   "an integer in the range of 1 to 99");
        return false;
    }

    if (params->has_cpu_throttle_increment &&
        (params->cpu_throttle_increment < 1 ||
         params->cpu_throttle_increment > 99)) {
        error_setg(errp, "Option cpu_throttle_increment expects "
                   "an integer in the range of 1 to 99");
        return false;
    }

    if (params->has_max_bandwidth && (params->max_bandwidth > SIZE_MAX)) {
        error_setg(errp, "Option max_bandwidth expects "
                   "an integer in the range of 0 to "stringify(SIZE_MAX)
                   " bytes/second");
        return false;
    }

    if (params->has_avail_switchover_bandwidth &&
        (params->avail_switchover_bandwidth > SIZE_MAX)) {
        error_setg(errp, "Option avail_switchover_bandwidth expects "
                   "an integer in the range of 0 to "stringify(SIZE_MAX)
                   " bytes/second");
        return false;
    }

    if (params->has_downtime_limit &&
        (params->downtime_limit > MAX_MIGRATE_DOWNTIME)) {
        error_setg(errp, "Option downtime_limit expects "
                   "an integer in the range of 0 to "
                    stringify(MAX_MIGRATE_DOWNTIME)" ms");
        return false;
    }

    if (params->has_multifd_channels && (params->multifd_channels < 1)) {
        error_setg(errp, "Option multifd_channels expects "
                   "a value between 1 and 255");
        return false;
    }

    if (params->has_multifd_zlib_level &&
        (params->multifd_zlib_level > 9)) {
        error_setg(errp, "Option multifd_zlib_level expects "
                   "a value between 0 and 9");
        return false;
    }

    if (params->has_multifd_qatzip_level &&
        ((params->multifd_qatzip_level > 9) ||
        (params->multifd_qatzip_level < 1))) {
        error_setg(errp, "Option multifd_qatzip_level expects "
                   "a value between 1 and 9");
        return false;
    }

    if (params->has_multifd_zstd_level &&
        (params->multifd_zstd_level > 20)) {
        error_setg(errp, "Option multifd_zstd_level expects "
                   "a value between 0 and 20");
        return false;
    }

    if (params->has_xbzrle_cache_size &&
        (params->xbzrle_cache_size < qemu_target_page_size() ||
         !is_power_of_2(params->xbzrle_cache_size))) {
        error_setg(errp, "Option xbzrle_cache_size expects "
                   "a power of two no less than the target page size");
        return false;
    }

    if (params->has_max_cpu_throttle &&
        (params->max_cpu_throttle < params->cpu_throttle_initial ||
         params->max_cpu_throttle > 99)) {
        error_setg(errp, "max_Option cpu_throttle expects "
                   "an integer in the range of cpu_throttle_initial to 99");
        return false;
    }

    if (params->has_announce_initial &&
        params->announce_initial > 100000) {
        error_setg(errp, "Option announce_initial expects "
                   "a value between 0 and 100000");
        return false;
    }
    if (params->has_announce_max &&
        params->announce_max > 100000) {
        error_setg(errp, "Option announce_max expects "
                   "a value between 0 and 100000");
        return false;
    }
    if (params->has_announce_rounds &&
        params->announce_rounds > 1000) {
        error_setg(errp, "Option announce_rounds expects "
                   "a value between 0 and 1000");
        return false;
    }
    if (params->has_announce_step &&
        (params->announce_step < 1 ||
        params->announce_step > 10000)) {
        error_setg(errp, "Option announce_step expects "
                   "a value between 0 and 10000");
        return false;
    }

    if (params->has_block_bitmap_mapping &&
        !check_dirty_bitmap_mig_alias_map(params->block_bitmap_mapping, errp)) {
        error_prepend(errp, "Invalid mapping given for block-bitmap-mapping: ");
        return false;
    }

#ifdef CONFIG_LINUX
    if (migrate_zero_copy_send() &&
        ((params->has_multifd_compression && params->multifd_compression) ||
         (params->tls_creds && *params->tls_creds))) {
        error_setg(errp,
                   "Zero copy only available for non-compressed non-TLS multifd migration");
        return false;
    }
#endif

    if (migrate_mapped_ram() &&
        (migrate_multifd_compression() || migrate_tls())) {
        error_setg(errp,
                   "Mapped-ram only available for non-compressed non-TLS multifd migration");
        return false;
    }

    if (params->has_x_vcpu_dirty_limit_period &&
        (params->x_vcpu_dirty_limit_period < 1 ||
         params->x_vcpu_dirty_limit_period > 1000)) {
        error_setg(errp, "Option x-vcpu-dirty-limit-period expects "
                   "a value between 1 and 1000");
        return false;
    }

    if (params->has_vcpu_dirty_limit &&
        (params->vcpu_dirty_limit < 1)) {
        error_setg(errp,
                   "Option 'vcpu_dirty_limit' must be greater than 1 MB/s");
        return false;
    }

    if (params->has_direct_io && params->direct_io && !qemu_has_direct_io()) {
        error_setg(errp, "No build-time support for direct-io");
        return false;
    }

    return true;
}

/*
 * Compatibility layer to convert MigrateSetParameters to
 * MigrationConfig. In the existing QMP user interface, the
 * migrate-set-parameters command takes the TLS options as 'StrOrNull'
 * while the query-migrate-parameters command returns the TLS strings
 * as 'str'.
 */
static void migrate_params_copy_compat(MigrationConfig *dst,
                                       MigrateSetParameters *src)
{
    /* copy the common elements between the two */
    QAPI_CLONE_MEMBERS(MigrationConfigBase,
                       (MigrationConfigBase *)dst,
                       (MigrationConfigBase *)src);

     /* now copy the elements of different type */
    if (src->tls_creds) {
        if (src->tls_creds->type == QTYPE_QNULL) {
            dst->tls_creds = NULL;
        } else {
            dst->tls_creds = src->tls_creds->u.s;
        }
    }

    if (src->tls_hostname) {
        if (src->tls_hostname->type == QTYPE_QNULL) {
            dst->tls_hostname = NULL;
        } else {
            dst->tls_hostname = src->tls_hostname->u.s;
        }
    }

    if (src->tls_authz) {
        if (src->tls_authz->type == QTYPE_QNULL) {
            dst->tls_authz = NULL;
        } else {
            dst->tls_authz = src->tls_authz->u.s;
        }
    }
}

static void migrate_config_apply(MigrationConfig *new)
{
    MigrationState *s = migrate_get_current();

    if (new->has_throttle_trigger_threshold) {
        s->config.throttle_trigger_threshold = new->throttle_trigger_threshold;
    }

    if (new->has_cpu_throttle_initial) {
        s->config.cpu_throttle_initial = new->cpu_throttle_initial;
    }

    if (new->has_cpu_throttle_increment) {
        s->config.cpu_throttle_increment = new->cpu_throttle_increment;
    }

    if (new->has_cpu_throttle_tailslow) {
        s->config.cpu_throttle_tailslow = new->cpu_throttle_tailslow;
    }

    if (new->tls_creds) {
        g_free(s->config.tls_creds);
        s->config.tls_creds = g_strdup(new->tls_creds);
    }

    if (new->tls_hostname) {
        g_free(s->config.tls_hostname);
        s->config.tls_hostname = g_strdup(new->tls_hostname);
    }

    if (new->tls_authz) {
        g_free(s->config.tls_authz);
        s->config.tls_authz = g_strdup(new->tls_authz);
    }

    if (new->has_max_bandwidth) {
        s->config.max_bandwidth = new->max_bandwidth;
    }

    if (new->has_avail_switchover_bandwidth) {
        s->config.avail_switchover_bandwidth = new->avail_switchover_bandwidth;
    }

    if (new->has_downtime_limit) {
        s->config.downtime_limit = new->downtime_limit;
    }

    if (new->has_x_checkpoint_delay) {
        s->config.x_checkpoint_delay = new->x_checkpoint_delay;
    }

    if (new->has_multifd_channels) {
        s->config.multifd_channels = new->multifd_channels;
    }
    if (new->has_multifd_compression) {
        s->config.multifd_compression = new->multifd_compression;
    }
    if (new->has_multifd_qatzip_level) {
        s->config.multifd_qatzip_level = new->multifd_qatzip_level;
    }
    if (new->has_multifd_zlib_level) {
        s->config.multifd_zlib_level = new->multifd_zlib_level;
    }
    if (new->has_multifd_zstd_level) {
        s->config.multifd_zstd_level = new->multifd_zstd_level;
    }
    if (new->has_xbzrle_cache_size) {
        s->config.xbzrle_cache_size = new->xbzrle_cache_size;
    }
    if (new->has_max_postcopy_bandwidth) {
        s->config.max_postcopy_bandwidth = new->max_postcopy_bandwidth;
    }
    if (new->has_max_cpu_throttle) {
        s->config.max_cpu_throttle = new->max_cpu_throttle;
    }
    if (new->has_announce_initial) {
        s->config.announce_initial = new->announce_initial;
    }
    if (new->has_announce_max) {
        s->config.announce_max = new->announce_max;
    }
    if (new->has_announce_rounds) {
        s->config.announce_rounds = new->announce_rounds;
    }
    if (new->has_announce_step) {
        s->config.announce_step = new->announce_step;
    }

    if (new->has_block_bitmap_mapping) {
        qapi_free_BitmapMigrationNodeAliasList(
            s->config.block_bitmap_mapping);

        s->config.has_block_bitmap_mapping = true;
        s->config.block_bitmap_mapping =
            QAPI_CLONE(BitmapMigrationNodeAliasList,
                       new->block_bitmap_mapping);
    }

    if (new->has_x_vcpu_dirty_limit_period) {
        s->config.x_vcpu_dirty_limit_period =
            new->x_vcpu_dirty_limit_period;
    }
    if (new->has_vcpu_dirty_limit) {
        s->config.vcpu_dirty_limit = new->vcpu_dirty_limit;
    }

    if (new->has_mode) {
        s->config.mode = new->mode;
    }

    if (new->has_zero_page_detection) {
        s->config.zero_page_detection = new->zero_page_detection;
    }

    if (new->has_direct_io) {
        s->config.direct_io = new->direct_io;
    }
}

static void migrate_post_update_config(MigrationConfig *new, Error **errp)
{
    MigrationState *s = migrate_get_current();

    if (new->has_max_bandwidth) {
        if (s->to_dst_file && !migration_in_postcopy()) {
            migration_rate_set(new->max_bandwidth);
        }
    }

    if (new->has_x_checkpoint_delay) {
        colo_checkpoint_delay_set();
    }

    if (new->has_xbzrle_cache_size) {
        xbzrle_cache_resize(new->xbzrle_cache_size, errp);
    }

    if (new->has_max_postcopy_bandwidth) {
        if (s->to_dst_file && migration_in_postcopy()) {
            migration_rate_set(new->max_postcopy_bandwidth);
        }
    }
}

void qmp_migrate_set_parameters(MigrateSetParameters *params, Error **errp)
{
    MigrationConfig tmp = {};

    migrate_params_copy_compat(&tmp, params);

    if (!migrate_config_check(&tmp, errp)) {
        /* Invalid parameter */
        return;
    }

    migrate_config_apply(&tmp);
    migrate_post_update_config(&tmp, errp);
}
