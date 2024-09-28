/*
 * QEMU Apple ParavirtualizedGraphics.framework device, PCI variant
 *
 * Copyright © 2023-2024 Phil Dennis-Jordan
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * ParavirtualizedGraphics.framework is a set of libraries that macOS provides
 * which implements 3d graphics passthrough to the host as well as a
 * proprietary guest communication channel to drive it. This device model
 * implements support to drive that library from within QEMU as a PCI device
 * aimed primarily at x86-64 macOS VMs.
 */

#include "apple-gfx.h"
#include "hw/pci/pci_device.h"
#include "hw/pci/msi.h"
#include "hw/qdev-properties.h"
#include "qapi/error.h"
#include "trace.h"
#import <ParavirtualizedGraphics/ParavirtualizedGraphics.h>

typedef struct AppleGFXPCIState {
    PCIDevice parent_obj;

    AppleGFXState common;
} AppleGFXPCIState;

OBJECT_DECLARE_SIMPLE_TYPE(AppleGFXPCIState, APPLE_GFX_PCI)

static const char* apple_gfx_pci_option_rom_path = NULL;

static void apple_gfx_init_option_rom_path(void)
{
    NSURL *option_rom_url = PGCopyOptionROMURL();
    const char *option_rom_path = option_rom_url.fileSystemRepresentation;
    if (option_rom_url.fileURL && option_rom_path != NULL) {
        apple_gfx_pci_option_rom_path = g_strdup(option_rom_path);
    }
    [option_rom_url release];
}

static void apple_gfx_pci_init(Object *obj)
{
    AppleGFXPCIState *s = APPLE_GFX_PCI(obj);

    if (!apple_gfx_pci_option_rom_path) {
        /* Done on device not class init to avoid -daemonize ObjC fork crash */
        PCIDeviceClass *pci = PCI_DEVICE_CLASS(object_get_class(obj));
        apple_gfx_init_option_rom_path();
        pci->romfile = apple_gfx_pci_option_rom_path;
    }

    apple_gfx_common_init(obj, &s->common, TYPE_APPLE_GFX_PCI);
}

static void apple_gfx_pci_interrupt(PCIDevice *dev, AppleGFXPCIState *s,
                                    uint32_t vector)
{
    bool msi_ok;
    trace_apple_gfx_raise_irq(vector);

    msi_ok = msi_enabled(dev);
    if (msi_ok) {
        msi_notify(dev, vector);
    }
}

static void apple_gfx_pci_realize(PCIDevice *dev, Error **errp)
{
    AppleGFXPCIState *s = APPLE_GFX_PCI(dev);
    Error *err = NULL;
    int ret;

    pci_register_bar(dev, PG_PCI_BAR_MMIO,
                     PCI_BASE_ADDRESS_SPACE_MEMORY, &s->common.iomem_gfx);

    ret = msi_init(dev, 0x0 /* config offset; 0 = find space */,
                   PG_PCI_MAX_MSI_VECTORS, true /* msi64bit */,
                   false /*msi_per_vector_mask*/, &err);
    if (ret != 0) {
        error_propagate(errp, err);
        return;
    }

    @autoreleasepool {
        PGDeviceDescriptor *desc = [PGDeviceDescriptor new];
        desc.raiseInterrupt = ^(uint32_t vector) {
            apple_gfx_pci_interrupt(dev, s, vector);
        };

        apple_gfx_common_realize(&s->common, desc);
        [desc release];
        desc = nil;
    }
}

static void apple_gfx_pci_reset(Object *obj, ResetType type)
{
    AppleGFXPCIState *s = APPLE_GFX_PCI(obj);
    [s->common.pgdev reset];
}

static void apple_gfx_pci_get_display_modes(Object *obj, Visitor *v,
                                            const char *name, void *opaque,
                                            Error **errp)
{
    Property *prop = opaque;
    AppleGFXDisplayModeList *mode_list = object_field_prop_ptr(obj, prop);

    apple_gfx_get_display_modes(mode_list, v, name, errp);
}

static void apple_gfx_pci_set_display_modes(Object *obj, Visitor *v,
                                            const char *name, void *opaque,
                                            Error **errp)
{
    Property *prop = opaque;
    AppleGFXDisplayModeList *mode_list = object_field_prop_ptr(obj, prop);

    apple_gfx_set_display_modes(mode_list, v, name, errp);
}

const PropertyInfo apple_gfx_pci_prop_display_modes = {
    .name  = "display_modes",
    .description =
        "Colon-separated list of display modes; "
        "<width>x<height>@<refresh-rate>; the first mode is considered "
        "'native'. Example: 3840x2160@60:2560x1440@60:1920x1080@60",
    .get   = apple_gfx_pci_get_display_modes,
    .set   = apple_gfx_pci_set_display_modes,
};

#define DEFINE_PROP_DISPLAY_MODES(_name, _state, _field) \
    DEFINE_PROP(_name, _state, _field, apple_gfx_pci_prop_display_modes, \
                AppleGFXDisplayModeList)

static Property apple_gfx_pci_properties[] = {
    DEFINE_PROP_DISPLAY_MODES("display-modes", AppleGFXPCIState,
                              common.display_modes),
    DEFINE_PROP_END_OF_LIST(),
};

static void apple_gfx_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *pci = PCI_DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    assert(rc->phases.hold == NULL);
    rc->phases.hold = apple_gfx_pci_reset;
    dc->desc = "macOS Paravirtualized Graphics PCI Display Controller";
    dc->hotpluggable = false;
    set_bit(DEVICE_CATEGORY_DISPLAY, dc->categories);

    pci->vendor_id = PG_PCI_VENDOR_ID;
    pci->device_id = PG_PCI_DEVICE_ID;
    pci->class_id = PCI_CLASS_DISPLAY_OTHER;
    pci->realize = apple_gfx_pci_realize;

    device_class_set_props(dc, apple_gfx_pci_properties);
}

static TypeInfo apple_gfx_pci_types[] = {
    {
        .name          = TYPE_APPLE_GFX_PCI,
        .parent        = TYPE_PCI_DEVICE,
        .instance_size = sizeof(AppleGFXPCIState),
        .class_init    = apple_gfx_pci_class_init,
        .instance_init = apple_gfx_pci_init,
        .interfaces = (InterfaceInfo[]) {
            { INTERFACE_PCIE_DEVICE },
            { },
        },
    }
};
DEFINE_TYPES(apple_gfx_pci_types)
