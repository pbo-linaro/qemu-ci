/*
 * QEMU Apple ParavirtualizedGraphics.framework device, MMIO (arm64) variant
 *
 * Copyright © 2023 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * ParavirtualizedGraphics.framework is a set of libraries that macOS provides
 * which implements 3d graphics passthrough to the host as well as a
 * proprietary guest communication channel to drive it. This device model
 * implements support to drive that library from within QEMU as an MMIO-based
 * system device for macOS on arm64 VMs.
 */

#include "qemu/osdep.h"
#import <ParavirtualizedGraphics/ParavirtualizedGraphics.h>
#include "apple-gfx.h"
#include "monitor/monitor.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "trace.h"
#include "qemu/log.h"

OBJECT_DECLARE_SIMPLE_TYPE(AppleGFXMMIOState, APPLE_GFX_MMIO)

/*
 * ParavirtualizedGraphics.Framework only ships header files for the PCI
 * variant which does not include IOSFC descriptors and host devices. We add
 * their definitions here so that we can also work with the ARM version.
 */
typedef bool(^IOSFCRaiseInterrupt)(uint32_t vector);
typedef bool(^IOSFCUnmapMemory)(
    void *, void *, void *, void *, void *, void *);
typedef bool(^IOSFCMapMemory)(
    uint64_t phys, uint64_t len, bool ro, void **va, void *, void *);

@interface PGDeviceDescriptor (IOSurfaceMapper)
@property (readwrite, nonatomic) bool usingIOSurfaceMapper;
@end

@interface PGIOSurfaceHostDeviceDescriptor : NSObject
-(PGIOSurfaceHostDeviceDescriptor *)init;
@property (readwrite, nonatomic, copy, nullable) IOSFCMapMemory mapMemory;
@property (readwrite, nonatomic, copy, nullable) IOSFCUnmapMemory unmapMemory;
@property (readwrite, nonatomic, copy, nullable) IOSFCRaiseInterrupt raiseInterrupt;
@end

@interface PGIOSurfaceHostDevice : NSObject
-(instancetype)initWithDescriptor:(PGIOSurfaceHostDeviceDescriptor *)desc;
-(uint32_t)mmioReadAtOffset:(size_t)offset;
-(void)mmioWriteAtOffset:(size_t)offset value:(uint32_t)value;
@end

struct AppleGFXMapSurfaceMemoryJob;
struct AppleGFXMMIOState {
    SysBusDevice parent_obj;

    AppleGFXState common;

    QemuCond iosfc_map_job_cond;
    QemuCond iosfc_unmap_job_cond;
    qemu_irq irq_gfx;
    qemu_irq irq_iosfc;
    MemoryRegion iomem_iosfc;
    PGIOSurfaceHostDevice *pgiosfc;

    GArray *iosfc_mapped_regions; /* array of AppleGFXMMIOMappedRegion */
};

typedef struct AppleGFXMMIOJob {
    AppleGFXMMIOState *state;
    uint64_t offset;
    uint64_t value;
    bool completed;
} AppleGFXMMIOJob;

static void iosfc_do_read(void *opaque)
{
    AppleGFXMMIOJob *job = opaque;
    job->value = [job->state->pgiosfc mmioReadAtOffset:job->offset];
    qatomic_set(&job->completed, true);
    aio_wait_kick();
}

static uint64_t iosfc_read(void *opaque, hwaddr offset, unsigned size)
{
    AppleGFXMMIOJob job = {
        .state = opaque,
        .offset = offset,
        .completed = false,
    };
    dispatch_queue_t queue = dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0);

    dispatch_async_f(queue, &job, iosfc_do_read);
    AIO_WAIT_WHILE(NULL, !qatomic_read(&job.completed));

    trace_apple_gfx_mmio_iosfc_read(offset, job.value);
    return job.value;
}

static void iosfc_do_write(void *opaque)
{
    AppleGFXMMIOJob *job = opaque;
    [job->state->pgiosfc mmioWriteAtOffset:job->offset value:job->value];
    qatomic_set(&job->completed, true);
    aio_wait_kick();
}

static void iosfc_write(void *opaque, hwaddr offset, uint64_t val,
                        unsigned size)
{
    AppleGFXMMIOJob job = {
        .state = opaque,
        .offset = offset,
        .value = val,
        .completed = false,
    };
    dispatch_queue_t queue = dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0);

    dispatch_async_f(queue, &job, iosfc_do_write);
    AIO_WAIT_WHILE(NULL, !qatomic_read(&job.completed));

    trace_apple_gfx_mmio_iosfc_write(offset, val);
}

