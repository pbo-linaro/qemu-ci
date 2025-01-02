/* SPDX-License-Identifier: MIT */
/*
 * Define target-specific opcode support
 * Copyright (c) 2024 Linaro, Ltd.
 */

#ifndef TCG_HAS_H
#define TCG_HAS_H

#include "tcg-target-has.h"

#if TCG_TARGET_REG_BITS == 32
/* Turn some undef macros into false macros.  */
#define TCG_TARGET_HAS_extr_i64_i32     0
#define TCG_TARGET_HAS_ext8s_i64        0
#define TCG_TARGET_HAS_ext16s_i64       0
#define TCG_TARGET_HAS_ext32s_i64       0
#define TCG_TARGET_HAS_ext8u_i64        0
#define TCG_TARGET_HAS_ext16u_i64       0
#define TCG_TARGET_HAS_ext32u_i64       0
#define TCG_TARGET_HAS_deposit_i64      0
#define TCG_TARGET_HAS_extract_i64      0
#define TCG_TARGET_HAS_sextract_i64     0
#define TCG_TARGET_HAS_extract2_i64     0
#define TCG_TARGET_HAS_negsetcond_i64   0
#endif

#ifndef TCG_TARGET_deposit_i32_valid
#define TCG_TARGET_deposit_i32_valid(ofs, len) 1
#endif
#ifndef TCG_TARGET_deposit_i64_valid
#define TCG_TARGET_deposit_i64_valid(ofs, len) 1
#endif
#ifndef TCG_TARGET_extract_i32_valid
#define TCG_TARGET_extract_i32_valid(ofs, len) 1
#endif
#ifndef TCG_TARGET_extract_i64_valid
#define TCG_TARGET_extract_i64_valid(ofs, len) 1
#endif

/* Only one of DIV or DIV2 should be defined.  */
#if defined(TCG_TARGET_HAS_div)
#define TCG_TARGET_HAS_div2(T)          0
#elif defined(TCG_TARGET_HAS_div2)
#define TCG_TARGET_HAS_div(T)           0
#define TCG_TARGET_HAS_rem(T)           0
#endif

#if !defined(TCG_TARGET_HAS_v64) \
    && !defined(TCG_TARGET_HAS_v128) \
    && !defined(TCG_TARGET_HAS_v256)
#define TCG_TARGET_MAYBE_vec            0
#define TCG_TARGET_HAS_abs_vec          0
#define TCG_TARGET_HAS_neg_vec          0
#define TCG_TARGET_HAS_roti_vec         0
#define TCG_TARGET_HAS_rots_vec         0
#define TCG_TARGET_HAS_rotv_vec         0
#define TCG_TARGET_HAS_shi_vec          0
#define TCG_TARGET_HAS_shs_vec          0
#define TCG_TARGET_HAS_shv_vec          0
#define TCG_TARGET_HAS_mul_vec          0
#define TCG_TARGET_HAS_sat_vec          0
#define TCG_TARGET_HAS_minmax_vec       0
#define TCG_TARGET_HAS_bitsel_vec       0
#define TCG_TARGET_HAS_cmpsel_vec       0
#define TCG_TARGET_HAS_tst_vec          0
#else
#define TCG_TARGET_MAYBE_vec            1
#endif
#ifndef TCG_TARGET_HAS_v64
#define TCG_TARGET_HAS_v64              0
#endif
#ifndef TCG_TARGET_HAS_v128
#define TCG_TARGET_HAS_v128             0
#endif
#ifndef TCG_TARGET_HAS_v256
#define TCG_TARGET_HAS_v256             0
#endif

#endif
