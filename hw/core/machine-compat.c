/*
 * QEMU Machine compatibility properties
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/qdev-core.h"
#include "hw/pci/pci_bridge.h"
#include "hw/virtio/virtio-pci.h"
#include "hw/virtio/virtio-net.h"
#include "hw/virtio/virtio-iommu.h"

GlobalProperty hw_compat_9_1[] = {
};
const size_t hw_compat_9_1_len = G_N_ELEMENTS(hw_compat_9_1);

GlobalProperty hw_compat_9_0[] = {
    { "arm-cpu", "backcompat-cntfrq", "true" },
    { "scsi-hd", "migrate-emulated-scsi-request", "false" },
    { "scsi-cd", "migrate-emulated-scsi-request", "false" },
    { "vfio-pci", "skip-vsc-check", "false" },
    { "virtio-pci", "x-pcie-pm-no-soft-reset", "off" },
    { "sd-card", "spec_version", "2" },
};
const size_t hw_compat_9_0_len = G_N_ELEMENTS(hw_compat_9_0);

GlobalProperty hw_compat_8_2[] = {
    { "migration", "zero-page-detection", "legacy"},
    { TYPE_VIRTIO_IOMMU_PCI, "granule", "4k" },
    { TYPE_VIRTIO_IOMMU_PCI, "aw-bits", "64" },
    { "virtio-gpu-device", "x-scanout-vmstate-version", "1" },
};
const size_t hw_compat_8_2_len = G_N_ELEMENTS(hw_compat_8_2);

GlobalProperty hw_compat_8_1[] = {
    { TYPE_PCI_BRIDGE, "x-pci-express-writeable-slt-bug", "true" },
    { "ramfb", "x-migrate", "off" },
    { "vfio-pci-nohotplug", "x-ramfb-migrate", "off" },
    { "igb", "x-pcie-flr-init", "off" },
    { TYPE_VIRTIO_NET, "host_uso", "off"},
    { TYPE_VIRTIO_NET, "guest_uso4", "off"},
    { TYPE_VIRTIO_NET, "guest_uso6", "off"},
};
const size_t hw_compat_8_1_len = G_N_ELEMENTS(hw_compat_8_1);

GlobalProperty hw_compat_8_0[] = {
    { "migration", "multifd-flush-after-each-section", "on"},
    { TYPE_PCI_DEVICE, "x-pcie-ari-nextfn-1", "on" },
};
const size_t hw_compat_8_0_len = G_N_ELEMENTS(hw_compat_8_0);

GlobalProperty hw_compat_7_2[] = {
    { "e1000e", "migrate-timadj", "off" },
    { "virtio-mem", "x-early-migration", "false" },
    { "migration", "x-preempt-pre-7-2", "true" },
    { TYPE_PCI_DEVICE, "x-pcie-err-unc-mask", "off" },
};
const size_t hw_compat_7_2_len = G_N_ELEMENTS(hw_compat_7_2);

GlobalProperty hw_compat_7_1[] = {
    { "virtio-device", "queue_reset", "false" },
    { "virtio-rng-pci", "vectors", "0" },
    { "virtio-rng-pci-transitional", "vectors", "0" },
    { "virtio-rng-pci-non-transitional", "vectors", "0" },
};
const size_t hw_compat_7_1_len = G_N_ELEMENTS(hw_compat_7_1);

GlobalProperty hw_compat_7_0[] = {
    { "arm-gicv3-common", "force-8-bit-prio", "on" },
    { "nvme-ns", "eui64-default", "on"},
};
const size_t hw_compat_7_0_len = G_N_ELEMENTS(hw_compat_7_0);

GlobalProperty hw_compat_6_2[] = {
    { "PIIX4_PM", "x-not-migrate-acpi-index", "on"},
};
const size_t hw_compat_6_2_len = G_N_ELEMENTS(hw_compat_6_2);

GlobalProperty hw_compat_6_1[] = {
    { "vhost-user-vsock-device", "seqpacket", "off" },
    { "nvme-ns", "shared", "off" },
};
const size_t hw_compat_6_1_len = G_N_ELEMENTS(hw_compat_6_1);

GlobalProperty hw_compat_6_0[] = {
    { "gpex-pcihost", "allow-unmapped-accesses", "false" },
    { "i8042", "extended-state", "false"},
    { "nvme-ns", "eui64-default", "off"},
    { "e1000", "init-vet", "off" },
    { "e1000e", "init-vet", "off" },
    { "vhost-vsock-device", "seqpacket", "off" },
};
const size_t hw_compat_6_0_len = G_N_ELEMENTS(hw_compat_6_0);

GlobalProperty hw_compat_5_2[] = {
    { "ICH9-LPC", "smm-compat", "on"},
    { "PIIX4_PM", "smm-compat", "on"},
    { "virtio-blk-device", "report-discard-granularity", "off" },
    { "virtio-net-pci-base", "vectors", "3"},
    { "nvme", "msix-exclusive-bar", "on"},
};
const size_t hw_compat_5_2_len = G_N_ELEMENTS(hw_compat_5_2);

GlobalProperty hw_compat_5_1[] = {
    { "vhost-scsi", "num_queues", "1"},
    { "vhost-user-blk", "num-queues", "1"},
    { "vhost-user-scsi", "num_queues", "1"},
    { "virtio-blk-device", "num-queues", "1"},
    { "virtio-scsi-device", "num_queues", "1"},
    { "nvme", "use-intel-id", "on"},
    { "pvpanic", "events", "1"}, /* PVPANIC_PANICKED */
    { "pl011", "migrate-clk", "off" },
    { "virtio-pci", "x-ats-page-aligned", "off"},
};
const size_t hw_compat_5_1_len = G_N_ELEMENTS(hw_compat_5_1);

