#!/usr/bin/env python3
#
# Functional tests for the various graphics modes we can support.
#
# Copyright (c) 2024, 2025 Linaro Ltd.
#
# Author:
#  Alex Bennée <alex.bennee@linaro.org>
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu.machine.machine import VMLaunchFailure

from qemu_test import QemuSystemTest, Asset
from qemu_test import exec_command, exec_command_and_wait_for_pattern
from qemu_test import wait_for_console_pattern
from qemu_test import skipIfMissingCommands

from re import search
from subprocess import check_output


class Aarch64VirtGPUMachine(QemuSystemTest):
    KERNEL_COMMON_COMMAND_LINE = 'printk.time=0 '
    timeout = 360

    def wait_for_console_pattern(self, success_message, vm=None):
        wait_for_console_pattern(self, success_message,
                                 failure_message='Kernel panic - not syncing',
                                 vm=vm)

    ASSET_VIRT_GPU_KERNEL = Asset(
        'https://fileserver.linaro.org/s/ce5jXBFinPxtEdx/'
        'download?path=%2F&files='
        'Image',
        '89e5099d26166204cc5ca4bb6d1a11b92c217e1f82ec67e3ba363d09157462f6')

    ASSET_VIRT_GPU_ROOTFS = Asset(
        'https://fileserver.linaro.org/s/ce5jXBFinPxtEdx/'
        'download?path=%2F&files='
        'rootfs.ext4.zstd',
        '792da7573f5dc2913ddb7c638151d4a6b2d028a4cb2afb38add513c1924bdad4')

    def _run_virt_gpu_test(self, gpu_device,  weston_cmd, weston_pattern):

        self.set_machine('virt')
        self.require_accelerator("tcg")

        kernel_path = self.ASSET_VIRT_GPU_KERNEL.fetch()
        image_path = self.uncompress(self.ASSET_VIRT_GPU_ROOTFS, format="zstd")

        self.vm.set_console()
        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE +
                               'console=ttyAMA0 root=/dev/vda')

        self.vm.add_args("-accel", "tcg")
        self.vm.add_args("-cpu", "neoverse-v1,pauth-impdef=on")
        self.vm.add_args("-machine", "virt,gic-version=max",
                         '-kernel', kernel_path,
                         '-append', kernel_command_line)
        self.vm.add_args("-smp", "2", "-m", "2048")
        self.vm.add_args("-device", gpu_device)
        for opt in ["egl-headless", "dbus,gl=on"]:
            self.vm.add_args("-display", opt)

        self.vm.add_args("-device", "virtio-blk-device,drive=hd0")
        self.vm.add_args("-blockdev",
                         "driver=raw,file.driver=file,"
                         "node-name=hd0,read-only=on,"
                         f"file.filename={image_path}")
        self.vm.add_args("-snapshot")

        try:
            self.vm.launch()
        except VMLaunchFailure as excp:
            if "old virglrenderer, blob resources unsupported" in excp.output:
                self.skipTest("No blob support for virtio-gpu")
            elif "old virglrenderer, venus unsupported" in excp.output:
                self.skipTest("No venus support for virtio-gpu")
            elif "egl: no drm render node available" in excp.output:
                self.skipTest("Can't access host DRM render node")
            elif "'type' does not accept value 'egl-headless'" in excp.output:
                self.skipTest("egl-headless support is not available")
            else:
                self.log.info(f"unhandled launch failure: {excp.output}")
                raise excp

        self.wait_for_console_pattern('buildroot login:')
        exec_command(self, 'root')
        exec_command(self, 'export XDG_RUNTIME_DIR=/tmp')
        full_cmd = f"weston -B headless --renderer gl --shell kiosk -- {weston_cmd}"
        exec_command_and_wait_for_pattern(self, full_cmd, weston_pattern)

    @skipIfMissingCommands('zstd')
    def test_aarch64_virt_with_virgl_gpu(self):

        self.require_device('virtio-gpu-gl-pci')

        gpu_device = "virtio-gpu-gl-pci"
        weston_cmd = "glmark2-wayland -b:duration=1.0"
        weston_pattern = "glmark2 Score"
        self._run_virt_gpu_test(gpu_device, weston_cmd, weston_pattern)

    @skipIfMissingCommands('zstd')
    def test_aarch64_virt_with_virgl_blobs_gpu(self):

        self.require_device('virtio-gpu-gl-pci')

        gpu_device = "virtio-gpu-gl-pci,hostmem=4G,blob=on"
        weston_cmd = "glmark2-wayland -b:duration=1.0"
        weston_pattern = "glmark2 Score"
        self._run_virt_gpu_test(gpu_device, weston_cmd, weston_pattern)

    @skipIfMissingCommands('zstd')
    @skipIfMissingCommands('vulkaninfo')
    def test_aarch64_virt_with_vulkan_gpu(self):

        self.require_device('virtio-gpu-gl-pci')

        vk_info = check_output(["vulkaninfo", "--summary"], encoding="utf-8")

        if search(r"driverID\s+=\s+DRIVER_ID_NVIDIA_PROPRIETARY", vk_info):
            self.skipTest("Test skipped on NVIDIA proprietary driver")

        gpu_device = "virtio-gpu-gl-pci,hostmem=4G,blob=on,venus=on"
        weston_cmd = "vkmark -b:duration=1.0"
        weston_pattern = "vkmark Score"
        self._run_virt_gpu_test(gpu_device, weston_cmd, weston_pattern)

if __name__ == '__main__':
    QemuSystemTest.main()
