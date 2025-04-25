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

/* optional instructions */
#define TCG_TARGET_HAS_bswap16_i32      0
#define TCG_TARGET_HAS_bswap32_i32      0
#define TCG_TARGET_HAS_clz_i32          0
#define TCG_TARGET_HAS_ctz_i32          0
#define TCG_TARGET_HAS_ctpop_i32        0
#define TCG_TARGET_HAS_extract2_i32     0
#define TCG_TARGET_HAS_negsetcond_i32   1
#define TCG_TARGET_HAS_add2_i32         1
#define TCG_TARGET_HAS_sub2_i32         1
#define TCG_TARGET_HAS_mulu2_i32        1
#define TCG_TARGET_HAS_muls2_i32        1
#define TCG_TARGET_HAS_qemu_st8_i32     0

#define TCG_TARGET_HAS_extr_i64_i32     0
#define TCG_TARGET_HAS_bswap16_i64      0
#define TCG_TARGET_HAS_bswap32_i64      0
#define TCG_TARGET_HAS_bswap64_i64      0
#define TCG_TARGET_HAS_clz_i64          0
#define TCG_TARGET_HAS_ctz_i64          0
#define TCG_TARGET_HAS_ctpop_i64        0
#define TCG_TARGET_HAS_extract2_i64     0
#define TCG_TARGET_HAS_negsetcond_i64   1
#define TCG_TARGET_HAS_add2_i64         1
#define TCG_TARGET_HAS_sub2_i64         1
#define TCG_TARGET_HAS_mulu2_i64        0
#define TCG_TARGET_HAS_muls2_i64        0

#define TCG_TARGET_HAS_qemu_ldst_i128   0

#define TCG_TARGET_HAS_tst              1

#define TCG_TARGET_extract_valid(type, ofs, len) \
    ((type) == TCG_TYPE_I64 && (ofs) + (len) == 32)

#define TCG_TARGET_sextract_valid  TCG_TARGET_extract_valid

#define TCG_TARGET_deposit_valid(type, ofs, len) 0

#endif
