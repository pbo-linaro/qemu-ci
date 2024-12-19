#!/usr/bin/env python3
#
# Check for vmfwupdate device.
#
# Copyright (c) 2024 Red Hat, Inc.
#
# Author:
#  Ani Sinha <anisinha@redhat.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import QemuSystemTest

class VmFwUpdateDeviceCheck(QemuSystemTest):
    def test_multiple_device_fail(self):
        """
        Only one vmfwdevice can be instantiated. Ensure failure if
        user tries to create more than one device.
        """
        self.vm.add_args('-device', 'vmfwupdate,id=fwupd1',
                         '-device', 'vmfwupdate,id=fwupd2')
        self.vm.set_qmp_monitor(enabled=False)
        self.vm.launch()
        self.vm.wait()
        self.assertEqual(self.vm.exitcode(), 1, "QEMU exit code should be 1")
        self.assertRegex(self.vm.get_log(),
                         r'at most one vmfwupdate device is permitted')

if __name__ == '__main__':
    QemuSystemTest.main()
