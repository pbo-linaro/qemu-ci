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
 * Clock Tree

Boot options
------------

The ``imx8mp-evk`` machine can start using the standard -kernel functionality
for loading a Linux kernel.

Direct Linux Kernel Boot
''''''''''''''''''''''''

Probably the easiest way to get started with a whole Linux system on the machine
is to generate an image with Buildroot. Version 2024.11.1 is tested at the time
of writing and involves two steps. First run the following commands in the
toplevel directory of the Buildroot source tree:

.. code-block:: bash

  $ echo "BR2_TARGET_ROOTFS_CPIO=y" >> configs/freescale_imx8mpevk_defconfig
  $ make freescale_imx8mpevk_defconfig
  $ make

Once finished successfully there is an ``output/image`` subfolder. Navigate into
it patch the device tree needs to be patched with the following commands which
will remove the ``cpu-idle-states`` properties from CPU nodes:

.. code-block:: bash

  $ dtc imx8mp-evk.dtb | sed '/cpu-idle-states/d' > imx8mp-evk-patched.dts
  $ dtc imx8mp-evk-patched.dts -o imx8mp-evk-patched.dtb

Now that everything is prepared the newly built image can be run in the QEMU
``imx8mp-evk`` machine:

.. code-block:: bash

  $ qemu-system-aarch64 -M imx8mp-evk -smp 4 -m 3G \
      -display none -serial null -serial stdio \
      -kernel Image \
      -dtb imx8mp-evk-patched.dtb \
      -initrd rootfs.cpio \
      -append "root=/dev/ram"
