/* SPDX-License-Identifier: MIT */
/*
 * Define target-specific opcode support
 * Copyright (c) 2009, 2011 Stefan Weil
 */

#ifndef TCG_TARGET_HAS_H
#define TCG_TARGET_HAS_H

/* optional integer instructions */
#define TCG_TARGET_HAS_add2(T)          1
#define TCG_TARGET_HAS_bswap(T)         1
#define TCG_TARGET_HAS_clz(T)           1
#define TCG_TARGET_HAS_ctpop(T)         1
#define TCG_TARGET_HAS_ctz(T)           1
#define TCG_TARGET_HAS_div(T)           1
#define TCG_TARGET_HAS_muls2(T)         1
#define TCG_TARGET_HAS_mulu2(T)         1
#define TCG_TARGET_HAS_mulsh(T)         0
#define TCG_TARGET_HAS_muluh(T)         0
#define TCG_TARGET_HAS_negsetcond(T)    0
#define TCG_TARGET_HAS_rem(T)           1
#define TCG_TARGET_HAS_rot(T)           1
#define TCG_TARGET_HAS_sub2(T)          1
#define TCG_TARGET_HAS_extract2(T)      0

/* optional integer and vector instructions */
#define TCG_TARGET_HAS_andc(T)          1
#define TCG_TARGET_HAS_eqv(T)           1
#define TCG_TARGET_HAS_nand(T)          1
#define TCG_TARGET_HAS_nor(T)           1
#define TCG_TARGET_HAS_not(T)           1
#define TCG_TARGET_HAS_orc(T)           1

#define TCG_TARGET_HAS_qemu_st8_i32     0
#define TCG_TARGET_HAS_qemu_ldst_i128   0
#define TCG_TARGET_HAS_tst              1

#define TCG_TARGET_extract_valid(type, ofs, len)   1
#define TCG_TARGET_sextract_valid(type, ofs, len)  1
#define TCG_TARGET_deposit_valid(type, ofs, len)   1

#endif
