.. _Glossary:

--------
Glossary
--------

This section of the manual presents *simply* acronyms and terms QEMU developers
use.

Accelerator
-----------

A specific API used to accelerate execution of guest instructions. It can be
hardware-based, through a virtualization API provided by the host OS (kvm, hvf,
whpx, ...) or software-based (tcg). See this description of `supported
accelerators<Accelerators>`.

Board
-----

QEMU system defines board models for various architectures. It's a description
of a SoC (system-on-chip) with various devices pre-configured, and can be
selected with the option ``-machine`` of qemu-system.
For virtual machines, you'll use ``virt`` board model, designed for this use
case. As an example, for Arm architecture, you can find the `model code
<https://gitlab.com/qemu-project/qemu/-/blob/master/hw/arm/virt.c>`_ and
associated `documentation <arm-virt>`.

Block
-----

Block drivers are the available `disk formats <block-drivers>` available, and
block devices `(see Block device section on options page)<sec_005finvocation>`
are using them to implement disks for a virtual machine.

CFI
---

Control Flow Integrity is a hardening technique used to prevent exploits
targeting QEMU by detecting unexpected branches during execution. QEMU `actively
supports<cfi>` being compiled with CFI enabled.

Device
------

QEMU is able to emulate a CPU, and all the hardware interacting with it,
including many devices. When QEMU runs a virtual machine using a hardware-based
accelerator, it is responsible for emulating, using software, all devices.

EDK2
----

EDK2, as known as `TianoCore <https://www.tianocore.org/>`_, is an open source
implementation of UEFI standard. It's ran by QEMU to support UEFI for virtual
machines.

gdbstub
-------

QEMU implements a `gdb server <GDB usage>`, allowing gdb to attach to it and
debug a running virtual machine, or a program in user-mode. This allows to debug
a given architecture without having access to hardware.

glib2
-----

`GLib2 <https://docs.gtk.org/glib/>`_ is one of the most important library we
are using through the codebase. It provides many data structures, macros, string
and thread utilities and portable functions across different OS. It's required
to build QEMU.

Guest agent
-----------

`QEMU Guest agent <qemu-ga>` is a daemon intended to be executed by guest
virtual machines and providing various services to help QEMU to interact with
it.

Guest/Host
----------

Guest is the architecture of the virtual machine, which is emulated.
Host is the architecture on which QEMU is running on, which is native.

Hypervisor
----------

The formal definition of an hypervisor is a program than can be used to manage a
virtual machine. QEMU itself is an hypervisor.

In the context of QEMU, an hypervisor is an API, provided by the Host OS,
allowing to execute virtual machines. Linux implementation is KVM (and supports
Xen as well). For MacOS, it's HVF. Windows defines WHPX. And NetBSD provides
NVMM.

Migration
---------

QEMU can save and restore the execution of a virtual machine, including across
different machines. This is provided by the `Migration framework<migration>`.

NBD
---

`QEMU Network Block Device server <qemu-nbd>` is a tool that can be used to
mount and access QEMU images, providing functionality similar to a loop device.

Mailing List
------------

This is `where <https://wiki.qemu.org/Contribute/MailingLists>`_ all the
development happens! Changes are posted as series, that all developers can
review and share feedback for.

For reporting issues, our `GitLab
<https://gitlab.com/qemu-project/qemu/-/issues>`_ tracker is the best place.

MMU / softmmu
-------------

The Memory Management Unit is responsible for translating virtual addresses to
physical addresses and managing memory protection. QEMU system mode is named
"softmmu" precisely because it implements this in software, including a TLB
(Translation lookaside buffer), for the guest virtual machine.

QEMU user-mode does not implement a full software MMU, but "simply" translates
virtual addresses by adding a specific offset, and relying on host MMU/OS
instead.

Monitor / QMP / HMP
-------------------

`QEMU Monitor <QEMU monitor>` is a text interface which can be used to interact
with a running virtual machine.

QMP stands for QEMU Monitor Protocol and is a json based interface.
HMP stands for Human Monitor Protocol and is a set of text commands available
for users who prefer natural language to json.

MTTCG
-----

Multiple cpus support was first implemented using a round-robin algorithm
running on a single thread. Later on, `Multi-threaded TCG <mttcg>` was developed
to benefit from multiple cores to speed up execution.

Plugins
-------

`TCG Plugins <TCG Plugins>` is an API used to instrument guest code, in system
and user mode. The end goal is to have a similar set of functionality compared
to `DynamoRIO <https://dynamorio.org/>`_ or `valgrind <https://valgrind.org/>`_.

One key advantage of QEMU plugins is that they can be used to perform
architecture agnostic instrumentation.

Patchwork
---------

`Patchwork <https://patchew.org/QEMU/>`_ is a website that tracks
patches on the Mailing List.

PR
--

Once a series is reviewed and accepted by a subsystem maintainer, it will be
included in a PR (Pull Request) that the project maintainer will merge into QEMU
main branch, after running tests.

QCOW
----

QEMU Copy On Write is a disk format developed by QEMU. It provides transparent
compression, automatic extension, and many other advantages over a raw image.

QEMU
----

`QEMU (Quick Emulator) <https://www.qemu.org/>`_ is a generic and open source
machine emulator and virtualizer.

QOM
---

`QEMU Object Model <qom>` is an object oriented API used to define various
devices and hardware in the QEMU codebase.

Record/replay
-------------

`Record/replay <replay>` is a feature of QEMU allowing to have a deterministic
and reproducible execution of a virtual machine.

Rust
----

`A new programming language <https://www.rust-lang.org/>`_, memory safe by
default. We didn't see a more efficient way to create debates and tensions in
a community of C programmers since the birth of C++.

System mode
-----------

QEMU System mode emulates a full machine, including its cpu, memory and devices.
It can be accelerated to hardware speed by using one of the hypervisors QEMU
supports. It is referenced as softmmu as well.

TCG
---

`Tiny Code Generator <tcg>` is an intermediate representation (IR) used to run
guest instructions on host cpu, with both architectures possibly being
different.

It is one of the accelerator supported by QEMU, and supports a lot of
guest/host architectures.

User mode
---------

QEMU User mode allows to run programs for a guest architecture, on a host
architecture, by translating system calls and using TCG. It is available for
Linux and BSD.

VirtIO
------

VirtIO is an open standard used to define and implement virtual devices with a
minimal overhead, defining a set of data structures and hypercalls (similar to
system calls, but targeting an hypervisor, which happens to be QEMU in our
case). It's designed to be more efficient than emulating a real device, by
minimizing the amount of interactions between a guest VM and its hypervisor.

vhost-user
----------

`Vhost-user <vhost_user>` is an interface used to implement VirtIO devices
outside of QEMU itself.
