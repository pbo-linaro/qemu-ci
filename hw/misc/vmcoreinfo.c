/*
 * Virtual Machine coreinfo device
 *
 * Copyright (C) 2017 Red Hat, Inc.
 *
 * Authors: Marc-André Lureau <marcandre.lureau@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "sysemu/reset.h"
#include "hw/nvram/fw_cfg.h"
#include "migration/vmstate.h"
#include "hw/misc/vmcoreinfo.h"
#include "qom/object_interfaces.h"

static const VMStateDescription vmstate_vmcoreinfo = {
    .name = "vmcoreinfo",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_BOOL(has_vmcoreinfo, VMCoreInfoState),
        VMSTATE_UINT16(vmcoreinfo.host_format, VMCoreInfoState),
        VMSTATE_UINT16(vmcoreinfo.guest_format, VMCoreInfoState),
        VMSTATE_UINT32(vmcoreinfo.size, VMCoreInfoState),
        VMSTATE_UINT64(vmcoreinfo.paddr, VMCoreInfoState),
        VMSTATE_END_OF_LIST()
    },
};

static char *vmcoreinfo_get_vmstate_id(VMStateIf *vmif)
{
    return g_strdup(TYPE_VMCOREINFO);
}

static void fw_cfg_vmci_write(void *opaque, off_t offset, size_t len)
{
    VMCoreInfoState *s = opaque;

    s->has_vmcoreinfo = offset == 0 && len == sizeof(s->vmcoreinfo)
        && s->vmcoreinfo.guest_format != FW_CFG_VMCOREINFO_FORMAT_NONE;
}

static void vmcoreinfo_reset_hold(Object *obj, ResetType type)
{
    VMCoreInfoState *s = (VMCoreInfoState *)obj;

    s->has_vmcoreinfo = false;
    memset(&s->vmcoreinfo, 0, sizeof(s->vmcoreinfo));
    s->vmcoreinfo.host_format = cpu_to_le16(FW_CFG_VMCOREINFO_FORMAT_ELF);
}

static void vmcoreinfo_complete(UserCreatable *uc, Error **errp)
{
    VMCoreInfoState *s = VMCOREINFO(uc);
    FWCfgState *fw_cfg = fw_cfg_find();
    /* for gdb script dump-guest-memory.py */
    static VMCoreInfoState * volatile vmcoreinfo_state G_GNUC_UNUSED;

    /*
     * Given that this function is executing, there is at least one
     * VMCOREINFO instance. Check if there are several.
     */
    if (!vmcoreinfo_find()) {
        error_setg(errp, "at most one %s instance is permitted",
                   TYPE_VMCOREINFO_DEVICE);
        return;
    }

    if (!fw_cfg || !fw_cfg->dma_enabled) {
        error_setg(errp, "%s device requires fw_cfg with DMA",
                   TYPE_VMCOREINFO_DEVICE);
        return;
    }

    if (vmstate_register_any(VMSTATE_IF(s), &vmstate_vmcoreinfo, s) < 0) {
        error_setg(errp, "%s: Failed to register vmstate", TYPE_VMCOREINFO);
    }

    fw_cfg_add_file_callback(fw_cfg, FW_CFG_VMCOREINFO_FILENAME,
                             NULL, fw_cfg_vmci_write, s,
                             &s->vmcoreinfo, sizeof(s->vmcoreinfo), false);

    /*
     * This device requires to register a global reset because it is
     * not plugged to a bus (which, as its QOM parent, would reset it).
     */
    qemu_register_resettable(OBJECT(s));
    vmcoreinfo_state = s;
}

static bool vmcoreinfo_can_be_deleted(UserCreatable *uc)
{
    return false;
}

static void vmcoreinfo_class_init(ObjectClass *oc, void *data)
{
    UserCreatableClass *ucc = USER_CREATABLE_CLASS(oc);
    VMStateIfClass *vc = VMSTATE_IF_CLASS(oc);
    ResettableClass *rc = RESETTABLE_CLASS(oc);

    ucc->complete = vmcoreinfo_complete;
    ucc->can_be_deleted = vmcoreinfo_can_be_deleted;
    vc->get_id = vmcoreinfo_get_vmstate_id;
    rc->phases.hold = vmcoreinfo_reset_hold;
}

static const TypeInfo vmcoreinfo_types[] = {
    {
        .name           = TYPE_VMCOREINFO,
        .parent         = TYPE_OBJECT,
        .instance_size  = sizeof(VMCoreInfoState),
        .class_init     = vmcoreinfo_class_init,
        .interfaces = (InterfaceInfo[]) {
            { TYPE_RESETTABLE_INTERFACE },
            { TYPE_USER_CREATABLE },
            { TYPE_VMSTATE_IF },
            { }
        }
    }
};

DEFINE_TYPES(vmcoreinfo_types)

VMCoreInfoState *vmcoreinfo_find(void)
{
    Object *obj;

    obj = object_resolve_path_type("", TYPE_VMCOREINFO, NULL);

    return obj ? (VMCoreInfoState *)obj : NULL;
}
