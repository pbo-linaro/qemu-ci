/*
 * Data structures and functions shared between variants of the macOS
 * ParavirtualizedGraphics.framework based apple-gfx display adapter.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef QEMU_APPLE_GFX_H
#define QEMU_APPLE_GFX_H

#define TYPE_APPLE_GFX_MMIO         "apple-gfx-mmio"
#define TYPE_APPLE_GFX_PCI          "apple-gfx-pci"

#include "qemu/osdep.h"
#include <dispatch/dispatch.h>
#import <ParavirtualizedGraphics/ParavirtualizedGraphics.h>
#include "qemu/typedefs.h"
#include "exec/memory.h"
#include "hw/qdev-properties.h"
#include "ui/surface.h"

@class PGDeviceDescriptor;
@protocol PGDevice;
@protocol PGDisplay;
@protocol MTLDevice;
@protocol MTLTexture;
@protocol MTLCommandQueue;

typedef QTAILQ_HEAD(, PGTask_s) PGTaskList;

struct AppleGFXDisplayMode;
typedef struct AppleGFXState {
    /* Initialised on init/realize() */
    MemoryRegion iomem_gfx;
    id<PGDevice> pgdev;
    id<PGDisplay> pgdisp;
    QemuConsole *con;
    id<MTLDevice> mtl;
    id<MTLCommandQueue> mtl_queue;
    dispatch_queue_t render_queue;
    struct AppleGFXDisplayMode *display_modes;
    uint32_t num_display_modes;
    /*
     * QemuMutex & QemuConds for awaiting completion of PVG memory-mapping and
     * reading requests after submitting them to run in the AIO context.
     * QemuCond (rather than QemuEvent) are used so multiple concurrent jobs
     * can be handled safely.
     * The state associated with each job is tracked in a AppleGFX*Job struct
     * for each kind of job; instances are allocated on the caller's stack.
     * This struct also contains the completion flag which is used in
     * conjunction with the condition variable.
     */
    QemuMutex job_mutex;
    QemuCond task_map_job_cond;
    QemuCond mem_read_job_cond;

    /* tasks is protected by task_mutex */
    QemuMutex task_mutex;
    PGTaskList tasks;

    /* Mutable state (BQL) */
    QEMUCursor *cursor;
    bool cursor_show;
    bool gfx_update_requested;
    bool new_frame_ready;
    bool using_managed_texture_storage;
    int32_t pending_frames;
    void *vram;
    DisplaySurface *surface;
    id<MTLTexture> texture;
} AppleGFXState;

typedef struct AppleGFXDisplayMode {
    uint16_t width_px;
    uint16_t height_px;
    uint16_t refresh_rate_hz;
} AppleGFXDisplayMode;

void apple_gfx_common_init(Object *obj, AppleGFXState *s, const char* obj_name);
void apple_gfx_common_realize(AppleGFXState *s, PGDeviceDescriptor *desc,
                              Error **errp);
uintptr_t apple_gfx_host_address_for_gpa_range(uint64_t guest_physical,
                                               uint64_t length, bool read_only,
                                               MemoryRegion **mapping_in_region);
void apple_gfx_await_bh_job(AppleGFXState *s, QemuCond *job_cond,
                            bool *job_done_flag);

extern const PropertyInfo qdev_prop_display_mode;

#endif

