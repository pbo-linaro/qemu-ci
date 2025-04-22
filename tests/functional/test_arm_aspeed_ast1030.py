#!/usr/bin/env python3
#
# Functional test that boots the ASPEED SoCs with firmware
#
# Copyright (C) 2022 ASPEED Technology Inc
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import LinuxKernelTest, Asset
from qemu_test import exec_command_and_wait_for_pattern


class AST1030Machine(LinuxKernelTest):

    ASSET_ZEPHYR_3_00 = Asset(
        ('https://github.com/AspeedTech-BMC'
         '/zephyr/releases/download/v00.03.00/ast1030-evb-demo.zip'),
        '37fe3ecd4a1b9d620971a15b96492a81093435396eeac69b6f3e384262ff555f')

    def test_ast1030_zephyros_3_00(self):
        self.set_machine('ast1030-evb')

        kernel_name = "ast1030-evb-demo/zephyr.bin"
        kernel_file = self.archive_extract(
            self.ASSET_ZEPHYR_3_00, member=kernel_name)

        self.vm.set_console()
        self.vm.add_args('-kernel', kernel_file, '-nographic')
        self.vm.launch()
        self.wait_for_console_pattern("Booting Zephyr OS")
        for shell_cmd in [
                'kernel stacks',
                'hwinfo devid',
                'crypto aes256_cbc_vault',
                'jtag jtag@7e6e4100 sw_xfer high TMS',
                'iic scan i2c@7e7b0080',
                'hash test',
                'kernel uptime',
                'kernel reboot warm',
                'kernel uptime',
                'kernel reboot cold',
                'kernel uptime',
        ]: exec_command_and_wait_for_pattern(self, shell_cmd, "uart:~$")


if __name__ == '__main__':
    LinuxKernelTest.main()
