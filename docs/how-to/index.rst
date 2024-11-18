.. _how-to:

------
How to
------

This section of the manual will give you some commands to do various tasks with
QEMU. It does not intend to be complete, but to be simple.

Build
-----

First you need setup your `build environment <setup-build-env>`.

Then, you can build QEMU using:

::

    git clone https://gitlab.com/qemu-project/qemu
    cd qemu
    ./configure
    ninja -C build
    # all binaries are in ./build

By default, QEMU build is optimized. You may want to switch to debug builds
instead (non optimized, and with more runtime checks enabled):

::

    ./configure --enable-debug

It's recommended to use sanitizers to catch issues when developing your change.

::

    ./configure --enable-asan --enable-ubsan
    # Of course, you can combine debug and sanitizers if needed

You can find more information on `build page <build>`.

Test
----

QEMU has a lot of tests, mainly in 4 categories:

::

    # run tests related to TCG. They are based on Makefiles.
    make check-tcg
    # run system tests, running a full VM, with avocado framework
    make check-avocado
    # run functional tests, running a full VM, integrated with Meson
    make check-functional
    # run all other tests, integrated with Meson
    make check

You can find more information on `testing page<testing>`.

Use QEMU
--------

To create a 20 gigabytes disk image usable with qemu-system:

::

    qemu-img create system.img 20g

To run an x86_64 system emulated, with 4 cpus, 8G of memory and an install iso:

::

    qemu-system-x86_64 -smp 4 -m 8G system.img -cdrom install.iso

To boot directly a Linux Kernel:

::

    qemu-system-x86_64 -kernel bzImage -hda system.img -append "root=/dev/hda"

To boot an aarch64 system emulated, you need to specify a UEFI and associated
pflash. Once started, you can switch to Serial output by clicking on View ->
Serial0.

::

    # UEFI can be obtained from debian package qemu-efi-aarch64.
    # First, we need to copy a file to save UEFI variables:
    # cp /usr/share/AAVMF/AAVMF_VARS.fd .
    qemu-system-aarch64 \
        -m 8G \
        -smp 4 \
        -M virt \
        -cpu max \
        -device virtio-blk-pci,drive=root \
        -drive if=none,id=root,file=system.img \
        -drive if=pflash,readonly=on,file=/usr/share/AAVMF/AAVMF_CODE.fd \
        -drive if=pflash,file=AAVMF_VARS.fd \
        -cdrom install.iso

To run git using QEMU user-mode:

::

    qemu-x86_64 /usr/bin/git --version

Contribute
----------

We recommend using `git-publish <https://github.com/stefanha/git-publish>`_ for
contributing. You need to configure `git send-email
<https://git-send-email.io/>`_ first.

::

    git checkout -b my_feature
    ... # edit, build, test
    # When ready to send the series...

    # Add upstream QEMU repo as a remote.
    git remote add upstream https://gitlab.com/qemu-project/qemu
    # Fetch all new content.
    git fetch -a upstream

    # Rebase your branch on top of upstream master, and include a signoff.
    git rebase -i upstream/master --signoff
    # Check your patches are correct.
    ./scripts/checkpatch.pl $(git merge-base upstream/master HEAD)..HEAD

    # Send your series, you'll be given a chance to edit cover letter for it.
    git-publish

    # After review, and other changes, you can send a v2 simply by using:
    git-publish

If you need to apply locally an existing series, you can use `b4
<https://github.com/mricon/b4>`_ (installable via pip) to retrieve it:

::

    b4 shazam <series_msg_id>
    # message id is an identifier present in email sent.
    # when using patchwork, it is the last part of a series url (2024...):
    # https://patchew.org/QEMU/20241118021820.4928-1-joel@jms.id.au/

More complete information is available on our `Submit a patch page
<submitting-a-patch>`.
