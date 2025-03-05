#ifndef QEMU_ARCH_INIT_H
#define QEMU_ARCH_INIT_H

#include "qemu/bitops.h"

typedef enum QemuArchBit {
    QEMU_ARCH_BIT_ALPHA         = 0,
    QEMU_ARCH_BIT_ARM           = 1,
    QEMU_ARCH_BIT_I386          = 3,
    QEMU_ARCH_BIT_M68K          = 4,
    QEMU_ARCH_BIT_MICROBLAZE    = 6,
    QEMU_ARCH_BIT_MIPS          = 7,
    QEMU_ARCH_BIT_PPC           = 8,
    QEMU_ARCH_BIT_S390X         = 9,
    QEMU_ARCH_BIT_SH4           = 10,
    QEMU_ARCH_BIT_SPARC         = 11,
    QEMU_ARCH_BIT_XTENSA        = 12,
    QEMU_ARCH_BIT_OPENRISC      = 13,
    QEMU_ARCH_BIT_TRICORE       = 16,
    QEMU_ARCH_BIT_HPPA          = 18,
    QEMU_ARCH_BIT_RISCV         = 19,
    QEMU_ARCH_BIT_RX            = 20,
    QEMU_ARCH_BIT_AVR           = 21,
    QEMU_ARCH_BIT_HEXAGON       = 22,
    QEMU_ARCH_BIT_LOONGARCH     = 23,
} QemuArchBit;

#define QEMU_ARCH_ALPHA         BIT(QEMU_ARCH_BIT_ALPHA)
#define QEMU_ARCH_ARM           BIT(QEMU_ARCH_BIT_ARM)
#define QEMU_ARCH_I386          BIT(QEMU_ARCH_BIT_I386)
#define QEMU_ARCH_M68K          BIT(QEMU_ARCH_BIT_M68K)
#define QEMU_ARCH_MICROBLAZE    BIT(QEMU_ARCH_BIT_MICROBLAZE)
#define QEMU_ARCH_MIPS          BIT(QEMU_ARCH_BIT_MIPS)
#define QEMU_ARCH_PPC           BIT(QEMU_ARCH_BIT_PPC)
#define QEMU_ARCH_S390X         BIT(QEMU_ARCH_BIT_S390X)
#define QEMU_ARCH_SH4           BIT(QEMU_ARCH_BIT_SH4)
#define QEMU_ARCH_SPARC         BIT(QEMU_ARCH_BIT_SPARC)
#define QEMU_ARCH_XTENSA        BIT(QEMU_ARCH_BIT_XTENSA)
#define QEMU_ARCH_OPENRISC      BIT(QEMU_ARCH_BIT_OPENRISC)
#define QEMU_ARCH_TRICORE       BIT(QEMU_ARCH_BIT_TRICORE)
#define QEMU_ARCH_HPPA          BIT(QEMU_ARCH_BIT_HPPA)
#define QEMU_ARCH_RISCV         BIT(QEMU_ARCH_BIT_RISCV)
#define QEMU_ARCH_RX            BIT(QEMU_ARCH_BIT_RX)
#define QEMU_ARCH_AVR           BIT(QEMU_ARCH_BIT_AVR)
#define QEMU_ARCH_HEXAGON       BIT(QEMU_ARCH_BIT_HEXAGON)
#define QEMU_ARCH_LOONGARCH     BIT(QEMU_ARCH_BIT_LOONGARCH)
#define QEMU_ARCH_ALL           -1

bool qemu_arch_available(unsigned qemu_arch_mask);

#endif
