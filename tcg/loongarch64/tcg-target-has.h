/* SPDX-License-Identifier: MIT */
/*
 * Define target-specific opcode support
 * Copyright (c) 2021 WANG Xuerui <git@xen0n.name>
 */

#ifndef TCG_TARGET_HAS_H
#define TCG_TARGET_HAS_H

#include "host/cpuinfo.h"

/* optional integer instructions */
#define TCG_TARGET_HAS_bswap(T)         1
#define TCG_TARGET_HAS_clz(T)           1
#define TCG_TARGET_HAS_ctpop(T)         0
#define TCG_TARGET_HAS_ctz(T)           1
#define TCG_TARGET_HAS_div(T)           1
#define TCG_TARGET_HAS_muls2(T)         0
#define TCG_TARGET_HAS_mulu2(T)         0
#define TCG_TARGET_HAS_mulsh(T)         1
#define TCG_TARGET_HAS_muluh(T)         1
#define TCG_TARGET_HAS_rem(T)           1
#define TCG_TARGET_HAS_rot(T)           1

/* optional integer and vector instructions */
#define TCG_TARGET_HAS_andc(T)          1
#define TCG_TARGET_HAS_eqv(T)           0
#define TCG_TARGET_HAS_nand(T)          0
#define TCG_TARGET_HAS_nor(T)           1
#define TCG_TARGET_HAS_not(T)           1
#define TCG_TARGET_HAS_orc(T)           1

#define TCG_TARGET_HAS_negsetcond_i32   0
#define TCG_TARGET_HAS_deposit_i32      1
#define TCG_TARGET_HAS_extract_i32      1
#define TCG_TARGET_HAS_sextract_i32     0
#define TCG_TARGET_HAS_extract2_i32     0
#define TCG_TARGET_HAS_add2_i32         0
#define TCG_TARGET_HAS_sub2_i32         0
#define TCG_TARGET_HAS_ext8s_i32        1
#define TCG_TARGET_HAS_ext16s_i32       1
#define TCG_TARGET_HAS_ext8u_i32        1
#define TCG_TARGET_HAS_ext16u_i32       1
#define TCG_TARGET_HAS_brcond2          0
#define TCG_TARGET_HAS_setcond2         0
#define TCG_TARGET_HAS_qemu_st8_i32     0

/* 64-bit operations */
#define TCG_TARGET_HAS_negsetcond_i64   0
#define TCG_TARGET_HAS_deposit_i64      1
#define TCG_TARGET_HAS_extract_i64      1
#define TCG_TARGET_HAS_sextract_i64     0
#define TCG_TARGET_HAS_extract2_i64     0
#define TCG_TARGET_HAS_extr_i64_i32     1
#define TCG_TARGET_HAS_ext8s_i64        1
#define TCG_TARGET_HAS_ext16s_i64       1
#define TCG_TARGET_HAS_ext32s_i64       1
#define TCG_TARGET_HAS_ext8u_i64        1
#define TCG_TARGET_HAS_ext16u_i64       1
#define TCG_TARGET_HAS_ext32u_i64       1
#define TCG_TARGET_HAS_add2_i64         0
#define TCG_TARGET_HAS_sub2_i64         0

#define TCG_TARGET_HAS_qemu_ldst_i128   (cpuinfo & CPUINFO_LSX)

#define TCG_TARGET_HAS_tst              0

#define TCG_TARGET_HAS_v64              (cpuinfo & CPUINFO_LSX)
#define TCG_TARGET_HAS_v128             (cpuinfo & CPUINFO_LSX)
#define TCG_TARGET_HAS_v256             (cpuinfo & CPUINFO_LASX)

#define TCG_TARGET_HAS_neg_vec          1
#define TCG_TARGET_HAS_abs_vec          0
#define TCG_TARGET_HAS_mul_vec          1
#define TCG_TARGET_HAS_shi_vec          1
#define TCG_TARGET_HAS_shs_vec          0
#define TCG_TARGET_HAS_shv_vec          1
#define TCG_TARGET_HAS_roti_vec         1
#define TCG_TARGET_HAS_rots_vec         0
#define TCG_TARGET_HAS_rotv_vec         1
#define TCG_TARGET_HAS_sat_vec          1
#define TCG_TARGET_HAS_minmax_vec       1
#define TCG_TARGET_HAS_bitsel_vec       1
#define TCG_TARGET_HAS_cmpsel_vec       0
#define TCG_TARGET_HAS_tst_vec          0


#endif
