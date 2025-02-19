/*
 * Container for vfio-user IOMMU type: rather than communicating with the kernel
 * vfio driver, we communicate over a socket to a server using the vfio-user
 * protocol.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#include <sys/ioctl.h>
#include <linux/vfio.h>
#include "qemu/osdep.h"

#include "exec/address-spaces.h"
#include "exec/memory.h"
#include "exec/ram_addr.h"
#include "hw/hw.h"
#include "hw/vfio/pci.h"
#include "hw/vfio-user/container.h"
#include "qemu/error-report.h"
#include "qemu/range.h"
#include "qapi/error.h"
#include "trace.h"

static int vfio_user_dma_unmap(const VFIOContainerBase *bcontainer,
                               hwaddr iova, ram_addr_t size,
                               IOMMUTLBEntry *iotlb, int flags)
{
    return -ENOTSUP;
}

static int vfio_user_dma_map(const VFIOContainerBase *bcontainer, hwaddr iova,
                             ram_addr_t size, void *vaddr, bool readonly,
                             MemoryRegion *mrp)
{
    return -ENOTSUP;
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
    error_setg_errno(errp, ENOTSUP, "Not supported");
    return -ENOTSUP;
}

static VFIOUserContainer *vfio_create_user_container(Error **errp)
{
    VFIOUserContainer *container;

    container = VFIO_IOMMU_USER(object_new(TYPE_VFIO_IOMMU_USER));
    return container;
}

/*
 * Try to mirror vfio_connect_container() as much as possible.
 */
static VFIOUserContainer *
vfio_connect_user_container(AddressSpace *as, Error **errp)
{
    VFIOContainerBase *bcontainer;
    VFIOUserContainer *container;
    VFIOAddressSpace *space;
    VFIOIOMMUClass *vioc;

    space = vfio_get_address_space(as);

    container = vfio_create_user_container(errp);
    if (!container) {
        goto put_space_exit;
    }

    bcontainer = &container->bcontainer;

    if (!vfio_cpr_register_container(bcontainer, errp)) {
        goto free_container_exit;
    }

    vioc = VFIO_IOMMU_GET_CLASS(bcontainer);
    assert(vioc->setup);

    if (!vioc->setup(bcontainer, errp)) {
        goto unregister_container_exit;
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

    memory_listener_unregister(&bcontainer->listener);
    if (vioc->release) {
        vioc->release(bcontainer);
    }

    VFIOAddressSpace *space = bcontainer->space;

    vfio_cpr_unregister_container(bcontainer);
    object_unref(container);

    vfio_put_address_space(space);
}

static bool vfio_user_get_device(VFIOUserContainer *container,
                                 VFIODevice *vbasedev, Error **errp)
{
    struct vfio_device_info info = { 0 };

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

    container = vfio_connect_user_container(as, errp);
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
