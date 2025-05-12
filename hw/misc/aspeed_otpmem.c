/*
 *  ASPEED OTP (One-Time Programmable) memory
 *
 *  Copyright (C) 2025 Aspeed
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "trace.h"
#include "hw/misc/aspeed_otpmem.h"

void aspeed_otpmem_set_backend(AspeedOTPMemState *s, const char *path)
{
    g_free(s->otpmem_img_path);
    s->otpmem_img_path = g_strdup(path);
}

static void aspeed_otpmem_sync_region(AspeedOTPMemState *s,
                                      hwaddr offset, hwaddr size)
{
    memory_region_msync(&s->iomem, offset, size);
}

static uint32_t aspeed_otpmem_read(AspeedOtpmemState *s,
                                   uint32_t addr, Error **errp)
{
    uint32_t val = 0;
    MemTxResult ret;

    ret = address_space_read(&s->as, addr, MEMTXATTRS_UNSPECIFIED,
                             (uint8_t *)&val, sizeof(val));
    if (ret != MEMTX_OK) {
        error_setg(errp, "Failed to read data from 0x%x", addr);
        return OTPMEM_ERR_MAGIC;
    }
    return val;
}

static bool valid_program_data(uint32_t otp_addr,
                                 uint32_t value, uint32_t prog_bit)
{
    uint32_t programmed_bits, has_programmable_bits;
    bool is_odd = otp_addr & 1;

    /*
     * prog_bit uses 0s to indicate target bits to program:
     *   - if OTP word is even-indexed, programmed bits flip 0->1
     *   - if odd, bits flip 1->0
     * Bit programming is one-way only and irreversible.
     */
    if (is_odd) {
        programmed_bits = ~value & prog_bit;
    } else {
        programmed_bits = value & (~prog_bit);
    }

    /* If any bit can be programmed, accept the request */
    has_programmable_bits = value ^ (~prog_bit);

    if (programmed_bits) {
        trace_aspeed_otpmem_prog_conflict(otp_addr, programmed_bits);
        for (int i = 0; i < 32; ++i) {
            if (programmed_bits & (1U << i)) {
                trace_aspeed_otpmem_prog_bit(i);
            }
        }
    }

    return has_programmable_bits != 0;
}

static bool program_otpmem_data(void *opaque, uint32_t otp_addr,
                             uint32_t prog_bit, uint32_t *value)
{
    AspeedOTPMemState *s = ASPEED_OTPMEM(opaque);
    bool is_odd = otp_addr & 1;
    uint32_t otp_offset = otp_addr << 2;
    MemTxResult ret;

    ret = address_space_read(&s->as, otp_offset, MEMTXATTRS_UNSPECIFIED,
                             value, sizeof(uint32_t));
    if (ret != MEMTX_OK) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "Failed to read data 0x%x\n",
                      otp_offset);
        return false;
    }

    if (!valid_program_data(otp_addr, *value, prog_bit)) {
        return false;
    }

    if (is_odd) {
        *value &= ~prog_bit;
    } else {
        *value |= ~prog_bit;
    }

    return true;
}

static void aspeed_otpmem_prog(AspeedOtpmemState *s, uint32_t otp_addr,
                               uint32_t val, Error **errp)
{
    uint32_t otp_offset, value;
    MemTxResult ret;

    if (!program_otpmem_data(s, otp_addr, val, &value)) {
        error_setg(errp, "Failed to program data");
        return;
    }

    otp_offset = otp_addr << 2;
    ret = address_space_write(&s->as, otp_offset, MEMTXATTRS_UNSPECIFIED,
                              (uint8_t *)&value, sizeof(value));
    if (ret != MEMTX_OK) {
        error_setg(errp, "Failed to write %x to OTP [%x]", val, otp_addr);
        return;
    }
    trace_aspeed_otpmem_prog(otp_offset, value, val);
    aspeed_otpmem_sync_region(s, otp_offset, sizeof(value));
}

static void aspeed_otpmem_set_default(AspeedOtpmemState *s, uint32_t otp_offset,
                                      uint32_t val, Error **errp)
{
    MemTxResult ret;

    ret = address_space_write(&s->as, otp_offset, MEMTXATTRS_UNSPECIFIED,
                              (uint8_t *)&val, sizeof(val));
    if (ret != MEMTX_OK) {
        error_setg(errp, "Failed to set value %x to OTP [%x]", val, otp_offset);
        return;
    }
    aspeed_otpmem_sync_region(s, otp_offset, sizeof(val));
}

static const AspeedOTPMemOps aspeed_otpmem_default_ops = {
    .read = aspeed_otpmem_read,
    .prog = aspeed_otpmem_prog,
    .set_default = aspeed_otpmem_set_default,
};

const AspeedOTPMemOps *aspeed_otpmem_get_ops(AspeedOTPMemState *s)
{
    return s->ops;
}

static void aspeed_otpmem_realize(DeviceState *dev, Error **errp)
{
    AspeedOTPMemState *s = ASPEED_OTPMEM(dev);
    struct stat st;

    s->size = OTPMEM_SIZE;
    s->ops = &aspeed_otpmem_default_ops;

    if (s->otpmem_img_path && strlen(s->otpmem_img_path)) {
        if (stat(s->otpmem_img_path, &st) < 0) {
            error_setg(errp, "Failed to open %s",
                       s->otpmem_img_path);
            return;
        }
        if (st.st_size != OTPMEM_SIZE) {
            error_setg(errp, "Invalid OTP size %ld",
                       st.st_size);
            return;
        }
        memory_region_init_ram_from_file(&s->iomem, OBJECT(dev),
                "aspeed.otpmem.backend", s->size, s->size,
                RAM_SHARED, s->otpmem_img_path, 0, errp);
        address_space_init(&s->as, &s->iomem, NULL);
    }
}

static void aspeed_otpmem_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->realize = aspeed_otpmem_realize;
    dc->user_creatable = false;
}

static const TypeInfo aspeed_otpmem_info = {
    .name          = TYPE_ASPEED_OTPMEM,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(AspeedOTPMemState),
    .class_init    = aspeed_otpmem_class_init,
};

static void aspeed_otpmem_register_types(void)
{
    type_register_static(&aspeed_otpmem_info);
}

type_init(aspeed_otpmem_register_types)
