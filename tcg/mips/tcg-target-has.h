/* SPDX-License-Identifier: MIT */
/*
 * Define target-specific opcode support
 * Copyright (c) 2008-2009 Arnaud Patard <arnaud.patard@rtp-net.org>
 * Copyright (c) 2009 Aurelien Jarno <aurelien@aurel32.net>
 */

#ifndef TCG_TARGET_HAS_H
#define TCG_TARGET_HAS_H

/* MOVN/MOVZ instructions detection */
#if (defined(__mips_isa_rev) && (__mips_isa_rev >= 1)) || \
    defined(_MIPS_ARCH_LOONGSON2E) || defined(_MIPS_ARCH_LOONGSON2F) || \
    defined(_MIPS_ARCH_MIPS4)
#define use_movnz_instructions  1
#else
extern bool use_movnz_instructions;
#endif

/* MIPS32 instruction set detection */
#if defined(__mips_isa_rev) && (__mips_isa_rev >= 1)
#define use_mips32_instructions  1
#else
extern bool use_mips32_instructions;
#endif

/* MIPS32R2 instruction set detection */
#if defined(__mips_isa_rev) && (__mips_isa_rev >= 2)
#define use_mips32r2_instructions  1
#else
extern bool use_mips32r2_instructions;
#endif

/* MIPS32R6 instruction set detection */
#if defined(__mips_isa_rev) && (__mips_isa_rev >= 6)
#define use_mips32r6_instructions  1
#else
#define use_mips32r6_instructions  0
#endif

/* optional integer instructions */
#define TCG_TARGET_HAS_add2(T)          (TCG_TARGET_REG_BITS == 32)
#define TCG_TARGET_HAS_bswap(T)         1
#define TCG_TARGET_HAS_clz(T)           use_mips32r2_instructions
#define TCG_TARGET_HAS_ctpop(T)         0
#define TCG_TARGET_HAS_ctz(T)           0
#define TCG_TARGET_HAS_div(T)           1
#define TCG_TARGET_HAS_muls2(T)         (!use_mips32r6_instructions)
#define TCG_TARGET_HAS_mulu2(T)         (!use_mips32r6_instructions)
#define TCG_TARGET_HAS_mulsh(T)         1
#define TCG_TARGET_HAS_muluh(T)         1
#define TCG_TARGET_HAS_negsetcond(T)    0
#define TCG_TARGET_HAS_rem(T)           1
#define TCG_TARGET_HAS_rot(T)           use_mips32r2_instructions
#define TCG_TARGET_HAS_sub2(T)          (TCG_TARGET_REG_BITS == 32)
#define TCG_TARGET_HAS_extract2(T)      0

/* optional integer and vector instructions */
#define TCG_TARGET_HAS_andc(T)          0
#define TCG_TARGET_HAS_eqv(T)           0
#define TCG_TARGET_HAS_nand(T)          0
#define TCG_TARGET_HAS_nor(T)           1
#define TCG_TARGET_HAS_not(T)           1
#define TCG_TARGET_HAS_orc(T)           0

#if TCG_TARGET_REG_BITS == 64
#define TCG_TARGET_HAS_extr_i64_i32     1
#define TCG_TARGET_HAS_ext32s_i64       1
#define TCG_TARGET_HAS_ext32u_i64       1
#endif

/* optional instructions detected at runtime */
#define TCG_TARGET_HAS_deposit_i32      use_mips32r2_instructions
#define TCG_TARGET_HAS_extract_i32      use_mips32r2_instructions
#define TCG_TARGET_HAS_sextract_i32     0
#define TCG_TARGET_HAS_extract2_i32     0
#define TCG_TARGET_HAS_ext8s_i32        use_mips32r2_instructions
#define TCG_TARGET_HAS_ext16s_i32       use_mips32r2_instructions
#define TCG_TARGET_HAS_qemu_st8_i32     0

#if TCG_TARGET_REG_BITS == 64
#define TCG_TARGET_HAS_deposit_i64      use_mips32r2_instructions
#define TCG_TARGET_HAS_extract_i64      use_mips32r2_instructions
#define TCG_TARGET_HAS_sextract_i64     0
#define TCG_TARGET_HAS_extract2_i64     0
#define TCG_TARGET_HAS_ext8s_i64        use_mips32r2_instructions
#define TCG_TARGET_HAS_ext16s_i64       use_mips32r2_instructions
#endif

/* optional instructions automatically implemented */
#define TCG_TARGET_HAS_ext8u_i32        0 /* andi rt, rs, 0xff   */
#define TCG_TARGET_HAS_ext16u_i32       0 /* andi rt, rs, 0xffff */

#if TCG_TARGET_REG_BITS == 64
#define TCG_TARGET_HAS_ext8u_i64        0 /* andi rt, rs, 0xff   */
#define TCG_TARGET_HAS_ext16u_i64       0 /* andi rt, rs, 0xffff */
#endif

#define TCG_TARGET_HAS_qemu_ldst_i128   0
#define TCG_TARGET_HAS_tst              0

#endif
