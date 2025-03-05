/*
 * QEMU System Emulator
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include "qemu/osdep.h"
#include "qemu/arch_info.h"

const char *qemu_arch_name(QemuArchBit qemu_arch_bit)
{
    static const char *legacy_target_names[] = {
        [QEMU_ARCH_ALPHA] = "alpha",
        [QEMU_ARCH_BIT_ARM] = TARGET_LONG_BITS == 32 ? "arm" : "aarch64",
        [QEMU_ARCH_BIT_AVR] = "avr",
        [QEMU_ARCH_BIT_HEXAGON] = "hexagon",
        [QEMU_ARCH_BIT_HPPA] = "hppa",
        [QEMU_ARCH_BIT_I386] = TARGET_LONG_BITS == 32 ? "i386" : "x86_64",
        [QEMU_ARCH_BIT_LOONGARCH] = "loongarch64",
        [QEMU_ARCH_BIT_M68K] = "m68k",
        [QEMU_ARCH_BIT_MICROBLAZE] = TARGET_BIG_ENDIAN ? "microblaze"
                                                       : "microblazeel",
        [QEMU_ARCH_BIT_MIPS] = TARGET_BIG_ENDIAN
                             ? (TARGET_LONG_BITS == 32 ? "mips" : "mips64")
                             : (TARGET_LONG_BITS == 32 ? "mipsel" : "mips64el"),
        [QEMU_ARCH_BIT_OPENRISC] = "or1k",
        [QEMU_ARCH_BIT_PPC] = TARGET_LONG_BITS == 32 ? "ppc" : "ppc64",
        [QEMU_ARCH_BIT_RISCV] = TARGET_LONG_BITS == 32 ? "riscv32" : "riscv64",
        [QEMU_ARCH_BIT_RX] = "rx",
        [QEMU_ARCH_BIT_S390X] = "s390x",
        [QEMU_ARCH_BIT_SH4] = TARGET_BIG_ENDIAN ? "sh4eb" : "sh4",
        [QEMU_ARCH_BIT_SPARC] = TARGET_LONG_BITS == 32 ? "sparc" : "sparc64",
        [QEMU_ARCH_BIT_TRICORE] = "tricore",
        [QEMU_ARCH_BIT_XTENSA] = TARGET_BIG_ENDIAN ? "xtensaeb" : "xtensa",
    };

    assert(qemu_arch_bit < ARRAY_SIZE(legacy_target_names));
    assert(legacy_target_names[qemu_arch_bit]);
    return legacy_target_names[qemu_arch_bit];
}

const char *target_name(void)
{
    return qemu_arch_name(QEMU_ARCH_BIT);
}

bool qemu_arch_available(unsigned qemu_arch_mask)
{
    return qemu_arch_mask & BIT(QEMU_ARCH_BIT);
}