GlobalProperty hw_compat_5_0[] = {
    { "pci-host-bridge", "x-config-reg-migration-enabled", "off" },
    { "virtio-balloon-device", "page-poison", "false" },
    { "vmport", "x-read-set-eax", "off" },
    { "vmport", "x-signal-unsupported-cmd", "off" },
    { "vmport", "x-report-vmx-type", "off" },
    { "vmport", "x-cmds-v2", "off" },
    { "virtio-device", "x-disable-legacy-check", "true" },
};
const size_t hw_compat_5_0_len = G_N_ELEMENTS(hw_compat_5_0);

GlobalProperty hw_compat_4_2[] = {
    { "virtio-blk-device", "queue-size", "128"},
    { "virtio-scsi-device", "virtqueue_size", "128"},
    { "virtio-blk-device", "x-enable-wce-if-config-wce", "off" },
    { "virtio-blk-device", "seg-max-adjust", "off"},
    { "virtio-scsi-device", "seg_max_adjust", "off"},
    { "vhost-blk-device", "seg_max_adjust", "off"},
    { "usb-host", "suppress-remote-wake", "off" },
    { "usb-redir", "suppress-remote-wake", "off" },
    { "qxl", "revision", "4" },
    { "qxl-vga", "revision", "4" },
    { "fw_cfg", "acpi-mr-restore", "false" },
    { "virtio-device", "use-disabled-flag", "false" },
};
const size_t hw_compat_4_2_len = G_N_ELEMENTS(hw_compat_4_2);

GlobalProperty hw_compat_4_1[] = {
    { "virtio-pci", "x-pcie-flr-init", "off" },
};
const size_t hw_compat_4_1_len = G_N_ELEMENTS(hw_compat_4_1);

GlobalProperty hw_compat_4_0[] = {
    { "VGA",            "edid", "false" },
    { "secondary-vga",  "edid", "false" },
    { "bochs-display",  "edid", "false" },
    { "virtio-vga",     "edid", "false" },
    { "virtio-gpu-device", "edid", "false" },
    { "virtio-device", "use-started", "false" },
    { "virtio-balloon-device", "qemu-4-0-config-size", "true" },
    { "pl031", "migrate-tick-offset", "false" },
};
const size_t hw_compat_4_0_len = G_N_ELEMENTS(hw_compat_4_0);

GlobalProperty hw_compat_3_1[] = {
    { "pcie-root-port", "x-speed", "2_5" },
    { "pcie-root-port", "x-width", "1" },
    { "memory-backend-file", "x-use-canonical-path-for-ramblock-id", "true" },
    { "memory-backend-memfd", "x-use-canonical-path-for-ramblock-id", "true" },
    { "tpm-crb", "ppi", "false" },
    { "tpm-tis", "ppi", "false" },
    { "usb-kbd", "serial", "42" },
    { "usb-mouse", "serial", "42" },
    { "usb-tablet", "serial", "42" },
    { "virtio-blk-device", "discard", "false" },
    { "virtio-blk-device", "write-zeroes", "false" },
    { "virtio-balloon-device", "qemu-4-0-config-size", "false" },
    { "pcie-root-port-base", "disable-acs", "true" }, /* Added in 4.1 */
};
const size_t hw_compat_3_1_len = G_N_ELEMENTS(hw_compat_3_1);

GlobalProperty hw_compat_3_0[] = {};
const size_t hw_compat_3_0_len = G_N_ELEMENTS(hw_compat_3_0);

GlobalProperty hw_compat_2_12[] = {
    { "hda-audio", "use-timer", "false" },
    { "cirrus-vga", "global-vmstate", "true" },
    { "VGA", "global-vmstate", "true" },
    { "vmware-svga", "global-vmstate", "true" },
    { "qxl-vga", "global-vmstate", "true" },
};
const size_t hw_compat_2_12_len = G_N_ELEMENTS(hw_compat_2_12);

GlobalProperty hw_compat_2_11[] = {
    { "hpet", "hpet-offset-saved", "false" },
    { "virtio-blk-pci", "vectors", "2" },
    { "vhost-user-blk-pci", "vectors", "2" },
    { "e1000", "migrate_tso_props", "off" },
};
const size_t hw_compat_2_11_len = G_N_ELEMENTS(hw_compat_2_11);