static const MemoryRegionOps apple_iosfc_ops = {
    .read = iosfc_read,
    .write = iosfc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
};

static void raise_irq(void *opaque)
{
    qemu_irq *irq = opaque;

    qemu_irq_pulse(*irq);
}

typedef struct AppleGFXMapSurfaceMemoryJob {
    uint64_t guest_physical_address;
    uint64_t guest_physical_length;
    void *result_mem;
    AppleGFXMMIOState *state;
    bool read_only;
    bool success;
    bool done;
} AppleGFXMapSurfaceMemoryJob;

typedef struct AppleGFXMMIOMappedRegion {
    MemoryRegion *region;
    uint64_t map_count;
    uintptr_t host_virtual_start;
    uintptr_t host_virtual_end;
} AppleGFXMMIOMappedRegion;

static void apple_gfx_mmio_map_surface_memory(void *opaque)
{
    AppleGFXMapSurfaceMemoryJob *job = opaque;
    AppleGFXMMIOState *s = job->state;
    mach_vm_address_t mem;
    MemoryRegion *region = NULL;
    GArray *regions = s->iosfc_mapped_regions;
    AppleGFXMMIOMappedRegion *mapped_region;
    size_t i;

    mem = apple_gfx_host_address_for_gpa_range(job->guest_physical_address,
                                               job->guest_physical_length,
                                               job->read_only, &region);

    if (mem != 0) {
        for (i = 0; i < regions->len; ++i) {
            mapped_region = &g_array_index(regions, AppleGFXMMIOMappedRegion, i);
            if (region == mapped_region->region) {
                ++mapped_region->map_count;
                break;
            }
        }

        if (i >= regions->len) {
            /* No existing mapping to this region found, keep a reference and save
             */
            uintptr_t start = (uintptr_t)memory_region_get_ram_ptr(region);
            AppleGFXMMIOMappedRegion new_region = {
                region, 1,
                start,
                start + memory_region_size(region)
            };
            memory_region_ref(region);
            g_array_append_val(regions, new_region);
            trace_apple_gfx_iosfc_map_memory_new_region(
                i, region, start, new_region.host_virtual_end);
        }
    }

    qemu_mutex_lock(&s->common.job_mutex);
    job->result_mem = (void *)mem;
    job->success = mem != 0;
    job->done = true;
    qemu_cond_broadcast(&s->iosfc_map_job_cond);
    qemu_mutex_unlock(&s->common.job_mutex);
}

typedef struct AppleGFXUnmapSurfaceMemoryJob {
    void *virtual_address;
    AppleGFXMMIOState *state;
    bool done;
} AppleGFXUnmapSurfaceMemoryJob;

static AppleGFXMMIOMappedRegion *find_mapped_region_containing(GArray *regions,
                                                               uintptr_t va,
                                                               size_t *index)
{
    size_t i;
    AppleGFXMMIOMappedRegion *mapped_region;

    for (i = 0; i < regions->len; ++i) {
        mapped_region = &g_array_index(regions, AppleGFXMMIOMappedRegion, i);
        if (va >= mapped_region->host_virtual_start &&
            va < mapped_region->host_virtual_end) {
            *index = i;
            return mapped_region;
        }
    }
    return NULL;
}

static void apple_gfx_mmio_unmap_surface_memory(void *opaque)
{
    AppleGFXUnmapSurfaceMemoryJob *job = opaque;
    AppleGFXMMIOState *s = job->state;
    uintptr_t mem = (uintptr_t)job->virtual_address;
    GArray *regions = s->iosfc_mapped_regions;
    size_t region_index;
    AppleGFXMMIOMappedRegion *mapped_region =
        find_mapped_region_containing(regions, mem, &region_index);

    if (mapped_region) {
        trace_apple_gfx_iosfc_unmap_memory_region(mem, region_index, mapped_region->map_count, mapped_region->region);
        if (--mapped_region->map_count == 0) {
            memory_region_unref(mapped_region->region);
            g_array_remove_index_fast(regions, region_index);
        }
    } else {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: memory at %p to be unmapped not "
                      "found in any of %u mapped regions.\n",
                      __func__,
                      job->virtual_address, regions->len);
    }

    qemu_mutex_lock(&s->common.job_mutex);
    job->done = true;
    qemu_cond_broadcast(&s->iosfc_unmap_job_cond);
    qemu_mutex_unlock(&s->common.job_mutex);
}

