/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2025 Loongson Technology Corporation Limited
 */

#ifndef TARGET_LOONGARCH_CSR_H
#define TARGET_LOONGARCH_CSR_H

#include "cpu-csr.h"
#ifdef CONFIG_TCG
#include "tcg/tcg-op.h"
#endif

#ifdef CONFIG_TCG
typedef void (*GenCSRRead)(TCGv dest, TCGv_ptr env);
typedef void (*GenCSRWrite)(TCGv dest, TCGv_ptr env, TCGv src);
#else
typedef void (*GenCSRRead)(void);
typedef void (*GenCSRWrite)(void);
#endif

enum {
    CSRFL_READONLY = (1 << 0),
    CSRFL_EXITTB   = (1 << 1),
    CSRFL_IO       = (1 << 2),
};

typedef struct {
    const char *name;
    int offset;
    int flags;
    GenCSRRead readfn;
    GenCSRWrite writefn;
} CSRInfo;

CSRInfo *get_csr(unsigned int csr_num);
#endif /* TARGET_LOONGARCH_TCG_LOONGARCH_H */
