/*
 *  ASPEED OTP (One-Time Programmable) memory
 *
 *  Copyright (C) 2025 Aspeed
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 */

#ifndef ASPEED_OTPMMEM_H
#define ASPEED_OTPMMEM_H

#include "system/memory.h"

#define OTPMEM_SIZE 0x4000
#define OTPMEM_ERR_MAGIC 0x45727200
#define TYPE_ASPEED_OTPMEM "aspeed.otpmem"
OBJECT_DECLARE_SIMPLE_TYPE(AspeedOTPMemState, ASPEED_OTPMEM)

typedef struct AspeedOTPMemOps {
    uint32_t (*read)(AspeedOTPMemState *s, uint32_t addr, Error **errp);
    void (*prog)(AspeedOTPMemState *s, uint32_t addr, uint32_t val, Error **errp);
    void (*set_default)(AspeedOTPMemState *s, uint32_t addr, uint32_t val, Error **errp);
} AspeedOTPMemOps;

typedef struct AspeedOTPMemState {
    DeviceState parent_obj;

    MemoryRegion iomem;
    AddressSpace as;
    size_t size;

    const AspeedOTPMemOps *ops;
    char *otpmem_img_path;
} AspeedOtpmemState;

const AspeedOTPMemOps *aspeed_otpmem_get_ops(AspeedOTPMemState *s);
void aspeed_otpmem_set_backend(AspeedOTPMemState *s, const char *path);
#endif /* ASPEED_OTPMMEM_H */
