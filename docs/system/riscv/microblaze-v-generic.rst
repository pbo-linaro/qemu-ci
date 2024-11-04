Microblaze-V generic board (``amd-microblaze-v-generic``)
=========================================================
The AMD MicroBlaze™ V processor is a soft-core RISC-V processor IP for AMD adaptive SoCs and FPGAs.
The MicroBlaze V processor is based on a 32-bit / 64-bit RISC-V instruction set architecture (ISA)
and its fully hardware compatible with the classic MicroBlaze processor.

More details here:
https://docs.amd.com/r/en-US/ug1629-microblaze-v-user-guide/MicroBlaze-V-Architecture

The microblaze-v generic board in QEMU has following supported devices

Implemented CPU cores:

1 RISCV core
    * RV32I base integer instruction set
    * "Zicsr" Control and Status register instructions
    * "Zifencei" instruction-fetch
    * Configurable features:
      - RV64I Base Integer Instruction Set
      - Extensions supported: M, A, C, F, Zba, Zbb, Zbc, Zbs

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


   $ qemu-system-riscv32 -M amd-microblaze-v-generic \
     -display none \
     -device loader,addr=0x80000000,file=u-boot-spl.bin,cpu-num=0 \
     -device loader,addr=0x80200000,file=u-boot.img \
     -serial mon:stdio \
     -device loader,addr=0x83000000,file=system.dtb \
     -m 2g
