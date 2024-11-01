CheckPoint and Restart (CPR)
============================

CPR is the umbrella name for a set of migration modes in which the
VM is migrated to a new QEMU instance on the same host.  It is
intended for use when the goal is to update host software components
that run the VM, such as QEMU or even the host kernel.  At this time,
the cpr-reboot and cpr-transfer modes are available.

Because QEMU is restarted on the same host, with access to the same
local devices, CPR is allowed in certain cases where normal migration
would be blocked.  However, the user must not modify the contents of
guest block devices between quitting old QEMU and starting new QEMU.

CPR unconditionally stops VM execution before memory is saved, and
thus does not depend on any form of dirty page tracking.

cpr-reboot mode
---------------

In this mode, QEMU stops the VM, and writes VM state to the migration
URI, which will typically be a file.  After quitting QEMU, the user
resumes by running QEMU with the ``-incoming`` option.  Because the
old and new QEMU instances are not active concurrently, the URI cannot
be a type that streams data from one instance to the other.

Guest RAM can be saved in place if backed by shared memory, or can be
copied to a file.  The former is more efficient and is therefore
preferred.

After state and memory are saved, the user may update userland host
software before restarting QEMU and resuming the VM.  Further, if
the RAM is backed by persistent shared memory, such as a DAX device,
then the user may reboot to a new host kernel before restarting QEMU.

This mode supports VFIO devices provided the user first puts the
guest in the suspended runstate, such as by issuing the
``guest-suspend-ram`` command to the QEMU guest agent.  The agent
must be pre-installed in the guest, and the guest must support
suspend to RAM.  Beware that suspension can take a few seconds, so
the user should poll to see the suspended state before proceeding
with the CPR operation.

Usage
^^^^^

It is recommended that guest RAM be backed with some type of shared
memory, such as ``memory-backend-file,share=on``, and that the
``x-ignore-shared`` capability be set.  This combination allows memory
to be saved in place.  Otherwise, after QEMU stops the VM, all guest
RAM is copied to the migration URI.

Outgoing:
  * Set the migration mode parameter to ``cpr-reboot``.
  * Set the ``x-ignore-shared`` capability if desired.
  * Issue the ``migrate`` command.  It is recommended the URI be a
    ``file`` type, but one can use other types such as ``exec``,
    provided the command captures all the data from the outgoing side,
    and provides all the data to the incoming side.
  * Quit when QEMU reaches the postmigrate state.

Incoming:
  * Start QEMU with the ``-incoming defer`` option.
  * Set the migration mode parameter to ``cpr-reboot``.
  * Set the ``x-ignore-shared`` capability if desired.
  * Issue the ``migrate-incoming`` command.
  * If the VM was running when the outgoing ``migrate`` command was
    issued, then QEMU automatically resumes VM execution.

Example 1
^^^^^^^^^
::

  # qemu-kvm -monitor stdio
  -object memory-backend-file,id=ram0,size=4G,mem-path=/dev/dax0.0,align=2M,share=on -m 4G
  ...

  (qemu) info status
  VM status: running
  (qemu) migrate_set_parameter mode cpr-reboot
  (qemu) migrate_set_capability x-ignore-shared on
  (qemu) migrate -d file:vm.state
  (qemu) info status
  VM status: paused (postmigrate)
  (qemu) quit

  ### optionally update kernel and reboot
  # systemctl kexec
  kexec_core: Starting new kernel
  ...

  # qemu-kvm ... -incoming defer
  (qemu) info status
  VM status: paused (inmigrate)
  (qemu) migrate_set_parameter mode cpr-reboot
  (qemu) migrate_set_capability x-ignore-shared on
  (qemu) migrate_incoming file:vm.state
  (qemu) info status
  VM status: running

Example 2: VFIO
^^^^^^^^^^^^^^^
::

  # qemu-kvm -monitor stdio
  -object memory-backend-file,id=ram0,size=4G,mem-path=/dev/dax0.0,align=2M,share=on -m 4G
  -device vfio-pci, ...
  -chardev socket,id=qga0,path=qga.sock,server=on,wait=off
  -device virtserialport,chardev=qga0,name=org.qemu.guest_agent.0
  ...

  (qemu) info status
  VM status: running

  # echo '{"execute":"guest-suspend-ram"}' | ncat --send-only -U qga.sock

  (qemu) info status
  VM status: paused (suspended)
  (qemu) migrate_set_parameter mode cpr-reboot
  (qemu) migrate_set_capability x-ignore-shared on
  (qemu) migrate -d file:vm.state
  (qemu) info status
  VM status: paused (postmigrate)
  (qemu) quit

  ### optionally update kernel and reboot
  # systemctl kexec
  kexec_core: Starting new kernel
  ...

  # qemu-kvm ... -incoming defer
  (qemu) info status
  VM status: paused (inmigrate)
  (qemu) migrate_set_parameter mode cpr-reboot
  (qemu) migrate_set_capability x-ignore-shared on
  (qemu) migrate_incoming file:vm.state
  (qemu) info status
  VM status: paused (suspended)
  (qemu) system_wakeup
  (qemu) info status
  VM status: running

