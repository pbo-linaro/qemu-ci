NXP i.MX 8M Plus Evaluation Kit (``imx8mp-evk``)
================================================

The QEMU i.MX 8M Plus EVK board emulation is intended to emulate a plain i.MX 8M
Plus system on chip (SoC). All peripherals the real board has such as flash and
I2C devices are intended to be added via configuration, e.g. command line.

Supported devices
-----------------

The ``imx8mp-evk`` machine implements the following devices:

 * Up to 4 Cortex-A53 Cores
 * Generic Interrupt Controller (GICv3)
 * 4 UARTs

Boot options
------------

The ``imx8mp-evk`` machine can start using the standard -kernel functionality
for loading a Linux kernel.

Direct Linux Kernel Boot
''''''''''''''''''''''''

Linux mainline v6.12 release is tested at the time of writing. To build a Linux
mainline kernel that can be booted by the ``imx8mp-evk`` machine, simply
configure the kernel using the defconfig configuration:

.. code-block:: bash

  $ export ARCH=arm64
  $ export CROSS_COMPILE=aarch64-linux-gnu-
  $ make defconfig
  $ make

To boot the newly built Linux kernel in QEMU with the ``imx8mp-evk`` machine,
run:

.. code-block:: bash

  $ qemu-system-aarch64 -M imx8mp-evk -smp 4 -m 3G \
      -display none -serial null -serial stdio \
      -kernel arch/arm64/boot/Image \
      -dtb arch/arm64/boot/dts/freescale/imx8mp-evk.dtb \
      -initrd /path/to/rootfs.ext4 \
      -append "root=/dev/ram"
