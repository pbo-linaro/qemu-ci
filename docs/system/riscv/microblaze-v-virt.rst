Microblaze-V virt board (``amd-microblaze-v-virt``)
===================================================
The AMD MicroBlaze™ V processor is a soft-core RISC-V processor IP for AMD adaptive SoCs and FPGAs.
The MicroBlaze V processor is based on a 32-bit RISC-V instruction set architecture (ISA).

More details here:
https://docs.amd.com/r/en-US/ug1629-microblaze-v-user-guide/MicroBlaze-V-Architecture

The microblaze-v virt board in QEMU is a virtual board with
following supported devices

Implemented CPU cores:

1 RISCV32 core

Implemented devices:

    - timer
    - uartlite
    - uart16550
    - emaclite
    - timer2
    - axi emac
    - axi dma

Running
"""""""
Running U-boot

.. code-block:: bash


   $ qemu-system-riscv32 -M amd-microblaze-v-virt \
     -display none \
     -device loader,addr=0x80000000,file=u-boot-spl.bin,cpu-num=0 \
     -device loader,addr=0x80200000,file=u-boot.img \
     -serial mon:stdio \
     -device loader,addr=0x83000000,file=system.dtb \
     -m 2g