Caveats
^^^^^^^

cpr-reboot mode may not be used with postcopy, background-snapshot,
or COLO.

cpr-transfer mode
-----------------

This mode allows the user to transfer a guest to a new QEMU instance
on the same host with minimal guest pause time, by preserving guest
RAM in place, albeit with new virtual addresses in new QEMU.

The user starts new QEMU on the same host as old QEMU, with the
same arguments as old QEMU, plus the ``-incoming option``.  The user
issues the migrate command to old QEMU, which stops the VM, saves
state to the migration channels, and enters the postmigrate state.
Execution resumes in new QEMU.

This mode requires a second migration channel, specified by the
``cpr-uri`` migration property on the outgoing side, and by the
``cpr-uri`` QEMU command-line option on the incoming side.  The
channel must be a type, such as unix socket, that supports SCM_RIGHTS.

Usage
^^^^^

Memory backend objects must have the ``share=on`` attribute.

The VM must be started with the ``-machine anon-alloc=memfd``
option.  This causes implicit RAM blocks (those not described by
a memory-backend object) to be allocated by mmap'ing a memfd.
Examples include VGA and ROM.

Outgoing:
  * Set the migration mode parameter to ``cpr-transfer``.
  * Set the ``cpr-uri`` parameter.  It must be a ``unix`` type.
  * Issue the ``migrate`` command.

Incoming:
  * Start new QEMU with the ``-incoming`` and ``-cpr-uri`` options.
  * If the VM was running when the outgoing ``migrate`` command was
    issued, then QEMU automatically resumes VM execution.

Caveats
^^^^^^^

cpr-transfer mode may not be used with postcopy, background-snapshot,
or COLO.

memory-backend-epc and memory-backend-ram are not supported.

The incoming migration channel cannot be a file type.

If the incoming migration channel is a tcp type, then the port cannot
be 0 (meaning dynamically choose a port).

When using ``-incoming defer``, you must issue the migrate command to
old QEMU before issuing any monitor commands to new QEMU, because new
QEMU blocks waiting to read from cpr-uri before starting its monitor,
and old QEMU does not write to cpr-uri until the migrate command is
issued.  However, new QEMU does not open and read the migration stream
until you issue the migrate incoming command.

Example 1: incoming channel
^^^^^^^^^^^^^^^^^^^^^^^^^^^

In these examples, we simply restart the same version of QEMU, but
in a real scenario one would start new QEMU on the incoming side.
Note that new QEMU does not print the monitor prompt until old QEMU
has issued the migrate command.

::

  Outgoing:                             Incoming:

  # qemu-kvm -monitor stdio
  -object memory-backend-file,id=ram0,size=4G,
  mem-path=/dev/shm/ram0,share=on -m 4G
  -machine anon-alloc=memfd'
  ...
                                        # qemu-kvm -incoming tcp:0:44444
                                        -cpr-uri unix:cpr.sock
                                        ...
  QEMU 9.2.50 monitor
  (qemu) info status
  VM status: running
  (qemu) migrate_set_parameter mode cpr-transfer
  (qemu) migrate_set_parameter cpr-uri unix:cpr.sock
  (qemu) migrate -d tcp:0:44444

                                        QEMU 9.2.50 monitor
                                        (qemu) info status
                                        VM status: running

  (qemu) info status
  VM status: paused (postmigrate)


Example 2: incoming defer
^^^^^^^^^^^^^^^^^^^^^^^^^

This example uses ``-incoming defer`` to hot plug a device before
accepting the migration stream.  Again note you must issue the
migrate command to old QEMU before you can issue any monitor
commands to new QEMU.


::

  Outgoing:                             Incoming:

  # qemu-kvm -monitor stdio
  -object memory-backend-file,id=ram0,size=4G,
  mem-path=/dev/shm/ram0,share=on -m 4G
  -machine anon-alloc=memfd'
  ...
                                        # qemu-kvm -incoming defer
                                        -cpr-uri unix:cpr.sock
                                        ...
  QEMU 9.2.50 monitor
  (qemu) device_add pcie-root-port
  (qemu) migrate_set_parameter mode cpr-transfer
  (qemu) migrate_set_parameter cpr-uri unix:cpr.sock
  (qemu) migrate -d tcp:0:44444

                                        QEMU 9.2.50 monitor
                                        (qemu) info status
                                        VM status: paused (inmigrate)
                                        (qemu) device_add pcie-root-port
                                        (qemu) migrate_incoming tcp:0:44444
                                        (qemu) info status
                                        VM status: running

  (qemu) info status
  VM status: paused (postmigrate)

Futures
^^^^^^^

cpr-transfer mode is based on a capability to transfer open file
descriptors from old to new QEMU.  In the future, descriptors for
vfio, iommufd, vhost, and char devices could be transferred,
preserving those devices and their kernel state without interruption,
even if they do not explicitly support live migration.
