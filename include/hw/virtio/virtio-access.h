/*
 * Virtio Accessor Support: In case your target can change endian.
 *
 * Copyright IBM, Corp. 2013
 *
 * Authors:
 *  Rusty Russell   <rusty@au.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#ifndef QEMU_VIRTIO_ACCESS_H
#define QEMU_VIRTIO_ACCESS_H

#include "exec/hwaddr.h"
#include "hw/virtio/virtio.h"
#include "hw/virtio/virtio-bus.h"

#if defined(TARGET_PPC64) || defined(TARGET_ARM)
#define LEGACY_VIRTIO_IS_BIENDIAN 1
#endif

static inline bool virtio_access_is_big_endian(VirtIODevice *vdev)
{
#if defined(LEGACY_VIRTIO_IS_BIENDIAN)
    return virtio_is_big_endian(vdev);
#elif TARGET_BIG_ENDIAN
    if (virtio_vdev_has_feature(vdev, VIRTIO_F_VERSION_1)) {
        /* Devices conforming to VIRTIO 1.0 or later are always LE. */
        return false;
    }
    return true;
#else
    return false;
#endif
}

static inline uint16_t virtio_lduw_phys(VirtIODevice *vdev, hwaddr pa)
{
    AddressSpace *dma_as = vdev->dma_as;

    return lduw_endian_phys(virtio_access_is_big_endian(vdev), dma_as, pa);
}

static inline uint32_t virtio_ldl_phys(VirtIODevice *vdev, hwaddr pa)
{
    AddressSpace *dma_as = vdev->dma_as;

    return ldl_endian_phys(virtio_access_is_big_endian(vdev), dma_as, pa);
}

static inline uint64_t virtio_ldq_phys(VirtIODevice *vdev, hwaddr pa)
{
    AddressSpace *dma_as = vdev->dma_as;

    return ldq_endian_phys(virtio_access_is_big_endian(vdev), dma_as, pa);
}

static inline void virtio_stw_phys(VirtIODevice *vdev, hwaddr pa,
                                   uint16_t value)
{
    AddressSpace *dma_as = vdev->dma_as;

    stw_endian_phys(virtio_access_is_big_endian(vdev), dma_as, pa, value);
}

static inline void virtio_stl_phys(VirtIODevice *vdev, hwaddr pa,
                                   uint32_t value)
{
    AddressSpace *dma_as = vdev->dma_as;

    stl_endian_phys(virtio_access_is_big_endian(vdev), dma_as, pa, value);
}

static inline void virtio_stw_p(VirtIODevice *vdev, void *ptr, uint16_t v)
{
    stw_endian_p(virtio_access_is_big_endian(vdev), ptr, v);
}

static inline void virtio_stl_p(VirtIODevice *vdev, void *ptr, uint32_t v)
{
    stl_endian_p(virtio_access_is_big_endian(vdev), ptr, v);
}

static inline void virtio_stq_p(VirtIODevice *vdev, void *ptr, uint64_t v)
{
    stq_endian_p(virtio_access_is_big_endian(vdev), ptr, v);
}

static inline int virtio_lduw_p(VirtIODevice *vdev, const void *ptr)
{
    return lduw_endian_p(virtio_access_is_big_endian(vdev), ptr);
}

static inline int virtio_ldl_p(VirtIODevice *vdev, const void *ptr)
{
    return ldl_endian_p(virtio_access_is_big_endian(vdev), ptr);
}

static inline uint64_t virtio_ldq_p(VirtIODevice *vdev, const void *ptr)
{
    return ldq_endian_p(virtio_access_is_big_endian(vdev), ptr);
}

static inline uint16_t virtio_tswap16(VirtIODevice *vdev, uint16_t s)
{
#if HOST_BIG_ENDIAN
    return virtio_access_is_big_endian(vdev) ? s : bswap16(s);
#else
    return virtio_access_is_big_endian(vdev) ? bswap16(s) : s;
#endif
}

static inline uint16_t virtio_lduw_phys_cached(VirtIODevice *vdev,
                                               MemoryRegionCache *cache,
                                               hwaddr pa)
{
    if (virtio_access_is_big_endian(vdev)) {
        return lduw_be_phys_cached(cache, pa);
    }
    return lduw_le_phys_cached(cache, pa);
}

static inline uint32_t virtio_ldl_phys_cached(VirtIODevice *vdev,
                                              MemoryRegionCache *cache,
                                              hwaddr pa)
{
    if (virtio_access_is_big_endian(vdev)) {
        return ldl_be_phys_cached(cache, pa);
    }
    return ldl_le_phys_cached(cache, pa);
}

static inline uint64_t virtio_ldq_phys_cached(VirtIODevice *vdev,
                                              MemoryRegionCache *cache,
                                              hwaddr pa)
{
    if (virtio_access_is_big_endian(vdev)) {
        return ldq_be_phys_cached(cache, pa);
    }
    return ldq_le_phys_cached(cache, pa);
}

static inline void virtio_stw_phys_cached(VirtIODevice *vdev,
                                          MemoryRegionCache *cache,
                                          hwaddr pa, uint16_t value)
{
    if (virtio_access_is_big_endian(vdev)) {
        stw_be_phys_cached(cache, pa, value);
    } else {
        stw_le_phys_cached(cache, pa, value);
    }
}

static inline void virtio_stl_phys_cached(VirtIODevice *vdev,
                                          MemoryRegionCache *cache,
                                          hwaddr pa, uint32_t value)
{
    if (virtio_access_is_big_endian(vdev)) {
        stl_be_phys_cached(cache, pa, value);
    } else {
        stl_le_phys_cached(cache, pa, value);
    }
}

static inline void virtio_tswap16s(VirtIODevice *vdev, uint16_t *s)
{
    *s = virtio_tswap16(vdev, *s);
}

static inline uint32_t virtio_tswap32(VirtIODevice *vdev, uint32_t s)
{
#if HOST_BIG_ENDIAN
    return virtio_access_is_big_endian(vdev) ? s : bswap32(s);
#else
    return virtio_access_is_big_endian(vdev) ? bswap32(s) : s;
#endif
}

static inline void virtio_tswap32s(VirtIODevice *vdev, uint32_t *s)
{
    *s = virtio_tswap32(vdev, *s);
}

static inline uint64_t virtio_tswap64(VirtIODevice *vdev, uint64_t s)
{
#if HOST_BIG_ENDIAN
    return virtio_access_is_big_endian(vdev) ? s : bswap64(s);
#else
    return virtio_access_is_big_endian(vdev) ? bswap64(s) : s;
#endif
}

static inline void virtio_tswap64s(VirtIODevice *vdev, uint64_t *s)
{
    *s = virtio_tswap64(vdev, *s);
}
#endif /* QEMU_VIRTIO_ACCESS_H */
