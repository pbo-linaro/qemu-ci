/*
 * IGD device quirk stubs
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright (C) Linaro, Ltd.
 */

#include "qemu/osdep.h"
#include "pci.h"

void vfio_probe_igd_bar0_quirk(VFIOPCIDevice *vdev, int nr)
{
    g_assert_not_reached();
}

void vfio_probe_igd_bar4_quirk(VFIOPCIDevice *vdev, int nr)
{
    g_assert_not_reached();
}
