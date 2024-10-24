#ifndef QEMU_APPLE_GFX_H
#define QEMU_APPLE_GFX_H

#define TYPE_APPLE_GFX_MMIO         "apple-gfx-mmio"
#define TYPE_APPLE_GFX_PCI          "apple-gfx-pci"

#include "qemu/osdep.h"
#include <dispatch/dispatch.h>
#import <ParavirtualizedGraphics/ParavirtualizedGraphics.h>
#include "qemu/typedefs.h"
#include "exec/memory.h"
#include "ui/surface.h"

@class PGDeviceDescriptor;
@protocol PGDevice;
@protocol PGDisplay;
@protocol MTLDevice;
@protocol MTLTexture;
@protocol MTLCommandQueue;

typedef QTAILQ_HEAD(, PGTask_s) PGTaskList;

struct AppleGFXMapMemoryJob;
typedef struct AppleGFXState {
    MemoryRegion iomem_gfx;
    id<PGDevice> pgdev;
    id<PGDisplay> pgdisp;
    PGTaskList tasks;
    QemuConsole *con;
    id<MTLDevice> mtl;
    id<MTLCommandQueue> mtl_queue;
    bool cursor_show;
    QEMUCursor *cursor;

    /* For running PVG memory-mapping requests in the AIO context */
    QemuCond job_cond;
    QemuMutex job_mutex;

    dispatch_queue_t render_queue;
    /* The following fields should only be accessed from the BQL: */
    bool gfx_update_requested;
    bool new_frame_ready;
    bool using_managed_texture_storage;
    int32_t pending_frames;
    void *vram;
    DisplaySurface *surface;
    id<MTLTexture> texture;
} AppleGFXState;

void apple_gfx_common_init(Object *obj, AppleGFXState *s, const char* obj_name);
void apple_gfx_common_realize(AppleGFXState *s, PGDeviceDescriptor *desc,
                              Error **errp);
uintptr_t apple_gfx_host_address_for_gpa_range(uint64_t guest_physical,
                                               uint64_t length, bool read_only);
void apple_gfx_await_bh_job(AppleGFXState *s, bool *job_done_flag);

#endif

