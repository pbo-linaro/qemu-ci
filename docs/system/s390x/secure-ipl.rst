.. SPDX-License-Identifier: GPL-2.0-or-later

s390 Secure IPL
===============

Secure IPL, also known as secure boot, enables s390-ccw virtual machines to
leverage qcrypto libraries and z/Arch implementations to verify the integrity of
guest kernels. These operations are rely on userspace invocations and QEMU
interpretation. The user provides one or more certificates via the command line
options, which populates a certificate store. DIAGNOSE 'X'320' is invoked by
userspace to query cert store info and retrieve specific certificates from QEMU.
DIAGNOSE 'X'508' is used by userspace to leverage qcrypto libraries to perform
signature-verification in QEMU. Lastly, userspace generates and appends an
IPL Information Report Block (IIRB) at the end of the IPL Parameter Block.

The steps are as follows:

- Userspace retrieves data payload from disk (e.g. stage3 boot loader, kernel)
- Userspace checks the validity of the SCLAB
- Userspace invokes DIAG 508 subcode 1 and provides it the payload
- QEMU handles DIAG 508 request by reading the payload and retrieving the
  certificate store
- QEMU DIAG 508 utilizes qcrypto libraries to perform signature-verification on
  the payload, attempting with each cert in the store (until success or 
  exhausted)
- QEMU DIAG 508 returns:

  - success: index of cert used to verify payload
  - failure: error code

- Userspace responds to this operation:

  - success: retrieves cert from store via DIAG 320 using returned index
  - failure: reports with warning (audit mode), aborts with error (secure mode)

- Userspace appends IIRB at the end of the IPLB
- Userspace kicks off IPL


Constraints
-----------

The following constraints apply when attempting to secure IPL an s390 guest:

- z16 CPU model
- certificates must be in X.509 DER format
- only sha256 encryption is supported
- only support for SCSI scheme of virtio-blk/virtio-scsi devices
- a boot device must be specified
- any unsupported devices (e.g., ECKD and VFIO) or non-eligible devices (e.g.,
  Net) will cause the entire boot process terminating early with an error 
  logged to the console.


s390 Certificate Store
======================

Secure boot relies on user certificates for signature-verification. Normally, 
these certificates would be stored somewhere on the LPAR. Instead, for virtual
guests, a certificate store is implemented within QEMU. This store will read
any certificates provided by the user via command-line, which are expected to
be stored somewhere on the host file system. Once these certificates are
stored, they are ready to be queried/requested by DIAGNOSE 'X'320' or used for
verification by DIAGNOSE 'X'508'.

The certificate store can be populated by supplying a comma-delimited list of
certificates on the command-line:

.. code-block:: shell

    qemu-system-s390x -machine s390-ccw-virtio, \
    boot-certificates=/.../qemu/certs:/another/path/cert.der


DIAGNOSE function code 'X'320' - Certificate Store Facility
-----------------------------------------------------------

DIAGNOSE 'X'320' is used to provide support to query the certificate store.

Subcode 0 - query installed subcodes
    Returns a 256-bit installed subcodes mask (ISM) stored in the installed
    subcodes block (ISB). This mask indicates which sucodes are currently
    installed and available for use.

Subcode 1 - query verification certificate storage information
    Provides the information required to determine the amount of memory needed to
    store one or more verification-certificates (VCs) from the certificate store (CS).

    Upon successful completion, this subcode returns various storage size values for
    verification-certificate blocks (VCBs).

    The output is returned in the verification-certificate-storage-size block (VCSSB).

Subcode 2 - store verification certificates
    Provides VCs that are in the certificate store.

    The output is provided in a VCB, which includes a common header followed by zero
    or more verification-certificate entries (VCEs).

    The first-VC index and last-VC index fields of VCB specify the range of VCs 
    to be stored by subcode 2. Stored count and remained count fields specify the 
    number of VCs stored and could not be stored in the VCB due to insufficient 
    storage specified in the VCB input length field.

    VCE contains various information of a VC from the CS.


