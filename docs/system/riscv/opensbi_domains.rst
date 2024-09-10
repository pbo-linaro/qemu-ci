OpenSBI Domains
===============

OpenSBI has support for domains, which are partitions of CPUs and memory into
isolated compartments. Domains can be specified in the device tree according to
a standardized format [1_], which OpenSBI parses at boot time to initialize all
system domains. Depending on the specific QEMU machine being used, these domain
configurations can be specified on the QEMU command line. Currently, this is
only possible for the ``virt`` machine. To enable this functionality for a new
machine, the initialization code must call ``create_fdt_opensbi_domains`` after
all device tree nodes for peripherals have been initialized. This is to ensure
that references to devices work for MMIO regions.

There are two "devices" that are used to configure OpenSBI domains with this
mechanism: ``opensbi-memregion`` and ``opensbi-domain``.

Memregions
----------

OpenSBI memregions can be added to the machine's device tree with the following
flag:

.. code-block:: bash

    -device opensbi-memregion

For this device flag, the following options are implemented:

- ``id``: The name of the memregion. This name is later used to link this
  region to a domain if desired, in which case this argument is required.
- ``base`` (required): The base address of this memregion. This address must
  be aligned to ``2 ^ order``.
- ``order`` (required): The ``log2`` of the memregion's size, which must be
  between 3 and ``__riscv_xlen`` inclusive.
- ``mmio`` (optional): A boolean indicating whether the specified physical
  address range belongs to an MMIO-mapped peripheral device.
- ``deviceX`` (optional): If ``mmio`` is indicated, this is the device tree
  path to the ``X``-th device corresponding to this physical address range,
  where ``0 <= X < OPENSBI_MEMREGION_DEVICES_MAX`` (default 16).

Domains
-------

OpenSBI domains can be added to the machine's device tree with the following
flag:

.. code-block:: bash

    - device opensbi-domain

For this device flag, the following options are implemented:

- ``id`` (required): The name of the domain, which becomes its identifier in
  the device tree
- ``boot-hart`` (optional): The HART booting the domain instance.
- ``possible-harts`` (optional): The contiguous list of CPUs for the domain
  instance, specified as ``firstcpu[-lastcpu]`` (e.g. ``0-3``).
- ``next-arg1`` (optional): The 64 bit next booting stage arg1 for the domain
  instance.
- ``next-addr`` (optional): The 64 bit next booting stage address for the
  domain instance.
- ``next-mode`` (optional): The 32 bit next booting stage mode for the domain
  instance.
- ``system-reset-allowed`` (optional): Whether the domain instance is allowed
  to do system reset.
- ``system-suspend-allowed`` (optional): Whether the domain instance is allowed
  to do system suspend.

Furthermore, memregions can be linked to domains using the following options:

- ``regionX`` (optional): The ``id`` of the ``X``-th region for this domain,
  where ``0 <= X < OPENSBI_DOMAIN_MEMREGIONS_MAX`` (default 16).
- ``permsX`` (optional): Access permissions for the ``X``-th region for this
  domain, ``0 <= X < OPENSBI_DOMAIN_MEMREGIONS_MAX`` (default 16). This must be
  encoded using OpenSBI's permission encoding scheme in ``sbi_domain.h``, and
  copied below at the time of writing for convenience

.. code-block:: c

    /** Flags representing memory region attributes */
    #define SBI_MEMREGION_M_READABLE	(1UL << 0)
    #define SBI_MEMREGION_M_WRITABLE	(1UL << 1)
    #define SBI_MEMREGION_M_EXECUTABLE	(1UL << 2)
    #define SBI_MEMREGION_SU_READABLE	(1UL << 3)
    #define SBI_MEMREGION_SU_WRITABLE	(1UL << 4)
    #define SBI_MEMREGION_SU_EXECUTABLE	(1UL << 5)

Example
-------

A complete example command line is shown below:

.. code-block:: bash

    $ qemu-system-riscv64 -machine virt -bios fw_jump.bin -cpu max -smp 2 -m 4G -nographic \
            -device opensbi-memregion,id=mem,base=0xBC000000,order=26,mmio=false \
            -device opensbi-memregion,id=uart,base=0x10000000,order=12,mmio=true,device0="/soc/serial@10000000" \
            -device opensbi-domain,id=domain,possible-harts=0-1,boot-hart=0x0,next-addr=0xBC000000,next-mode=1,region0=mem,perms0=0x3f,region1=uart,perms1=0x3f

As a result of the above configuration, QEMU will add the following subnodes to
the device tree:

.. code-block:: dts

    chosen {
            opensbi-domains {
                    compatible = "opensbi,domain,config";

                    domain {
                            next-mode = <0x01>;
                            next-addr = <0x00 0xbc000000>;
                            boot-hart = <0x03>;
                            regions = <0x8000 0x3f 0x8002 0x3f>;
                            possible-harts = <0x03 0x01>;
                            phandle = <0x8003>;
                            compatible = "opensbi,domain,instance";
                    };

                    uart {
                            phandle = <0x8002>;
                            devices = <0x1800000>;
                            mmio;
                            order = <0x0c>;
                            base = <0x00 0x10000000>;
                            compatible = "opensbi,domain,memregion";
                    };

                    mem {
                            phandle = <0x8000>;
                            order = <0x1a>;
                            base = <0x00 0xbc000000>;
                            compatible = "opensbi,domain,memregion";
                    };
            };
    };

This results in OpenSBI output as below, where regions 01-03 are inherited from
the root domain and regions 00 and 04 correspond to the user specified ones:

.. code-block:: console

    Domain1 Name              : domain
    Domain1 Boot HART         : 0
    Domain1 HARTs             : 0,1
    Domain1 Region00          : 0x0000000010000000-0x0000000010000fff M: (I,R,W,X) S/U: (R,W,X)
    Domain1 Region01          : 0x0000000002000000-0x000000000200ffff M: (I,R,W) S/U: ()
    Domain1 Region02          : 0x0000000080080000-0x000000008009ffff M: (R,W) S/U: ()
    Domain1 Region03          : 0x0000000080000000-0x000000008007ffff M: (R,X) S/U: ()
    Domain1 Region04          : 0x00000000bc000000-0x00000000bfffffff M: (R,W,X) S/U: (R,W,X)
    Domain1 Next Address      : 0x00000000bc000000
    Domain1 Next Arg1         : 0x0000000000000000
    Domain1 Next Mode         : S-mode
    Domain1 SysReset          : no
    Domain1 SysSuspend        : no

.. _1: https://github.com/riscv-software-src/opensbi/blob/master/docs/domain_support.md
