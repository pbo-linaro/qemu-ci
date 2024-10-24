/*
 * QEMU Apple ParavirtualizedGraphics.framework device
 *
 * Copyright © 2023 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * ParavirtualizedGraphics.framework is a set of libraries that macOS provides
 * which implements 3d graphics passthrough to the host as well as a
 * proprietary guest communication channel to drive it. This device model
 * implements support to drive that library from within QEMU.
 */

#include "qemu/osdep.h"
#import <ParavirtualizedGraphics/ParavirtualizedGraphics.h>
#include <mach/mach_vm.h>
#include "apple-gfx.h"
#include "trace.h"
#include "qemu-main.h"
#include "exec/address-spaces.h"
#include "migration/blocker.h"
#include "monitor/monitor.h"
#include "qemu/main-loop.h"
#include "qemu/cutils.h"
#include "qemu/log.h"
#include "qapi/visitor.h"
#include "qapi/error.h"
#include "ui/console.h"

static const PGDisplayCoord_t apple_gfx_modes[] = {
    { .x = 1440, .y = 1080 },
    { .x = 1280, .y = 1024 },
};

/* This implements a type defined in <ParavirtualizedGraphics/PGDevice.h>
 * which is opaque from the framework's point of view. Typedef PGTask_t already
 * exists in the framework headers. */
struct PGTask_s {
    QTAILQ_ENTRY(PGTask_s) node;
    mach_vm_address_t address;
    uint64_t len;
};

static Error *apple_gfx_mig_blocker;

static void apple_gfx_render_frame_completed(AppleGFXState *s,
                                             uint32_t width, uint32_t height);

static inline dispatch_queue_t get_background_queue(void)
{
    return dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0);
}

static PGTask_t *apple_gfx_new_task(AppleGFXState *s, uint64_t len)
{
    mach_vm_address_t task_mem;
    PGTask_t *task;
    kern_return_t r;

    r = mach_vm_allocate(mach_task_self(), &task_mem, len, VM_FLAGS_ANYWHERE);
    if (r != KERN_SUCCESS || task_mem == 0) {
        return NULL;
    }

    task = g_new0(PGTask_t, 1);

    task->address = task_mem;
    task->len = len;
    QTAILQ_INSERT_TAIL(&s->tasks, task, node);

    return task;
}

typedef struct AppleGFXIOJob {
    AppleGFXState *state;
    uint64_t offset;
    uint64_t value;
    bool completed;
} AppleGFXIOJob;

static void apple_gfx_do_read(void *opaque)
{
    AppleGFXIOJob *job = opaque;
    job->value = [job->state->pgdev mmioReadAtOffset:job->offset];
    qatomic_set(&job->completed, true);
    aio_wait_kick();
}

static uint64_t apple_gfx_read(void *opaque, hwaddr offset, unsigned size)
{
    AppleGFXIOJob job = {
        .state = opaque,
        .offset = offset,
        .completed = false,
    };
    AioContext *context = qemu_get_aio_context();
    dispatch_queue_t queue = get_background_queue();

    dispatch_async_f(queue, &job, apple_gfx_do_read);
    AIO_WAIT_WHILE(context, !qatomic_read(&job.completed));

    trace_apple_gfx_read(offset, job.value);
    return job.value;
}

static void apple_gfx_do_write(void *opaque)
{
    AppleGFXIOJob *job = opaque;
    [job->state->pgdev mmioWriteAtOffset:job->offset value:job->value];
    qatomic_set(&job->completed, true);
    aio_wait_kick();
}

