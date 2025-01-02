/* SPDX-License-Identifier: MIT */
/*
 * Define target-specific opcode support
 * Copyright (c) 2018 SiFive, Inc
 */

#ifndef TCG_TARGET_HAS_H
#define TCG_TARGET_HAS_H

#include "host/cpuinfo.h"

/* optional integer instructions */
#define TCG_TARGET_HAS_add2(T)          1
#define TCG_TARGET_HAS_bswap(T)         (cpuinfo & CPUINFO_ZBB)
#define TCG_TARGET_HAS_clz(T)           (cpuinfo & CPUINFO_ZBB)
#define TCG_TARGET_HAS_ctpop(T)         (cpuinfo & CPUINFO_ZBB)
#define TCG_TARGET_HAS_ctz(T)           (cpuinfo & CPUINFO_ZBB)
#define TCG_TARGET_HAS_div(T)           1
#define TCG_TARGET_HAS_muls2(T)         0
#define TCG_TARGET_HAS_mulu2(T)         0
#define TCG_TARGET_HAS_mulsh(T)         (T == TCG_TYPE_I64)
#define TCG_TARGET_HAS_muluh(T)         (T == TCG_TYPE_I64)
#define TCG_TARGET_HAS_negsetcond(T)    1
#define TCG_TARGET_HAS_rem(T)           1
#define TCG_TARGET_HAS_rot(T)           (cpuinfo & CPUINFO_ZBB)
#define TCG_TARGET_HAS_sub2(T)          1

/* optional integer and vector instructions */
#define TCG_TARGET_HAS_andc(T)          (T <= TCG_TYPE_REG && (cpuinfo & CPUINFO_ZBB))
#define TCG_TARGET_HAS_eqv(T)           (T <= TCG_TYPE_REG && (cpuinfo & CPUINFO_ZBB))
#define TCG_TARGET_HAS_nand(T)          0
#define TCG_TARGET_HAS_nor(T)           0
#define TCG_TARGET_HAS_not(T)           1
#define TCG_TARGET_HAS_orc(T)           (T <= TCG_TYPE_REG && (cpuinfo & CPUINFO_ZBB))

#define TCG_TARGET_HAS_deposit_i32      0
#define TCG_TARGET_HAS_extract_i32      1
#define TCG_TARGET_HAS_sextract_i32     1
#define TCG_TARGET_HAS_extract2_i32     0
#define TCG_TARGET_HAS_ext8s_i32        1
#define TCG_TARGET_HAS_ext16s_i32       1
#define TCG_TARGET_HAS_ext8u_i32        1
#define TCG_TARGET_HAS_ext16u_i32       1
#define TCG_TARGET_HAS_brcond2          1
#define TCG_TARGET_HAS_setcond2         1
#define TCG_TARGET_HAS_qemu_st8_i32     0

#define TCG_TARGET_HAS_deposit_i64      0
#define TCG_TARGET_HAS_extract_i64      1
#define TCG_TARGET_HAS_sextract_i64     1
#define TCG_TARGET_HAS_extract2_i64     0
#define TCG_TARGET_HAS_extr_i64_i32     1
#define TCG_TARGET_HAS_ext8s_i64        1
#define TCG_TARGET_HAS_ext16s_i64       1
#define TCG_TARGET_HAS_ext32s_i64       1
#define TCG_TARGET_HAS_ext8u_i64        1
#define TCG_TARGET_HAS_ext16u_i64       1
#define TCG_TARGET_HAS_ext32u_i64       1

#define TCG_TARGET_HAS_qemu_ldst_i128   0

#define TCG_TARGET_HAS_tst              0

/* vector instructions */
#define TCG_TARGET_HAS_v64              (cpuinfo & CPUINFO_ZVE64X)
#define TCG_TARGET_HAS_v128             (cpuinfo & CPUINFO_ZVE64X)
#define TCG_TARGET_HAS_v256             (cpuinfo & CPUINFO_ZVE64X)
#define TCG_TARGET_HAS_neg_vec          1
#define TCG_TARGET_HAS_abs_vec          0
#define TCG_TARGET_HAS_roti_vec         1
#define TCG_TARGET_HAS_rots_vec         1
#define TCG_TARGET_HAS_rotv_vec         1
#define TCG_TARGET_HAS_shi_vec          1
#define TCG_TARGET_HAS_shs_vec          1
#define TCG_TARGET_HAS_shv_vec          1
#define TCG_TARGET_HAS_mul_vec          1
#define TCG_TARGET_HAS_sat_vec          1
#define TCG_TARGET_HAS_minmax_vec       1
#define TCG_TARGET_HAS_bitsel_vec       0
#define TCG_TARGET_HAS_cmpsel_vec       1

#define TCG_TARGET_HAS_tst_vec          0

static inline bool
tcg_target_extract_valid(TCGType type, unsigned ofs, unsigned len)
{
    if (type == TCG_TYPE_I64 && ofs + len == 32) {
        /* ofs > 0 uses SRLIW; ofs == 0 uses add.uw. */
        return ofs || (cpuinfo & CPUINFO_ZBA);
    }
    return (cpuinfo & CPUINFO_ZBB) && ofs == 0 && len == 16;
}
#define TCG_TARGET_extract_valid  tcg_target_extract_valid

static inline bool
tcg_target_sextract_valid(TCGType type, unsigned ofs, unsigned len)
{
    if (type == TCG_TYPE_I64 && ofs + len == 32) {
        return true;
    }
    return (cpuinfo & CPUINFO_ZBB) && ofs == 0 && (len == 8 || len == 16);
}
#define TCG_TARGET_sextract_valid  tcg_target_sextract_valid

#endif
