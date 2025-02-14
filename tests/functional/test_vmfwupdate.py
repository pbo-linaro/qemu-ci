#!/usr/bin/env python3
#
# Check for vmfwupdate device.
#
# Copyright (c) 2025 Red Hat, Inc.
#
# Author:
#  Ani Sinha <anisinha@redhat.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import QemuSystemTest
import time

class VmFwUpdateDeviceCheck(QemuSystemTest):
    DELAY_BOOT_SEQUENCE = 1

    def test_vmfwupdate_pass(self):
        """
        Basic test to make sure vmfwupdate device can be instantiated.
        """
        if self.arch != 'x86_64':
            return

        self.vm.add_args('-device', 'vmfwupdate,id=fwupd1')
        self.vm.set_qmp_monitor(enabled=False)
        self.vm.launch()
        time.sleep(self.DELAY_BOOT_SEQUENCE)
        self.vm.shutdown()
        self.assertEqual(self.vm.exitcode(), 0, "QEMU exit code should be 0")

    def test_vmfwupdate_disabled(self):
        """
        Basic test to make sure vmfwupdate device can be instantiated.
        """
        if self.arch != 'x86_64':
            return

        self.vm.add_args('-device', 'vmfwupdate,id=fwupd,disable=1')
        self.vm.set_qmp_monitor(enabled=False)
        self.vm.launch()
        time.sleep(self.DELAY_BOOT_SEQUENCE)
        self.vm.shutdown()
        self.assertRegex(self.vm.get_log(),
                         r'vmfwupdate device is disabled on the command-line')
        self.assertEqual(self.vm.exitcode(), 0, "QEMU exit code should be 0")

    def test_multiple_device_fail(self):
        """
        Only one vmfwdevice can be instantiated. Ensure failure if
        user tries to create more than one device.
        """
        if self.arch != 'x86_64':
            return

        self.vm.add_args('-device', 'vmfwupdate,id=fw1',
                         '-device', 'vmfwupdate,id=fw2')
        self.vm.set_qmp_monitor(enabled=False)
        self.vm.launch()
        self.vm.wait()
        self.assertEqual(self.vm.exitcode(), 1, "QEMU exit code should be 1")
        self.assertRegex(self.vm.get_log(),
                         r'at most one vmfwupdate device is permitted')

    def aarch64_fail_test(self):
        """
        Currently the device is only supported for pc platforms.
        """
        if self.arch != 'aarch64':
            return

        self.vm.add_args('-machine', 'virt', '-device',
                         'vmfwupdate,id=fwupd1')
        self.vm.set_qmp_monitor(enabled=False)
        self.vm.launch()
        self.vm.wait()
        self.assertEqual(self.vm.exitcode(), 1, "QEMU exit code should be 1")
        self.assertRegex(self.vm.get_log(),
                         r'This machine does not support vmfwupdate device')

if __name__ == '__main__':
    QemuSystemTest.main()
