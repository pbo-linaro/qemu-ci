#!/usr/bin/env python3
#
# INTEL_IOMMU Functional tests
#
# Copyright (c) 2021 Red Hat, Inc.
#
# Author:
#  Eric Auger <eric.auger@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

from qemu_test import LinuxKernelTest, Asset, exec_command_and_wait_for_pattern

class IntelIOMMU(LinuxKernelTest):

    ASSET_KERNEL = Asset(
        ('https://archives.fedoraproject.org/pub/archive/fedora/linux/'
         'releases/39/Server/x86_64/os/images/pxeboot/vmlinuz'),
        '5f2ef0de47f8d79d5ee9bf8b0ee6d5ba4d987c2f9a16b8b511a7c69e53931fe3')

    ASSET_INITRD = Asset(
        ('https://archives.fedoraproject.org/pub/archive/fedora/linux/'
         'releases/39/Server/x86_64/os/images/pxeboot/initrd.img'),
        '5bc29e2d872ceeb39a9698d42da3fb0afd7583dc7180de05a6b78bcc726674bb')

    IOMMU_ADDON = ',iommu_platform=on,disable-modern=off,disable-legacy=on'
    default_kernel_params = 'console=ttyS0 rd.rescue quiet '
    kernel_path = None
    initrd_path = None
    kernel_params = None

    def add_common_args(self):
        self.vm.add_args('-device', 'virtio-rng-pci,rng=rng0')
        self.vm.add_args('-object',
                         'rng-random,id=rng0,filename=/dev/urandom')
        self.vm.add_args('-device', 'virtio-net-pci' + self.IOMMU_ADDON)
        self.vm.add_args('-device', 'virtio-gpu-pci' + self.IOMMU_ADDON)
        self.vm.add_args("-m", "1G")

    def common_vm_setup(self):
        self.set_machine('q35')
        self.require_accelerator("kvm")
        self.add_common_args()
        self.vm.add_args("-accel", "kvm")

        self.kernel_path = self.ASSET_KERNEL.fetch()
        self.initrd_path = self.ASSET_INITRD.fetch()
        self.kernel_params = self.default_kernel_params

    def run_and_check(self):
        if self.kernel_path:
            self.vm.add_args('-kernel', self.kernel_path,
                             '-append', self.kernel_params,
                             '-initrd', self.initrd_path)
        self.vm.set_console()
        self.vm.launch()
        self.wait_for_console_pattern('(or press Control-D to continue):')
        prompt = ':/root#'
        exec_command_and_wait_for_pattern(self, '', prompt)
        exec_command_and_wait_for_pattern(self, 'cat /proc/cmdline',
                                          'intel_iommu=on')
        self.wait_for_console_pattern(prompt)
        exec_command_and_wait_for_pattern(self, 'dmesg | grep DMAR:',
                                          'IOMMU enabled')
        self.wait_for_console_pattern(prompt)
        exec_command_and_wait_for_pattern(self,
                                    'find /sys/kernel/iommu_groups/ -type l',
                                    'devices/0000:00:')
        self.wait_for_console_pattern(prompt)

    def test_intel_iommu(self):
        self.common_vm_setup()
        self.vm.add_args('-device', 'intel-iommu,intremap=on')
        self.vm.add_args('-machine', 'kernel_irqchip=split')
        self.kernel_params += 'intel_iommu=on'
        self.run_and_check()

    def test_intel_iommu_strict(self):
        self.common_vm_setup()
        self.vm.add_args('-device', 'intel-iommu,intremap=on')
        self.vm.add_args('-machine', 'kernel_irqchip=split')
        self.kernel_params += 'intel_iommu=on,strict'
        self.run_and_check()

    def test_intel_iommu_strict_cm(self):
        self.common_vm_setup()
        self.vm.add_args('-device', 'intel-iommu,intremap=on,caching-mode=on')
        self.vm.add_args('-machine', 'kernel_irqchip=split')
        self.kernel_params += 'intel_iommu=on,strict'
        self.run_and_check()

    def test_intel_iommu_pt(self):
        self.common_vm_setup()
        self.vm.add_args('-device', 'intel-iommu,intremap=on')
        self.vm.add_args('-machine', 'kernel_irqchip=split')
        self.kernel_params += 'intel_iommu=on iommu=pt'
        self.run_and_check()

if __name__ == '__main__':
    LinuxKernelTest.main()
