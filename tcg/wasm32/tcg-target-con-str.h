/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
/*
 * Define constraint letters for register sets:
 * REGS(letter, register_mask)
 */
REGS('r', MAKE_64BIT_MASK(0, TCG_TARGET_NB_REGS))