static PGIOSurfaceHostDevice *apple_gfx_prepare_iosurface_host_device(
    AppleGFXMMIOState *s)
{
    PGIOSurfaceHostDeviceDescriptor *iosfc_desc =
        [PGIOSurfaceHostDeviceDescriptor new];
    PGIOSurfaceHostDevice *iosfc_host_dev = nil;

    iosfc_desc.mapMemory =
        ^bool(uint64_t phys, uint64_t len, bool ro, void **va, void *e, void *f) {
            AppleGFXMapSurfaceMemoryJob job = {
                .guest_physical_address = phys, .guest_physical_length = len,
                .read_only = ro, .state = s,
            };

            aio_bh_schedule_oneshot(qemu_get_aio_context(),
                                    apple_gfx_mmio_map_surface_memory, &job);
            apple_gfx_await_bh_job(&s->common, &s->iosfc_map_job_cond, &job.done);

            *va = job.result_mem;

            trace_apple_gfx_iosfc_map_memory(phys, len, ro, va, e, f, *va,
                                             job.success);

            return job.success;
        };

    iosfc_desc.unmapMemory =
        ^bool(void *va, void *b, void *c, void *d, void *e, void *f) {
            AppleGFXUnmapSurfaceMemoryJob job = { va, s };
            trace_apple_gfx_iosfc_unmap_memory(va, b, c, d, e, f);

            aio_bh_schedule_oneshot(qemu_get_aio_context(),
                                    apple_gfx_mmio_unmap_surface_memory, &job);
            apple_gfx_await_bh_job(&s->common, &s->iosfc_unmap_job_cond, &job.done);

            return true;
        };

    iosfc_desc.raiseInterrupt = ^bool(uint32_t vector) {
        trace_apple_gfx_iosfc_raise_irq(vector);
        aio_bh_schedule_oneshot(qemu_get_aio_context(),
                                raise_irq, &s->irq_iosfc);
        return true;
    };

    iosfc_host_dev =
        [[PGIOSurfaceHostDevice alloc] initWithDescriptor:iosfc_desc];
    [iosfc_desc release];
    return iosfc_host_dev;
}

static void apple_gfx_mmio_realize(DeviceState *dev, Error **errp)
{
    @autoreleasepool {
        AppleGFXMMIOState *s = APPLE_GFX_MMIO(dev);
        PGDeviceDescriptor *desc = [PGDeviceDescriptor new];

        desc.raiseInterrupt = ^(uint32_t vector) {
            trace_apple_gfx_raise_irq(vector);
            aio_bh_schedule_oneshot(qemu_get_aio_context(),
                                    raise_irq, &s->irq_gfx);
        };

        desc.usingIOSurfaceMapper = true;
        s->pgiosfc = apple_gfx_prepare_iosurface_host_device(s);

        s->iosfc_mapped_regions =
            g_array_sized_new(false /* no termination */, true /* clear */,
                              sizeof(AppleGFXMMIOMappedRegion),
                              2 /* Usually no more RAM regions*/);

        apple_gfx_common_realize(&s->common, desc, errp);
        qemu_cond_init(&s->iosfc_map_job_cond);
        qemu_cond_init(&s->iosfc_unmap_job_cond);

        [desc release];
        desc = nil;
    }
}

static void apple_gfx_mmio_init(Object *obj)
{
    AppleGFXMMIOState *s = APPLE_GFX_MMIO(obj);

    apple_gfx_common_init(obj, &s->common, TYPE_APPLE_GFX_MMIO);

    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->common.iomem_gfx);
    memory_region_init_io(&s->iomem_iosfc, obj, &apple_iosfc_ops, s,
                          TYPE_APPLE_GFX_MMIO, 0x10000);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->iomem_iosfc);
    sysbus_init_irq(SYS_BUS_DEVICE(s), &s->irq_gfx);
    sysbus_init_irq(SYS_BUS_DEVICE(s), &s->irq_iosfc);
}

static void apple_gfx_mmio_reset(Object *obj, ResetType type)
{
    AppleGFXMMIOState *s = APPLE_GFX_MMIO(obj);
    [s->common.pgdev reset];
}


static void apple_gfx_mmio_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.hold = apple_gfx_mmio_reset;
    dc->hotpluggable = false;
    dc->realize = apple_gfx_mmio_realize;
}

static TypeInfo apple_gfx_mmio_types[] = {
    {
        .name          = TYPE_APPLE_GFX_MMIO,
        .parent        = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(AppleGFXMMIOState),
        .class_init    = apple_gfx_mmio_class_init,
        .instance_init = apple_gfx_mmio_init,
    }
};
DEFINE_TYPES(apple_gfx_mmio_types)
