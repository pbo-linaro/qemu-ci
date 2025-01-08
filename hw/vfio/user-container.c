/*
 * Container for vfio-user IOMMU type: rather than communicating with the kernel
 * vfio driver, we communicate over a socket to a server using the vfio-user
 * protocol.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include <sys/ioctl.h>
#include <linux/vfio.h>

#include "hw/vfio/vfio-common.h"
#include "hw/vfio/user.h"
#include "exec/address-spaces.h"
#include "exec/memory.h"
#include "exec/ram_addr.h"
#include "hw/hw.h"
#include "qemu/error-report.h"
#include "qemu/range.h"
#include "trace.h"
#include "qapi/error.h"
#include "pci.h"

/*
 * When DMA space is the physical address space, the region add/del listeners
 * will fire during memory update transactions.  These depend on BQL being held,
 * so do any resulting map/demap ops async while keeping BQL.
 */
static void vfio_user_listener_begin(VFIOContainerBase *bcontainer)
{
    VFIOUserContainer *container = container_of(bcontainer, VFIOUserContainer,
                                                 bcontainer);

    container->proxy->async_ops = true;
}

static void vfio_user_listener_commit(VFIOContainerBase *bcontainer)
{
    VFIOUserContainer *container = container_of(bcontainer, VFIOUserContainer,
                                            bcontainer);

    /* wait here for any async requests sent during the transaction */
    container->proxy->async_ops = false;
    vfio_user_wait_reqs(container->proxy);
}

static int vfio_user_dma_unmap(const VFIOContainerBase *bcontainer,
                               hwaddr iova, ram_addr_t size,
                               IOMMUTLBEntry *iotlb, int flags)
{
    VFIOUserContainer *container = container_of(bcontainer, VFIOUserContainer,
                                            bcontainer);

    VFIOUserDMAUnmap *msgp = g_malloc(sizeof(*msgp));

    vfio_user_request_msg(&msgp->hdr, VFIO_USER_DMA_UNMAP, sizeof(*msgp), 0);
    msgp->argsz = sizeof(struct vfio_iommu_type1_dma_unmap);
    msgp->flags = flags;
    msgp->iova = iova;
    msgp->size = size;
    trace_vfio_user_dma_unmap(msgp->iova, msgp->size, msgp->flags,
                              container->proxy->async_ops);

    if (container->proxy->async_ops) {
        vfio_user_send_nowait(container->proxy, &msgp->hdr, NULL, 0);
        return 0;
    }

    vfio_user_send_wait(container->proxy, &msgp->hdr, NULL, 0);
    if (msgp->hdr.flags & VFIO_USER_ERROR) {
        return -msgp->hdr.error_reply;
    }

    g_free(msgp);
    return 0;
}

static int vfio_user_dma_map(const VFIOContainerBase *bcontainer, hwaddr iova,
                             ram_addr_t size, void *vaddr, bool readonly,
                             MemoryRegion *mrp)
{
    VFIOUserContainer *container = container_of(bcontainer, VFIOUserContainer,
                                                bcontainer);

    VFIOUserProxy *proxy = container->proxy;
    int fd = memory_region_get_fd(mrp);
    int ret;

    VFIOUserFDs *fds = NULL;
    VFIOUserDMAMap *msgp = g_malloc0(sizeof(*msgp));

    vfio_user_request_msg(&msgp->hdr, VFIO_USER_DMA_MAP, sizeof(*msgp), 0);
    msgp->argsz = sizeof(struct vfio_iommu_type1_dma_map);
    msgp->flags = VFIO_DMA_MAP_FLAG_READ;
    msgp->offset = 0;
    msgp->iova = iova;
    msgp->size = size;

    /*
     * vaddr enters as a QEMU process address; make it either a file offset
     * for mapped areas or leave as 0.
     */
    if (fd != -1 && !(container->proxy->flags & VFIO_PROXY_NO_MMAP)) {
        msgp->offset = qemu_ram_block_host_offset(mrp->ram_block, vaddr);
    }

    if (!readonly) {
        msgp->flags |= VFIO_DMA_MAP_FLAG_WRITE;
    }

    trace_vfio_user_dma_map(msgp->iova, msgp->size, msgp->offset, msgp->flags,
                            container->proxy->async_ops);

    /*
     * The async_ops case sends without blocking or dropping BQL.
     * They're later waited for in vfio_send_wait_reqs.
     */
    if (container->proxy->async_ops) {
        /* can't use auto variable since we don't block */
        if (fd != -1) {
            fds = vfio_user_getfds(1);
            fds->send_fds = 1;
            fds->fds[0] = fd;
        }
        vfio_user_send_nowait(proxy, &msgp->hdr, fds, 0);
        ret = 0;
    } else {
        VFIOUserFDs local_fds = { 1, 0, &fd };

        fds = fd != -1 ? &local_fds : NULL;
        vfio_user_send_wait(proxy, &msgp->hdr, fds, 0);
        ret = (msgp->hdr.flags & VFIO_USER_ERROR) ? -msgp->hdr.error_reply : 0;
        g_free(msgp);
    }

    return ret;
}

