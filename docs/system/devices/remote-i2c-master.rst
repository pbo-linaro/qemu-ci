Remote I2C master
=================

Overview
--------
This module implements a virtual I2C controller device using FUSE (Filesystem in Userspace)
and CUSE (Character device in Userspace) technology. It allows userspace programs to
emulate I2C controllers that appear as real character devices in the system.

Features
--------
- Virtual I2C controller emulation via CUSE
- Full I2C character device interface
- Support for unrestricted IOCTL operations
- Integration with QEMU's AIO context for asynchronous operations
- Debugging support through FUSE debug mode

Architecture
------------
The module creates a virtual character device using CUSE that behaves like a physical I2C
controller. The device can be accessed through standard I2C tools and interfaces.

Kernel Requirements
~~~~~~~~~~~~~~~~~~~
- CUSE module loaded: ``sudo modprobe cuse``
- FUSE support enabled

Library Dependencies
~~~~~~~~~~~~~~~~~~~~
- libfuse3 or libfuse (version 2.9.0 or higher)
- FUSE development headers

Troubleshooting
~~~~~~~~~~~~~~~

CUSE_INIT Failures
^^^^^^^^^^^^^^^^^^
If you encounter ``CUSE_INIT`` errors:

1. Verify CUSE module is loaded:
   .. code-block:: bash

      lsmod | grep cuse
      sudo modprobe cuse

2. Check permissions:
   .. code-block:: bash

      # Ensure user has access to CUSE devices
      ls -la /dev/cuse

Debugging
---------

Enable debug output by including the debug option:

.. code-block:: c

   char fuse_opt_debug[] = FUSE_OPT_DEBUG;
   char *fuse_argv[] = { fuse_opt_dummy, fuse_opt_fore, fuse_opt_debug };

Examples
--------

Basic Usage
~~~~~~~~~~~

.. code-block:: bash

    ./qemu-system-arm -M ast2600-evb -device tmp105,address=0x40,bus=aspeed.i2c.bus.0 -device remote-i2c-master,i2cbus=aspeed.i2c.bus.0,devname=i2c-33

    $ i2cget -y 33 0x40 0x2
    0x4b

    $ i2cget -y 33 0x40 0x3
    0x50

See Also
--------
- `FUSE Documentation <https://github.com/libfuse/libfuse>`
- `Character devices in user space <https://lwn.net/Articles/308445/>`