GlobalProperty hw_compat_2_10[] = {
    { "virtio-mouse-device", "wheel-axis", "false" },
    { "virtio-tablet-device", "wheel-axis", "false" },
};
const size_t hw_compat_2_10_len = G_N_ELEMENTS(hw_compat_2_10);

GlobalProperty hw_compat_2_9[] = {
    { "pci-bridge", "shpc", "off" },
    { "intel-iommu", "pt", "off" },
    { "virtio-net-device", "x-mtu-bypass-backend", "off" },
    { "pcie-root-port", "x-migrate-msix", "false" },
};
const size_t hw_compat_2_9_len = G_N_ELEMENTS(hw_compat_2_9);

GlobalProperty hw_compat_2_8[] = {
    { "fw_cfg_mem", "x-file-slots", "0x10" },
    { "fw_cfg_io", "x-file-slots", "0x10" },
    { "pflash_cfi01", "old-multiple-chip-handling", "on" },
    { "pci-bridge", "shpc", "on" },
    { TYPE_PCI_DEVICE, "x-pcie-extcap-init", "off" },
    { "virtio-pci", "x-pcie-deverr-init", "off" },
    { "virtio-pci", "x-pcie-lnkctl-init", "off" },
    { "virtio-pci", "x-pcie-pm-init", "off" },
    { "cirrus-vga", "vgamem_mb", "8" },
    { "isa-cirrus-vga", "vgamem_mb", "8" },
};
const size_t hw_compat_2_8_len = G_N_ELEMENTS(hw_compat_2_8);

GlobalProperty hw_compat_2_7[] = {
    { "virtio-pci", "page-per-vq", "on" },
    { "virtio-serial-device", "emergency-write", "off" },
    { "ioapic", "version", "0x11" },
    { "intel-iommu", "x-buggy-eim", "true" },
    { "virtio-pci", "x-ignore-backend-features", "on" },
};
const size_t hw_compat_2_7_len = G_N_ELEMENTS(hw_compat_2_7);

GlobalProperty hw_compat_2_6[] = {
    { "virtio-mmio", "format_transport_address", "off" },
    /* Optional because not all virtio-pci devices support legacy mode */
    { "virtio-pci", "disable-modern", "on",  .optional = true },
    { "virtio-pci", "disable-legacy", "off", .optional = true },
};
const size_t hw_compat_2_6_len = G_N_ELEMENTS(hw_compat_2_6);

GlobalProperty hw_compat_2_5[] = {
    { "isa-fdc", "fallback", "144" },
    { "pvscsi", "x-old-pci-configuration", "on" },
    { "pvscsi", "x-disable-pcie", "on" },
    { "vmxnet3", "x-old-msi-offsets", "on" },
    { "vmxnet3", "x-disable-pcie", "on" },
};
const size_t hw_compat_2_5_len = G_N_ELEMENTS(hw_compat_2_5);

GlobalProperty hw_compat_2_4[] = {
    { "e1000", "extra_mac_registers", "off" },
    { "virtio-pci", "x-disable-pcie", "on" },
    { "virtio-pci", "migrate-extra", "off" },
    { "fw_cfg_mem", "dma_enabled", "off" },
    { "fw_cfg_io", "dma_enabled", "off" }
};
const size_t hw_compat_2_4_len = G_N_ELEMENTS(hw_compat_2_4);

GlobalProperty hw_compat_2_3[] = {
    { "virtio-blk-pci", "any_layout", "off" },
    { "virtio-balloon-pci", "any_layout", "off" },
    { "virtio-serial-pci", "any_layout", "off" },
    { "virtio-9p-pci", "any_layout", "off" },
    { "virtio-rng-pci", "any_layout", "off" },
    { TYPE_PCI_DEVICE, "x-pcie-lnksta-dllla", "off" },
    { "migration", "send-configuration", "off" },
    { "migration", "send-section-footer", "off" },
    { "migration", "store-global-state", "off" },
};
const size_t hw_compat_2_3_len = G_N_ELEMENTS(hw_compat_2_3);

GlobalProperty hw_compat_2_2[] = {};
const size_t hw_compat_2_2_len = G_N_ELEMENTS(hw_compat_2_2);

GlobalProperty hw_compat_2_1[] = {
    { "intel-hda", "old_msi_addr", "on" },
    { "VGA", "qemu-extended-regs", "off" },
    { "secondary-vga", "qemu-extended-regs", "off" },
    { "virtio-scsi-pci", "any_layout", "off" },
    { "usb-mouse", "usb_version", "1" },
    { "usb-kbd", "usb_version", "1" },
    { "virtio-pci", "virtio-pci-bus-master-bug-migration", "on" },
};
const size_t hw_compat_2_1_len = G_N_ELEMENTS(hw_compat_2_1);
