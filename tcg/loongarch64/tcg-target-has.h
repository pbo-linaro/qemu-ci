/* SPDX-License-Identifier: MIT */
/*
 * Define target-specific opcode support
 * Copyright (c) 2021 WANG Xuerui <git@xen0n.name>
 */

#ifndef TCG_TARGET_HAS_H
#define TCG_TARGET_HAS_H

#include "host/cpuinfo.h"

/* optional integer instructions */
#define TCG_TARGET_HAS_add2(T)          0
#define TCG_TARGET_HAS_bswap(T)         1
#define TCG_TARGET_HAS_clz(T)           1
#define TCG_TARGET_HAS_ctpop(T)         0
#define TCG_TARGET_HAS_ctz(T)           1
#define TCG_TARGET_HAS_div(T)           1
#define TCG_TARGET_HAS_muls2(T)         0
#define TCG_TARGET_HAS_mulu2(T)         0
#define TCG_TARGET_HAS_mulsh(T)         1
#define TCG_TARGET_HAS_muluh(T)         1
#define TCG_TARGET_HAS_negsetcond(T)    0
#define TCG_TARGET_HAS_rem(T)           1
#define TCG_TARGET_HAS_rot(T)           1
#define TCG_TARGET_HAS_sub2(T)          0
#define TCG_TARGET_HAS_extract2(T)      0

/* optional integer and vector instructions */
#define TCG_TARGET_HAS_andc(T)          1
#define TCG_TARGET_HAS_eqv(T)           0
#define TCG_TARGET_HAS_nand(T)          0
#define TCG_TARGET_HAS_nor(T)           1
#define TCG_TARGET_HAS_not(T)           1
#define TCG_TARGET_HAS_orc(T)           1

#define TCG_TARGET_HAS_qemu_st8_i32     0
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

#define TCG_TARGET_extract_valid(type, ofs, len)   1
#define TCG_TARGET_deposit_valid(type, ofs, len)   1

static inline bool
tcg_target_sextract_valid(TCGType type, unsigned ofs, unsigned len)
{
    if (type == TCG_TYPE_I64 && ofs + len == 32) {
        return true;
    }
    return ofs == 0 && (len == 8 || len == 16);
}
#define TCG_TARGET_sextract_valid  tcg_target_sextract_valid

#endif