IPL Modes
=========

Different IPL modes may be toggled with the following command line option:

.. code-block:: shell

    qemu-system-s390x -machine s390-ccw-virtio,secure-boot=on|off

Additionally, the provision of certificates affect the mode.


Normal Mode
-----------

The absence of both certificates and the ``secure-boot`` option will attempt to
IPL a guest without secure IPL operations. No checks are performed, and no
warnings/errors are reported.  This is the default mode, and can be explicitly
enabled with ``secure-boot=off``.


Audit Mode
----------

With *only* the presence of certificates in the store, it is assumed that secure
boot operations should be performed with errors reported as warnings. As such,
the secure IPL operations will be performed, and any errors that stem from these
operations will report a warning via the SCLP console.


Secure Mode
-----------

With *both* the presence of certificates in the store and the ``secure-boot=on``
option, it is understood that secure boot should be performed with errors
reported and boot will abort.


Secure IPL Functions
====================

IPL Information Report Block
----------------------------

The IPL Parameter Block (IPLPB), utilized for IPL operation, is extended with an
IPL Information Report Block (IIRB), which contains the results from secure IPL
operations such as:

* component data
* verification results
* certificate data


Secure Code Loading Attributes Facility
---------------------------------

Secure Code Loading Attributes Facility (SCLAF) provides additional security during IPL.

When SCLAF is available, its behavior depends on the IPL Modes.

* secure mode: IPL will terminate on any errors detected by this facility. 
* audit mode:  IPL may proceed regardless of any errors detected by this facility.

Errors detected by the SCLAF are reported in IIRB.

Unsigned components may only be loaded at absolute storage address x’2000’ or higher.

Signed components must include a Secure Code Loading Attribute Block (SCLAB),
which is located at the very end of the signed component.

**Secure Code Loading Attribute Block (SCLAB)**

The SCLAB is located at the end of each signed component. It defines the code loading
attributes for the component and may:

* Provide direction on how to process the rest of the component.

* Provide further validation of information on where to load the signed binary code
  from the load device.

* Specify where to start the execution of the loaded OS code.


DIAGNOSE function code 'X'508' - KVM IPL extensions
---------------------------------------------------

DIAGNOSE 'X'508' is reserved for KVM guest use in order to facilitate 
communication of additional IPL operations that cannot be handled by userspace,
such as signature verification for secure IPL.

If the function code specifies 0x508, KVM IPL extension functions are performed.
These functions are meant to provide extended functionality for s390 guest boot
that requires assistance from QEMU.

Subcode 0 - query installed subcodes
    Returns a 64-bit mask indicating which subcodes are supported.

Subcode 1 - perform signature verification
    Used to perform signature-verification on a signed component, leveraging
    qcrypto libraries to perform this operation and pulling from the certificate
    store.


Secure IPL Quickstart
=====================

Build QEMU with gnutls enabled:

.. code-block:: shell

    ./configure … --enable-gnutls

Generate certificate (e.g. via openssl):

.. code-block:: shell

    openssl req -new -x509 -newkey rsa:2048 -keyout mykey.priv \
                -outform DER -out mycert.der -days 36500 \
                -subj "/CN=My Name/" -nodes

Sign Images (e.g. via sign-file):

- signing must be performed on a KVM guest filesystem
- sign-file script used in the example below is located within the kernel source
  repo

.. code-block:: shell

    ./sign-file sha256 mykey.priv mycert.der /boot/vmlinuz-…
    ./sign-file sha256 mykey.priv mycert.der /usr/lib/s390-tools/stage3.bin

Run zipl with secure boot enabled

.. code-block:: shell

    zipl --secure 1 -V

Start Guest with Cmd Options:

.. code-block:: shell

    qemu-system-s390x -machine s390-ccw-virtio,secure-boot=on,boot-certificates=mycert.der ...