static int
vfio_user_set_dirty_page_tracking(const VFIOContainerBase *bcontainer,
                                    bool start, Error **errp)
{
    error_setg_errno(errp, ENOTSUP, "Not supported");
    return -ENOTSUP;
}

static int vfio_user_query_dirty_bitmap(const VFIOContainerBase *bcontainer,
                                         VFIOBitmap *vbmap, hwaddr iova,
                                         hwaddr size, Error **errp)
{
    error_setg_errno(errp, ENOTSUP, "Not supported");
    return -ENOTSUP;
}

static bool vfio_user_setup(VFIOContainerBase *bcontainer, Error **errp)
{
    VFIOUserContainer *container = container_of(bcontainer, VFIOUserContainer,
                                                bcontainer);

    assert(container->proxy->dma_pgsizes != 0);
    bcontainer->pgsizes = container->proxy->dma_pgsizes;
    bcontainer->dma_max_mappings = container->proxy->max_dma;

    /* No live migration support yet. */
    bcontainer->dirty_pages_supported = false;
    bcontainer->max_dirty_bitmap_size = container->proxy->max_bitmap;
    bcontainer->dirty_pgsizes = container->proxy->migr_pgsize;

    return true;
}

static VFIOUserContainer *vfio_create_user_container(VFIODevice *vbasedev,
                                                     Error **errp)
{
    VFIOUserContainer *container;

    container = VFIO_IOMMU_USER(object_new(TYPE_VFIO_IOMMU_USER));
    container->proxy = vbasedev->proxy;
    return container;
}

/*
 * Try to mirror vfio_connect_container() as much as possible.
 */
static VFIOUserContainer *
vfio_connect_user_container(AddressSpace *as, VFIODevice *vbasedev,
                            Error **errp)
{
    VFIOContainerBase *bcontainer;
    VFIOUserContainer *container;
    VFIOAddressSpace *space;
    VFIOIOMMUClass *vioc;
    int ret;

    space = vfio_get_address_space(as);

    container = vfio_create_user_container(vbasedev, errp);
    if (!container) {
        goto put_space_exit;
    }

    bcontainer = &container->bcontainer;

    if (!vfio_cpr_register_container(bcontainer, errp)) {
        goto free_container_exit;
    }

    ret = ram_block_uncoordinated_discard_disable(true);
    if (ret) {
        error_setg_errno(errp, -ret, "Cannot set discarding of RAM broken");
        goto unregister_container_exit;
    }

    vioc = VFIO_IOMMU_GET_CLASS(bcontainer);
    assert(vioc->setup);

    if (!vioc->setup(bcontainer, errp)) {
        goto enable_discards_exit;
    }

    vfio_address_space_insert(space, bcontainer);

    bcontainer->listener = vfio_memory_listener;
    memory_listener_register(&bcontainer->listener, bcontainer->space->as);

    if (bcontainer->error) {
        errno = EINVAL;
        error_propagate_prepend(errp, bcontainer->error,
            "memory listener initialization failed: ");
        goto listener_release_exit;
    }

    bcontainer->initialized = true;

    return container;

listener_release_exit:
    memory_listener_unregister(&bcontainer->listener);
    if (vioc->release) {
        vioc->release(bcontainer);
    }

enable_discards_exit:
    ram_block_uncoordinated_discard_disable(false);

unregister_container_exit:
    vfio_cpr_unregister_container(bcontainer);

free_container_exit:
    object_unref(container);

put_space_exit:
    vfio_put_address_space(space);

    return NULL;
}

