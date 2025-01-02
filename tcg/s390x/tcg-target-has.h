/* SPDX-License-Identifier: MIT */
/*
 * Define target-specific opcode support
 * Copyright (c) 2009 Ulrich Hecht <uli@suse.de>
 */

#ifndef TCG_TARGET_HAS_H
#define TCG_TARGET_HAS_H

/* Facilities required for proper operation; checked at startup. */

#define FACILITY_ZARCH_ACTIVE         2
#define FACILITY_LONG_DISP            18
#define FACILITY_EXT_IMM              21
#define FACILITY_GEN_INST_EXT         34
#define FACILITY_45                   45

/* Facilities that are checked at runtime. */

#define FACILITY_LOAD_ON_COND2        53
#define FACILITY_MISC_INSN_EXT2       58
#define FACILITY_MISC_INSN_EXT3       61
#define FACILITY_VECTOR               129
#define FACILITY_VECTOR_ENH1          135

extern uint64_t s390_facilities[3];

#define HAVE_FACILITY(X) \
    ((s390_facilities[FACILITY_##X / 64] >> (63 - FACILITY_##X % 64)) & 1)

/* optional integer instructions */
#define TCG_TARGET_HAS_add2(T)        1
#define TCG_TARGET_HAS_bswap(T)       1
#define TCG_TARGET_HAS_clz(T)         (T == TCG_TYPE_I64)
#define TCG_TARGET_HAS_ctpop(T)       1
#define TCG_TARGET_HAS_ctz(T)         0
#define TCG_TARGET_HAS_div2(T)        1
#define TCG_TARGET_HAS_muls2(T)       (T == TCG_TYPE_I64 && HAVE_FACILITY(MISC_INSN_EXT2))
#define TCG_TARGET_HAS_mulu2(T)       (T == TCG_TYPE_I64)
#define TCG_TARGET_HAS_mulsh(T)       0
#define TCG_TARGET_HAS_muluh(T)       0
#define TCG_TARGET_HAS_negsetcond(T)  1
#define TCG_TARGET_HAS_rot(T)         1
#define TCG_TARGET_HAS_sub2(T)        1
#define TCG_TARGET_HAS_extract2(T)    0

/* optional integer and vector instructions */
#define TCG_TARGET_HAS_andc(T)        (T >= TCG_TYPE_V64 || HAVE_FACILITY(MISC_INSN_EXT3))
#define TCG_TARGET_HAS_eqv(T)         (T >= TCG_TYPE_V64 ? HAVE_FACILITY(VECTOR_ENH1) : HAVE_FACILITY(MISC_INSN_EXT3))
#define TCG_TARGET_HAS_nand(T)        (T >= TCG_TYPE_V64 ? HAVE_FACILITY(VECTOR_ENH1) : HAVE_FACILITY(MISC_INSN_EXT3))
#define TCG_TARGET_HAS_nor(T)         (T >= TCG_TYPE_V64 || HAVE_FACILITY(MISC_INSN_EXT3))
#define TCG_TARGET_HAS_not(T)         (T >= TCG_TYPE_V64 || HAVE_FACILITY(MISC_INSN_EXT3))
#define TCG_TARGET_HAS_orc(T)         (T >= TCG_TYPE_V64 ? HAVE_FACILITY(VECTOR_ENH1) : HAVE_FACILITY(MISC_INSN_EXT3))

#define TCG_TARGET_HAS_ext8s_i32      1
#define TCG_TARGET_HAS_ext16s_i32     1
#define TCG_TARGET_HAS_ext8u_i32      1
#define TCG_TARGET_HAS_ext16u_i32     1
#define TCG_TARGET_HAS_qemu_st8_i32   0

#define TCG_TARGET_HAS_ext8s_i64      1
#define TCG_TARGET_HAS_ext16s_i64     1
#define TCG_TARGET_HAS_ext32s_i64     1
#define TCG_TARGET_HAS_ext8u_i64      1
#define TCG_TARGET_HAS_ext16u_i64     1
#define TCG_TARGET_HAS_ext32u_i64     1

#define TCG_TARGET_HAS_qemu_ldst_i128 1

#define TCG_TARGET_HAS_tst            1

#define TCG_TARGET_HAS_v64            HAVE_FACILITY(VECTOR)
#define TCG_TARGET_HAS_v128           HAVE_FACILITY(VECTOR)
#define TCG_TARGET_HAS_v256           0

#define TCG_TARGET_HAS_neg_vec        1
#define TCG_TARGET_HAS_abs_vec        1
#define TCG_TARGET_HAS_roti_vec       1
#define TCG_TARGET_HAS_rots_vec       1
#define TCG_TARGET_HAS_rotv_vec       1
#define TCG_TARGET_HAS_shi_vec        1
#define TCG_TARGET_HAS_shs_vec        1
#define TCG_TARGET_HAS_shv_vec        1
#define TCG_TARGET_HAS_mul_vec        1
#define TCG_TARGET_HAS_sat_vec        0
#define TCG_TARGET_HAS_minmax_vec     1
#define TCG_TARGET_HAS_bitsel_vec     1
#define TCG_TARGET_HAS_cmpsel_vec     1
#define TCG_TARGET_HAS_tst_vec        0

#define TCG_TARGET_extract_valid(type, ofs, len)   1
#define TCG_TARGET_deposit_valid(type, ofs, len)   1

static inline bool
tcg_target_sextract_valid(TCGType type, unsigned ofs, unsigned len)
{
    if (ofs == 0) {
        switch (len) {
        case 8:
        case 16:
            return true;
        case 32:
            return type == TCG_TYPE_I64;
        }
    }
    return false;
}
#define TCG_TARGET_sextract_valid  tcg_target_sextract_valid

#endif