static void apple_gfx_write(void *opaque, hwaddr offset, uint64_t val,
                            unsigned size)
{
    /* The methods mmioReadAtOffset: and especially mmioWriteAtOffset: can
     * trigger and block on operations on other dispatch queues, which in turn
     * may call back out on one or more of the callback blocks. For this reason,
     * and as we are holding the BQL, we invoke the I/O methods on a pool
     * thread and handle AIO tasks while we wait. Any work in the callbacks
     * requiring the BQL will in turn schedule BHs which this thread will
     * process while waiting. */
    AppleGFXIOJob job = {
        .state = opaque,
        .offset = offset,
        .value = val,
        .completed = false,
    };
    AioContext *context = qemu_get_current_aio_context();
    dispatch_queue_t queue = get_background_queue();

    dispatch_async_f(queue, &job, apple_gfx_do_write);
    AIO_WAIT_WHILE(context, !qatomic_read(&job.completed));

    trace_apple_gfx_write(offset, val);
}

static const MemoryRegionOps apple_gfx_ops = {
    .read = apple_gfx_read,
    .write = apple_gfx_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void apple_gfx_render_new_frame_bql_unlock(AppleGFXState *s)
{
    BOOL r;
    uint32_t width = surface_width(s->surface);
    uint32_t height = surface_height(s->surface);
    MTLRegion region = MTLRegionMake2D(0, 0, width, height);
    id<MTLCommandBuffer> command_buffer = [s->mtl_queue commandBuffer];
    id<MTLTexture> texture = s->texture;

    assert(bql_locked());
    [texture retain];

    bql_unlock();

    /* This is not safe to call from the BQL due to PVG-internal locks causing
     * deadlocks. */
    r = [s->pgdisp encodeCurrentFrameToCommandBuffer:command_buffer
                                             texture:texture
                                              region:region];
    if (!r) {
        [texture release];
        bql_lock();
        --s->pending_frames;
        bql_unlock();
        qemu_log_mask(LOG_GUEST_ERROR, "apple_gfx_render_new_frame_bql_unlock: "
                      "encodeCurrentFrameToCommandBuffer:texture:region: failed\n");
        return;
    }

    if (s->using_managed_texture_storage) {
        /* "Managed" textures exist in both VRAM and RAM and must be synced. */
        id<MTLBlitCommandEncoder> blit = [command_buffer blitCommandEncoder];
        [blit synchronizeResource:texture];
        [blit endEncoding];
    }
    [texture release];
    [command_buffer addCompletedHandler:
        ^(id<MTLCommandBuffer> cb)
        {
            dispatch_async(s->render_queue, ^{
                apple_gfx_render_frame_completed(s, width, height);
            });
        }];
    [command_buffer commit];
}

static void copy_mtl_texture_to_surface_mem(id<MTLTexture> texture, void *vram)
{
    /* TODO: Skip this entirely on a pure Metal or headless/guest-only
     * rendering path, else use a blit command encoder? Needs careful
     * (double?) buffering design. */
    size_t width = texture.width, height = texture.height;
    MTLRegion region = MTLRegionMake2D(0, 0, width, height);
    [texture getBytes:vram
          bytesPerRow:(width * 4)
        bytesPerImage:(width * height * 4)
           fromRegion:region
          mipmapLevel:0
                slice:0];
}

static void apple_gfx_render_frame_completed(AppleGFXState *s,
                                             uint32_t width, uint32_t height)
{
    bql_lock();
    --s->pending_frames;
    assert(s->pending_frames >= 0);

    /* Only update display if mode hasn't changed since we started rendering. */
    if (width == surface_width(s->surface) &&
        height == surface_height(s->surface)) {
        copy_mtl_texture_to_surface_mem(s->texture, s->vram);
        if (s->gfx_update_requested) {
            s->gfx_update_requested = false;
            dpy_gfx_update_full(s->con);
            graphic_hw_update_done(s->con);
            s->new_frame_ready = false;
        } else {
            s->new_frame_ready = true;
        }
    }
    if (s->pending_frames > 0) {
        apple_gfx_render_new_frame_bql_unlock(s);
    } else {
        bql_unlock();
    }
}

static void apple_gfx_fb_update_display(void *opaque)
{
    AppleGFXState *s = opaque;

    assert(bql_locked());
    if (s->new_frame_ready) {
        dpy_gfx_update_full(s->con);
        s->new_frame_ready = false;
        graphic_hw_update_done(s->con);
    } else if (s->pending_frames > 0) {
        s->gfx_update_requested = true;
    } else {
        graphic_hw_update_done(s->con);
    }
}

static const GraphicHwOps apple_gfx_fb_ops = {
    .gfx_update = apple_gfx_fb_update_display,
    .gfx_update_async = true,
};

static void update_cursor(AppleGFXState *s)
{
    assert(bql_locked());
    dpy_mouse_set(s->con, s->pgdisp.cursorPosition.x,
                  s->pgdisp.cursorPosition.y, s->cursor_show);
}

static void set_mode(AppleGFXState *s, uint32_t width, uint32_t height)
{
    MTLTextureDescriptor *textureDescriptor;

    if (s->surface &&
        width == surface_width(s->surface) &&
        height == surface_height(s->surface)) {
        return;
    }

    g_free(s->vram);
    [s->texture release];

    s->vram = g_malloc0_n(width * height, 4);
    s->surface = qemu_create_displaysurface_from(width, height, PIXMAN_LE_a8r8g8b8,
                                                 width * 4, s->vram);

    @autoreleasepool {
        textureDescriptor =
            [MTLTextureDescriptor
                texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                             width:width
                                            height:height
                                         mipmapped:NO];
        textureDescriptor.usage = s->pgdisp.minimumTextureUsage;
        s->texture = [s->mtl newTextureWithDescriptor:textureDescriptor];
    }

    s->using_managed_texture_storage =
        (s->texture.storageMode == MTLStorageModeManaged);
    dpy_gfx_replace_surface(s->con, s->surface);
}

static void create_fb(AppleGFXState *s)
{
    s->con = graphic_console_init(NULL, 0, &apple_gfx_fb_ops, s);
    set_mode(s, 1440, 1080);

    s->cursor_show = true;
}

static size_t apple_gfx_get_default_mmio_range_size(void)
{
    size_t mmio_range_size;
    @autoreleasepool {
        PGDeviceDescriptor *desc = [PGDeviceDescriptor new];
        mmio_range_size = desc.mmioLength;
        [desc release];
    }
    return mmio_range_size;
}

void apple_gfx_common_init(Object *obj, AppleGFXState *s, const char* obj_name)
{
    size_t mmio_range_size = apple_gfx_get_default_mmio_range_size();

    trace_apple_gfx_common_init(obj_name, mmio_range_size);
    memory_region_init_io(&s->iomem_gfx, obj, &apple_gfx_ops, s, obj_name,
                          mmio_range_size);

    /* TODO: PVG framework supports serialising device state: integrate it! */
}

typedef struct AppleGFXMapMemoryJob {
    AppleGFXState *state;
    PGTask_t *task;
    uint64_t virtual_offset;
    PGPhysicalMemoryRange_t *ranges;
    uint32_t range_count;
    bool read_only;
    bool success;
    bool done;
} AppleGFXMapMemoryJob;

uintptr_t apple_gfx_host_address_for_gpa_range(uint64_t guest_physical,
                                               uint64_t length, bool read_only)
{
    MemoryRegion *ram_region;
    uintptr_t host_address;
    hwaddr ram_region_offset = 0;
    hwaddr ram_region_length = length;

    ram_region = address_space_translate(&address_space_memory,
                                         guest_physical,
                                         &ram_region_offset,
                                         &ram_region_length, !read_only,
                                         MEMTXATTRS_UNSPECIFIED);

    if (!ram_region || ram_region_length < length ||
        !memory_access_is_direct(ram_region, !read_only)) {
        return 0;
    }

    host_address = (mach_vm_address_t)memory_region_get_ram_ptr(ram_region);
    if (host_address == 0) {
        return 0;
    }
    host_address += ram_region_offset;

    return host_address;
}

static void apple_gfx_map_memory(void *opaque)
{
    AppleGFXMapMemoryJob *job = opaque;
    AppleGFXState *s = job->state;
    PGTask_t *task                  = job->task;
    uint32_t range_count            = job->range_count;
    uint64_t virtual_offset         = job->virtual_offset;
    PGPhysicalMemoryRange_t *ranges = job->ranges;
    bool read_only                  = job->read_only;
    kern_return_t r;
    mach_vm_address_t target, source;
    vm_prot_t cur_protection, max_protection;
    bool success = true;

    g_assert(bql_locked());

    trace_apple_gfx_map_memory(task, range_count, virtual_offset, read_only);
    for (int i = 0; i < range_count; i++) {
        PGPhysicalMemoryRange_t *range = &ranges[i];

        target = task->address + virtual_offset;
        virtual_offset += range->physicalLength;

        trace_apple_gfx_map_memory_range(i, range->physicalAddress,
                                         range->physicalLength);

        source = apple_gfx_host_address_for_gpa_range(range->physicalAddress,
                                                      range->physicalLength,
                                                      read_only);
        if (source == 0) {
            success = false;
            continue;
        }

        MemoryRegion* alt_mr = NULL;
        mach_vm_address_t alt_source = (mach_vm_address_t)gpa2hva(&alt_mr, range->physicalAddress, range->physicalLength, NULL);
        g_assert(alt_source == source);

        cur_protection = 0;
        max_protection = 0;
        // Map guest RAM at range->physicalAddress into PG task memory range
        r = mach_vm_remap(mach_task_self(),
                          &target, range->physicalLength, vm_page_size - 1,
                          VM_FLAGS_FIXED | VM_FLAGS_OVERWRITE,
                          mach_task_self(),
                          source, false /* shared mapping, no copy */,
                          &cur_protection, &max_protection,
                          VM_INHERIT_COPY);
        trace_apple_gfx_remap(r, source, target);
        g_assert(r == KERN_SUCCESS);
    }

    qemu_mutex_lock(&s->job_mutex);
    job->success = success;
    job->done = true;
    qemu_cond_broadcast(&s->job_cond);
    qemu_mutex_unlock(&s->job_mutex);
}

void apple_gfx_await_bh_job(AppleGFXState *s, bool *job_done_flag)
{
    qemu_mutex_lock(&s->job_mutex);
    while (!*job_done_flag) {
        qemu_cond_wait(&s->job_cond, &s->job_mutex);
    }
    qemu_mutex_unlock(&s->job_mutex);
}

typedef struct AppleGFXReadMemoryJob {
    AppleGFXState *s;
    hwaddr physical_address;
    uint64_t length;
    void *dst;
    bool done;
} AppleGFXReadMemoryJob;

static void apple_gfx_do_read_memory(void *opaque)
{
    AppleGFXReadMemoryJob *job = opaque;
    AppleGFXState *s = job->s;

    cpu_physical_memory_read(job->physical_address, job->dst, job->length);

    qemu_mutex_lock(&s->job_mutex);
    job->done = true;
    qemu_cond_broadcast(&s->job_cond);
    qemu_mutex_unlock(&s->job_mutex);
}

static void apple_gfx_read_memory(AppleGFXState *s, hwaddr physical_address,
                                  uint64_t length, void *dst)
{
    AppleGFXReadMemoryJob job = {
        s, physical_address, length, dst
    };

    trace_apple_gfx_read_memory(physical_address, length, dst);

    /* Traversing the memory map requires RCU/BQL, so do it in a BH. */
    aio_bh_schedule_oneshot(qemu_get_aio_context(), apple_gfx_do_read_memory,
                            &job);
    apple_gfx_await_bh_job(s, &job.done);
}

static void apple_gfx_register_task_mapping_handlers(AppleGFXState *s,
                                                     PGDeviceDescriptor *desc)
{
    desc.createTask = ^(uint64_t vmSize, void * _Nullable * _Nonnull baseAddress) {
        PGTask_t *task = apple_gfx_new_task(s, vmSize);
        *baseAddress = (void *)task->address;
        trace_apple_gfx_create_task(vmSize, *baseAddress);
        return task;
    };

    desc.destroyTask = ^(PGTask_t * _Nonnull task) {
        trace_apple_gfx_destroy_task(task);
        QTAILQ_REMOVE(&s->tasks, task, node);
        mach_vm_deallocate(mach_task_self(), task->address, task->len);
        g_free(task);
    };

    desc.mapMemory = ^bool(PGTask_t * _Nonnull task, uint32_t range_count,
                       uint64_t virtual_offset, bool read_only,
                       PGPhysicalMemoryRange_t * _Nonnull ranges) {
        AppleGFXMapMemoryJob job = {
            .state = s,
            .task = task, .ranges = ranges, .range_count = range_count,
            .read_only = read_only, .virtual_offset = virtual_offset,
            .done = false, .success = true,
        };
        if (range_count > 0) {
            aio_bh_schedule_oneshot(qemu_get_aio_context(),
                                    apple_gfx_map_memory, &job);
            apple_gfx_await_bh_job(s, &job.done);
        }
        return job.success;
    };

    desc.unmapMemory = ^bool(PGTask_t * _Nonnull task, uint64_t virtualOffset,
                         uint64_t length) {
        kern_return_t r;
        mach_vm_address_t range_address;

        trace_apple_gfx_unmap_memory(task, virtualOffset, length);

        /* Replace task memory range with fresh pages, undoing the mapping
         * from guest RAM. */
        range_address = task->address + virtualOffset;
        r = mach_vm_allocate(mach_task_self(), &range_address, length,
                             VM_FLAGS_FIXED | VM_FLAGS_OVERWRITE);
        g_assert(r == KERN_SUCCESS);

        return true;
    };

    desc.readMemory = ^bool(uint64_t physical_address, uint64_t length,
                            void * _Nonnull dst) {
        apple_gfx_read_memory(s, physical_address, length, dst);
        return true;
    };
}

static PGDisplayDescriptor *apple_gfx_prepare_display_descriptor(AppleGFXState *s)
{
    PGDisplayDescriptor *disp_desc = [PGDisplayDescriptor new];

    disp_desc.name = @"QEMU display";
    disp_desc.sizeInMillimeters = NSMakeSize(400., 300.); /* A 20" display */
    disp_desc.queue = dispatch_get_main_queue();
    disp_desc.newFrameEventHandler = ^(void) {
        trace_apple_gfx_new_frame();
        dispatch_async(s->render_queue, ^{
            /* Drop frames if we get too far ahead. */
            bql_lock();
            if (s->pending_frames >= 2) {
                bql_unlock();
                return;
            }
            ++s->pending_frames;
            if (s->pending_frames > 1) {
                bql_unlock();
                return;
            }
            @autoreleasepool {
                apple_gfx_render_new_frame_bql_unlock(s);
            }
        });
    };
    disp_desc.modeChangeHandler = ^(PGDisplayCoord_t sizeInPixels,
                                    OSType pixelFormat) {
        trace_apple_gfx_mode_change(sizeInPixels.x, sizeInPixels.y);

        BQL_LOCK_GUARD();
        set_mode(s, sizeInPixels.x, sizeInPixels.y);
    };
    disp_desc.cursorGlyphHandler = ^(NSBitmapImageRep *glyph,
                                     PGDisplayCoord_t hotSpot) {
        [glyph retain];
        dispatch_async(get_background_queue(), ^{
            BQL_LOCK_GUARD();
            uint32_t bpp = glyph.bitsPerPixel;
            size_t width = glyph.pixelsWide;
            size_t height = glyph.pixelsHigh;
            size_t padding_bytes_per_row = glyph.bytesPerRow - width * 4;
            const uint8_t* px_data = glyph.bitmapData;

            trace_apple_gfx_cursor_set(bpp, width, height);

            if (s->cursor) {
                cursor_unref(s->cursor);
                s->cursor = NULL;
            }

            if (bpp == 32) { /* Shouldn't be anything else, but just to be safe...*/
                s->cursor = cursor_alloc(width, height);
                s->cursor->hot_x = hotSpot.x;
                s->cursor->hot_y = hotSpot.y;

                uint32_t *dest_px = s->cursor->data;

                for (size_t y = 0; y < height; ++y) {
                    for (size_t x = 0; x < width; ++x) {
                        /* NSBitmapImageRep's red & blue channels are swapped
                         * compared to QEMUCursor's. */
                        *dest_px =
                            (px_data[0] << 16u) |
                            (px_data[1] <<  8u) |
                            (px_data[2] <<  0u) |
                            (px_data[3] << 24u);
                        ++dest_px;
                        px_data += 4;
                    }
                    px_data += padding_bytes_per_row;
                }
                dpy_cursor_define(s->con, s->cursor);
                update_cursor(s);
            }
            [glyph release];
        });
    };
    disp_desc.cursorShowHandler = ^(BOOL show) {
        dispatch_async(get_background_queue(), ^{
            BQL_LOCK_GUARD();
            trace_apple_gfx_cursor_show(show);
            s->cursor_show = show;
            update_cursor(s);
        });
    };
    disp_desc.cursorMoveHandler = ^(void) {
        dispatch_async(get_background_queue(), ^{
            BQL_LOCK_GUARD();
            trace_apple_gfx_cursor_move();
            update_cursor(s);
        });
    };

    return disp_desc;
}

static NSArray<PGDisplayMode*>* apple_gfx_prepare_display_mode_array(void)
{
    PGDisplayMode *modes[ARRAY_SIZE(apple_gfx_modes)];
    NSArray<PGDisplayMode*>* mode_array = nil;
    int i;

    for (i = 0; i < ARRAY_SIZE(apple_gfx_modes); i++) {
        modes[i] =
            [[PGDisplayMode alloc] initWithSizeInPixels:apple_gfx_modes[i] refreshRateInHz:60.];
    }

    mode_array = [NSArray arrayWithObjects:modes count:ARRAY_SIZE(apple_gfx_modes)];

    for (i = 0; i < ARRAY_SIZE(apple_gfx_modes); i++) {
        [modes[i] release];
        modes[i] = nil;
    }

    return mode_array;
}

static id<MTLDevice> copy_suitable_metal_device(void)
{
    id<MTLDevice> dev = nil;
    NSArray<id<MTLDevice>> *devs = MTLCopyAllDevices();

    /* Prefer a unified memory GPU. Failing that, pick a non-removable GPU. */
    for (size_t i = 0; i < devs.count; ++i) {
        if (devs[i].hasUnifiedMemory) {
            dev = devs[i];
            break;
        }
        if (!devs[i].removable) {
            dev = devs[i];
        }
    }

    if (dev != nil) {
        [dev retain];
    } else {
        dev = MTLCreateSystemDefaultDevice();
    }
    [devs release];

    return dev;
}

void apple_gfx_common_realize(AppleGFXState *s, PGDeviceDescriptor *desc,
                              Error **errp)
{
    PGDisplayDescriptor *disp_desc = nil;

    if (apple_gfx_mig_blocker == NULL) {
        error_setg(&apple_gfx_mig_blocker,
                  "Migration state blocked by apple-gfx display device");
        if (migrate_add_blocker(&apple_gfx_mig_blocker, errp) < 0) {
            return;
        }
    }

    QTAILQ_INIT(&s->tasks);
    s->render_queue = dispatch_queue_create("apple-gfx.render",
                                            DISPATCH_QUEUE_SERIAL);
    s->mtl = copy_suitable_metal_device();
    s->mtl_queue = [s->mtl newCommandQueue];

    desc.device = s->mtl;

    apple_gfx_register_task_mapping_handlers(s, desc);

    s->pgdev = PGNewDeviceWithDescriptor(desc);

    disp_desc = apple_gfx_prepare_display_descriptor(s);
    s->pgdisp = [s->pgdev newDisplayWithDescriptor:disp_desc
                                              port:0 serialNum:1234];
    [disp_desc release];
    s->pgdisp.modeList = apple_gfx_prepare_display_mode_array();

    create_fb(s);

    qemu_mutex_init(&s->job_mutex);
    qemu_cond_init(&s->job_cond);
}
