/*
 * Register access utilities for device tests.
 *
 * Copyright (C) 2024 Google LLC
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#ifndef _REG_UTILS_H
#define _REG_UTILS_H

#include "libqtest-single.h"
#include "hw/registerfields.h"

#ifdef DEBUG_REG
#define debug(fmt, args...) fprintf(stderr, fmt, ## args)
#else
#define debug(fmt, args...)
#endif

#define _REG_OFF(mod, reg) (A_##mod##_##reg)

#define REG32_READ(mod, reg)                                            \
    ({                                                                  \
        uint32_t value;                                                 \
        value = readl(mod##_BASE + _REG_OFF(mod, reg));                 \
        debug("[%s] -> %08x\n", #reg, value);                           \
        value;                                                          \
    })

#define REG32_WRITE(mod, reg, value)                                    \
    do {                                                                \
        debug("[%s] <- %08x\n", #reg, value);                           \
        writel(mod##_BASE + _REG_OFF(mod, reg), value);                 \
    } while (0)

#define REG_FIELD_VAL(v, mod, reg, field)                               \
    FIELD_EX32(v, mod##_##reg, field)                                   \

#define REG32_READ_FIELD(mod, reg, field)                   \
    REG_FIELD_VAL(REG32_READ(mod, reg), mod, reg, field)

#define REG32_WRITE_FIELD(mod, reg, field, val)                         \
    do {                                                                \
        uint32_t _tmp = REG32_READ(mod, reg);                           \
        _tmp = FIELD_DP32(_tmp, mod##_##reg, field, val);               \
        REG32_WRITE(mod, reg, _tmp);                                    \
    } while (0)

#define REG32_WRITE_FIELD_NOUPDATE(mod, reg, field, val)                \
    do {                                                                \
        uint32_t _tmp = FIELD_DP32(0, mod##_##reg, field, val);         \
        REG32_WRITE(mod, reg, _tmp);                                    \
    } while (0)

#define WAIT_REG32_FIELD(ns, mod, reg, field, val)                      \
    do {                                                                \
        clock_step(ns);                                                 \
        g_assert_cmpuint(REG32_READ_FIELD(mod, reg, field), ==, val);   \
    } while (0)

#define REG32_READ_FAIL(mod, reg) \
    readl_fail(mod##_BASE + _REG_OFF(mod, reg))

#define REG32_WRITE_FAIL(mod, reg, value) \
    writel_fail(mod##_BASE + _REG_OFF(mod, reg), value)

#endif /* _REG_UTILS_H */
