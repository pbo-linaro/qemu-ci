/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Define target-specific opcode support
 * Copyright (c) 2013 Huawei Technologies Duesseldorf GmbH
 */

#ifndef TCG_TARGET_HAS_H
#define TCG_TARGET_HAS_H

#include "host/cpuinfo.h"

#define have_lse    (cpuinfo & CPUINFO_LSE)
#define have_lse2   (cpuinfo & CPUINFO_LSE2)

/* optional integer instructions */
#define TCG_TARGET_HAS_add2(T)          1
#define TCG_TARGET_HAS_bswap(T)         1
#define TCG_TARGET_HAS_clz(T)           1
#define TCG_TARGET_HAS_ctpop(T)         0
#define TCG_TARGET_HAS_ctz(T)           1
#define TCG_TARGET_HAS_div(T)           1
#define TCG_TARGET_HAS_muls2(T)         0
#define TCG_TARGET_HAS_mulu2(T)         0
#define TCG_TARGET_HAS_mulsh(T)         (T == TCG_TYPE_I64)
#define TCG_TARGET_HAS_muluh(T)         (T == TCG_TYPE_I64)
#define TCG_TARGET_HAS_negsetcond(T)    1
#define TCG_TARGET_HAS_rem(T)           1
#define TCG_TARGET_HAS_rot(T)           1
#define TCG_TARGET_HAS_sub2(T)          1
#define TCG_TARGET_HAS_extract2(T)      1

/* optional integer and vector instructions */
#define TCG_TARGET_HAS_andc(T)          1
#define TCG_TARGET_HAS_eqv(T)           (T <= TCG_TYPE_REG)
#define TCG_TARGET_HAS_nand(T)          0
#define TCG_TARGET_HAS_nor(T)           0
#define TCG_TARGET_HAS_not(T)           1
#define TCG_TARGET_HAS_orc(T)           1

/*
 * Without FEAT_LSE2, we must use LDXP+STXP to implement atomic 128-bit load,
 * which requires writable pages.  We must defer to the helper for user-only,
 * but in system mode all ram is writable for the host.
 */
#ifdef CONFIG_USER_ONLY
#define TCG_TARGET_HAS_qemu_ldst_i128   have_lse2
#else
#define TCG_TARGET_HAS_qemu_ldst_i128   1
#endif

#define TCG_TARGET_HAS_tst              1

#define TCG_TARGET_HAS_v64              1
#define TCG_TARGET_HAS_v128             1
#define TCG_TARGET_HAS_v256             0

#define TCG_TARGET_HAS_neg_vec          1
#define TCG_TARGET_HAS_abs_vec          1
#define TCG_TARGET_HAS_roti_vec         0
#define TCG_TARGET_HAS_rots_vec         0
#define TCG_TARGET_HAS_rotv_vec         0
#define TCG_TARGET_HAS_shi_vec          1
#define TCG_TARGET_HAS_shs_vec          0
#define TCG_TARGET_HAS_shv_vec          1
#define TCG_TARGET_HAS_mul_vec          1
#define TCG_TARGET_HAS_sat_vec          1
#define TCG_TARGET_HAS_minmax_vec       1
#define TCG_TARGET_HAS_bitsel_vec       1
#define TCG_TARGET_HAS_cmpsel_vec       0
#define TCG_TARGET_HAS_tst_vec          1

#define TCG_TARGET_extract_valid(type, ofs, len)   1
#define TCG_TARGET_sextract_valid(type, ofs, len)  1
#define TCG_TARGET_deposit_valid(type, ofs, len)   1

#endif
