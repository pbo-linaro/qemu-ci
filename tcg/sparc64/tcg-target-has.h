/* SPDX-License-Identifier: MIT */
/*
 * Define target-specific opcode support
 * Copyright (c) 2008 Fabrice Bellard
 */

#ifndef TCG_TARGET_HAS_H
#define TCG_TARGET_HAS_H

#if defined(__VIS__) && __VIS__ >= 0x300
#define use_vis3_instructions  1
#else
extern bool use_vis3_instructions;
#endif

/* optional integer instructions */
#define TCG_TARGET_HAS_add2(T)          1
#define TCG_TARGET_HAS_bswap(T)         0
#define TCG_TARGET_HAS_clz(T)           0
#define TCG_TARGET_HAS_ctpop(T)         0
#define TCG_TARGET_HAS_ctz(T)           0
#define TCG_TARGET_HAS_div(T)           1
#define TCG_TARGET_HAS_muls2(T)         (T == TCG_TYPE_I32)
#define TCG_TARGET_HAS_mulu2(T)         (T == TCG_TYPE_I32)
#define TCG_TARGET_HAS_mulsh(T)         0
#define TCG_TARGET_HAS_muluh(T)         (T == TCG_TYPE_I64 && use_vis3_instructions)
#define TCG_TARGET_HAS_negsetcond(T)    1
#define TCG_TARGET_HAS_rem(T)           0
#define TCG_TARGET_HAS_rot(T)           0
#define TCG_TARGET_HAS_sub2(T)          1
#define TCG_TARGET_HAS_extract2(T)      0

/* optional integer and vector instructions */
#define TCG_TARGET_HAS_andc(T)          1
#define TCG_TARGET_HAS_eqv(T)           0
#define TCG_TARGET_HAS_nand(T)          0
#define TCG_TARGET_HAS_nor(T)           0
#define TCG_TARGET_HAS_not(T)           1
#define TCG_TARGET_HAS_orc(T)           1

#define TCG_TARGET_HAS_ext8s_i32        0
#define TCG_TARGET_HAS_ext16s_i32       0
#define TCG_TARGET_HAS_ext8u_i32        0
#define TCG_TARGET_HAS_ext16u_i32       0
#define TCG_TARGET_HAS_qemu_st8_i32     0

#define TCG_TARGET_HAS_extr_i64_i32     0
#define TCG_TARGET_HAS_ext8s_i64        0
#define TCG_TARGET_HAS_ext16s_i64       0
#define TCG_TARGET_HAS_ext32s_i64       1
#define TCG_TARGET_HAS_ext8u_i64        0
#define TCG_TARGET_HAS_ext16u_i64       0
#define TCG_TARGET_HAS_ext32u_i64       1

#define TCG_TARGET_HAS_qemu_ldst_i128   0

#define TCG_TARGET_HAS_tst              1

#define TCG_TARGET_extract_valid(type, ofs, len) \
    ((type) == TCG_TYPE_I64 && (ofs) + (len) == 32)

#define TCG_TARGET_sextract_valid  TCG_TARGET_extract_valid

#define TCG_TARGET_deposit_valid(type, ofs, len) 0

#endif