static void vfio_disconnect_user_container(VFIOUserContainer *container)
{
    VFIOContainerBase *bcontainer = &container->bcontainer;
    VFIOIOMMUClass *vioc = VFIO_IOMMU_GET_CLASS(bcontainer);
    VFIOAddressSpace *space = bcontainer->space;

    ram_block_uncoordinated_discard_disable(false);

    memory_listener_unregister(&bcontainer->listener);
    if (vioc->release) {
        vioc->release(bcontainer);
    }

    vfio_cpr_unregister_container(bcontainer);
    object_unref(container);

    vfio_put_address_space(space);
}

static bool vfio_user_get_device(VFIOUserContainer *container,
                                 VFIODevice *vbasedev, Error **errp)
{
    struct vfio_device_info info = { .argsz = sizeof(info) };
    int ret;

    ret = vfio_user_get_info(vbasedev->proxy, &info);
    if (ret) {
        error_setg_errno(errp, -ret, "get info failure");
        return ret;
    }

    vbasedev->fd = -1;

    vfio_prepare_device(vbasedev, &container->bcontainer, NULL, &info);

    return true;
}

/*
 * vfio_user_attach_device: attach a device to a new container.
 */
static bool vfio_user_attach_device(const char *name, VFIODevice *vbasedev,
                                    AddressSpace *as, Error **errp)
{
    VFIOUserContainer *container;

    container = vfio_connect_user_container(as, vbasedev, errp);
    if (container == NULL) {
        error_prepend(errp, "failed to connect proxy");
        return false;
    }

    return vfio_user_get_device(container, vbasedev, errp);
}

static void vfio_user_detach_device(VFIODevice *vbasedev)
{
    VFIOUserContainer *container = container_of(vbasedev->bcontainer,
                                                VFIOUserContainer, bcontainer);

    QLIST_REMOVE(vbasedev, global_next);
    QLIST_REMOVE(vbasedev, container_next);
    vbasedev->bcontainer = NULL;
    vfio_put_base_device(vbasedev);
    vfio_disconnect_user_container(container);
}

static int vfio_user_pci_hot_reset(VFIODevice *vbasedev, bool single)
{
    /* ->needs_reset is always false for vfio-user. */
    return 0;
}

static void vfio_iommu_user_class_init(ObjectClass *klass, void *data)
{
    VFIOIOMMUClass *vioc = VFIO_IOMMU_CLASS(klass);

    vioc->setup = vfio_user_setup;
    vioc->listener_begin = vfio_user_listener_begin,
    vioc->listener_commit = vfio_user_listener_commit,
    vioc->dma_map = vfio_user_dma_map;
    vioc->dma_unmap = vfio_user_dma_unmap;
    vioc->attach_device = vfio_user_attach_device;
    vioc->detach_device = vfio_user_detach_device;
    vioc->set_dirty_page_tracking = vfio_user_set_dirty_page_tracking;
    vioc->query_dirty_bitmap = vfio_user_query_dirty_bitmap;
    vioc->pci_hot_reset = vfio_user_pci_hot_reset;
};

static const TypeInfo types[] = {
    {
        .name = TYPE_VFIO_IOMMU_USER,
        .parent = TYPE_VFIO_IOMMU,
        .instance_size = sizeof(VFIOUserContainer),
        .class_init = vfio_iommu_user_class_init,
    },
};

DEFINE_TYPES(types)
