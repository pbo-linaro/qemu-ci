/*
 * Guest driven VM boot component update device
 * For details and specification, please look at docs/specs/vmfwupdate.rst.
 *
 * Copyright (C) 2025 Red Hat, Inc.
 *
 * Authors: Ani Sinha <anisinha@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "system/reset.h"
#include "hw/nvram/fw_cfg.h"
#include "hw/i386/pc.h"
#include "hw/qdev-properties.h"
#include "hw/misc/vmfwupdate.h"
#include "qemu/error-report.h"

/*
 * the following is the list of machines currently
 * supporting this device.
 * If a new machine is added in this list, the
 * corresponding vm/machine reset operations must also
 * be implemented. Please see pc_machine_reset() ->
 * handle_vmfwupd_reset() as an example. The reset
 * implementation must adhere to the device spec.
 */
static const char *supported_machines[] = {
    TYPE_X86_MACHINE,
    NULL,
};

static const char *vmfwupdate_supported(void)
{
    MachineState *ms = MACHINE(qdev_get_machine());
    const char **machine = supported_machines;
    while (*machine) {
        if (object_dynamic_cast(OBJECT(ms), *machine)) {
            return *machine;
        }
        machine++;
    }
    return NULL;
}

static uint64_t get_bios_size(void)
{
    Object *m_obj = qdev_get_machine();
    MachineState *ms = MACHINE(m_obj);
    X86MachineState *x86ms;

    if (object_dynamic_cast(OBJECT(ms), TYPE_X86_MACHINE)) {
        x86ms = X86_MACHINE(ms);
        /*
         * for pc machines, return the current size of the bios memory region.
         */
        return memory_region_size(&x86ms->bios);
    } else {
        /*
         * for other machine types and platforms, return 0 for now.
         * non-pc machines are currently not supported anyway.
         */
        return 0;
    }
}

static void fw_blob_write(void *dev, off_t offset, size_t len)
{
    VMFwUpdateState *s = VMFWUPDATE(dev);

    /* for non-pc platform, we do not allow changing bios_size yet */
    if (!s->plat_bios_size) {
        return;
    }

    /*
     * in order to change the bios size, appropriate capability
     * must be enabled
     */
    if (s->fw_blob.bios_size &&
        !(s->capability & VMFWUPDATE_CAP_BIOS_RESIZE)) {
        warn_report("vmfwupdate: VMFWUPDATE_CAP_BIOS_RESIZE not enabled");
        return;
    }

    /*
     * For now, we do not let the guest resize the bios size to a value
     * larger than the size of the memory region that holds the current image.
     * If the size is larger, we may have to reinitialize the bios
     * memory region. For pc, see x86_bios_rom_init().
     */
    if (s->fw_blob.bios_size > get_bios_size()) {
        warn_report("vmfwupdate: bios size cannot be larger than %" PRIu64,
                    get_bios_size());
        return;
    }

    s->plat_bios_size = s->fw_blob.bios_size;

    return;
}

static void vmfwupdate_realize(DeviceState *dev, Error **errp)
{
    VMFwUpdateState *s = VMFWUPDATE(dev);
    FWCfgState *fw_cfg = fw_cfg_find();

    /* multiple devices are not supported */
    if (!vmfwupdate_find()) {
        error_setg(errp, "at most one %s device is permitted",
                   TYPE_VMFWUPDATE);
        return;
    }

    /* if current machine is not supported, do not initialize */
    if (!vmfwupdate_supported()) {
        error_setg(errp, "This machine does not support vmfwupdate device");
        return;
    }

    /* fw_cfg with DMA support is necessary to support this device */
    if (!fw_cfg || !fw_cfg_dma_enabled(fw_cfg)) {
        error_setg(errp, "%s device requires fw_cfg",
                   TYPE_VMFWUPDATE);
        return;
    }

    /*
     * If the device is disabled on purpose, do not initialize.
     * Old machines like pc-i440fx-2.8 does not have enough fw-cfg slots
     * and hence this device is disabled for those machines.
     */
    if (s->disable) {
        info_report("vmfwupdate device is disabled on the command-line");
        return;
    }

    memset(&s->fw_blob, 0, sizeof(s->fw_blob));
    memset(&s->opaque_blobs, 0, sizeof(s->opaque_blobs));

    fw_cfg_add_file_callback(fw_cfg, FILE_VMFWUPDATE_OBLOB,
                             NULL, NULL, s,
                             &s->opaque_blobs,
                             sizeof(s->opaque_blobs),
                             false);

    fw_cfg_add_file_callback(fw_cfg, FILE_VMFWUPDATE_FWBLOB,
                             NULL, fw_blob_write, s,
                             &s->fw_blob,
                             sizeof(s->fw_blob),
                             false);

    /*
     * Add global capability fw_cfg file. This will be used by the guest to
     * check capability of the hypervisor.
     * We do not allow the guest to change bios size for now.
     */
    s->capability = cpu_to_le64(CAP_VMFWUPD_MASK | VMFWUPDATE_CAP_EDKROM);

    fw_cfg_add_file(fw_cfg, FILE_VMFWUPDATE_CAP,
                    &s->capability, sizeof(s->capability));

    s->plat_bios_size = get_bios_size(); /* for non-pc, this is 0 */
    /* size of bios region for the platform - read only by the guest */
    fw_cfg_add_file(fw_cfg, FILE_VMFWUPDATE_BIOS_SIZE,
                    &s->plat_bios_size, sizeof(s->plat_bios_size));
    /*
     * add fw cfg control file to disable the hypervisor interface.
     */
    fw_cfg_add_file_callback(fw_cfg, FILE_VMFWUPDATE_CONTROL,
                             NULL, NULL, s,
                             &s->disable,
                             sizeof(s->disable),
                             false);
    /*
     * This device requires to register a global reset because it is
     * not plugged to a bus (which, as its QOM parent, would reset it).
     */
    qemu_register_resettable(OBJECT(s));
}

static const Property vmfwupdate_properties[] = {
    DEFINE_PROP_UINT8("disable", VMFwUpdateState, disable, 0),
};

static void vmfwupdate_device_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    /* we are not interested in migration - so no need to populate dc->vmsd */
    dc->desc = "VM firmware update device";
    dc->realize = vmfwupdate_realize;
    dc->hotpluggable = false;
    device_class_set_props(dc, vmfwupdate_properties);
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo vmfwupdate_device_types[] = {
    {
        .name          = TYPE_VMFWUPDATE,
        .parent        = TYPE_DEVICE,
        .instance_size = sizeof(VMFwUpdateState),
        .class_init    = vmfwupdate_device_class_init,
    },
};

DEFINE_TYPES(vmfwupdate_device_types)
