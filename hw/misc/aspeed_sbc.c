/*
 * ASPEED Secure Boot Controller
 *
 * Copyright (C) 2021-2022 IBM Corp.
 *
 * Joel Stanley <joel@jms.id.au>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "hw/qdev-properties.h"
#include "hw/misc/aspeed_sbc.h"
#include "qapi/error.h"
#include "migration/vmstate.h"
#include "trace.h"

#define R_PROT          (0x000 / 4)
#define R_CMD           (0x004 / 4)
#define R_ADDR          (0x010 / 4)
#define R_STATUS        (0x014 / 4)
#define R_CAMP1         (0x020 / 4)
#define R_CAMP2         (0x024 / 4)
#define R_QSR           (0x040 / 4)

/* R_STATUS */
#define ABR_EN                  BIT(14) /* Mirrors SCU510[11] */
#define ABR_IMAGE_SOURCE        BIT(13)
#define SPI_ABR_IMAGE_SOURCE    BIT(12)
#define SB_CRYPTO_KEY_EXP_DONE  BIT(11)
#define SB_CRYPTO_BUSY          BIT(10)
#define OTP_WP_EN               BIT(9)
#define OTP_ADDR_WP_EN          BIT(8)
#define LOW_SEC_KEY_EN          BIT(7)
#define SECURE_BOOT_EN          BIT(6)
#define UART_BOOT_EN            BIT(5)
/* bit 4 reserved*/
#define OTP_CHARGE_PUMP_READY   BIT(3)
#define OTP_IDLE                BIT(2)
#define OTP_MEM_IDLE            BIT(1)
#define OTP_COMPARE_STATUS      BIT(0)

/* QSR */
#define QSR_RSA_MASK           (0x3 << 12)
#define QSR_HASH_MASK          (0x3 << 10)

typedef enum {
    SBC_OTP_CMD_READ = 0x23b1e361,
    SBC_OTP_CMD_WRITE = 0x23b1e362,
    SBC_OTP_CMD_PROG = 0x23b1e364,
} SBC_OTP_Command;

#define OTP_DATA_DWORD_COUNT        (0x800)
#define OTP_TOTAL_DWORD_COUNT       (0x1000)

#define MODE_REGISTER               (0x1000)
#define MODE_REGISTER_A             (0x3000)
#define MODE_REGISTER_B             (0x5000)

static uint64_t aspeed_sbc_read(void *opaque, hwaddr addr, unsigned int size)
{
    AspeedSBCState *s = ASPEED_SBC(opaque);

    addr >>= 2;

    if (addr >= ASPEED_SBC_NR_REGS) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Out-of-bounds read at offset 0x%" HWADDR_PRIx "\n",
                      __func__, addr << 2);
        return 0;
    }

    return s->regs[addr];
}

static bool aspeed_sbc_otpmem_read(AspeedSBCState *s,
                                   uint32_t otp_addr, Error **errp)
{
    uint32_t data = 0, otp_offset;
    bool is_data = false;
    AspeedSBCClass *sc = ASPEED_SBC_GET_CLASS(s);
    const AspeedOTPMemOps *otp_ops;

    if (sc->has_otpmem == false) {
        trace_aspeed_sbc_otpmem_state("disabled");
        return true;
    }

    otp_ops = aspeed_otpmem_get_ops(&s->otpmem);

    if (otp_addr < OTP_DATA_DWORD_COUNT) {
        is_data = true;
    } else if (otp_addr >= OTP_TOTAL_DWORD_COUNT) {
        error_setg(errp, "Invalid OTP addr 0x%x", otp_addr);
        return false;
    }
    otp_offset = otp_addr << 2;

    data = otp_ops->read(&s->otpmem, otp_offset, errp);
    if (*errp) {
        return false;
    }
    s->regs[R_CAMP1] = data;

    if (is_data) {
        data = otp_ops->read(&s->otpmem, otp_offset + 4, errp);
        if (*errp) {
            return false;
        }
        s->regs[R_CAMP2] = data;
    }

    return true;
}

static bool mr_handler(uint32_t otp_addr, Error **errp)
{
    switch (otp_addr) {
    case MODE_REGISTER:
    case MODE_REGISTER_A:
    case MODE_REGISTER_B:
        /* HW behavior, do nothing here */
        return true;
    default:
        error_setg(errp, "Unsupported address 0x%x", otp_addr);
        return false;
    }
}

static bool aspeed_sbc_otpmem_write(AspeedSBCState *s,
                                    uint32_t otp_addr, Error **errp)
{
    if (otp_addr == 0) {
        trace_aspeed_sbc_ignore_cmd(otp_addr);
        return true;
    } else if (otp_addr >= MODE_REGISTER) {
        if (mr_handler(otp_addr, errp) == false) {
            return false;
        }
    } else {
        error_setg(errp, "Unhandled OTP write address 0x%x", otp_addr);
        return false;
    }

    return true;
}

static bool aspeed_sbc_otpmem_prog(AspeedSBCState *s,
                                   uint32_t otp_addr, Error **errp)
{
    uint32_t value;
    AspeedSBCClass *sc = ASPEED_SBC_GET_CLASS(s);
    const AspeedOTPMemOps *otp_ops;

    if (sc->has_otpmem == false) {
        trace_aspeed_sbc_otpmem_state("disabled");
        return true;
    }

    otp_ops = aspeed_otpmem_get_ops(&s->otpmem);
    value = s->regs[R_CAMP1];
    if (otp_addr >= OTP_TOTAL_DWORD_COUNT) {
        error_setg(errp, "Invalid OTP addr 0x%x", otp_addr);
        return false;
    }

    otp_ops->prog(&s->otpmem, otp_addr, value, errp);

    if (*errp) {
        return false;
    }

    return true;
}

static void aspeed_sbc_handle_command(void *opaque, uint32_t cmd)
{
    AspeedSBCState *s = ASPEED_SBC(opaque);
    Error *local_err = NULL;
    bool ret = false;
    uint32_t otp_addr;

    s->regs[R_STATUS] &= ~(OTP_MEM_IDLE | OTP_IDLE);
    otp_addr = s->regs[R_ADDR];

    switch (cmd) {
    case SBC_OTP_CMD_READ:
        ret = aspeed_sbc_otpmem_read(s, otp_addr, &local_err);
        break;
    case SBC_OTP_CMD_WRITE:
        ret = aspeed_sbc_otpmem_write(s, otp_addr, &local_err);
        break;
    case SBC_OTP_CMD_PROG:
        ret = aspeed_sbc_otpmem_prog(s, otp_addr, &local_err);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Unknown command 0x%x\n",
                      __func__, cmd);
        break;
    }

    trace_aspeed_sbc_handle_cmd(cmd, otp_addr, ret);
    if (ret == false && local_err) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: %s\n",
                      __func__, error_get_pretty(local_err));
        error_free(local_err);
    }
    s->regs[R_STATUS] |= (OTP_MEM_IDLE | OTP_IDLE);
}

static void aspeed_sbc_write(void *opaque, hwaddr addr, uint64_t data,
                              unsigned int size)
{
    AspeedSBCState *s = ASPEED_SBC(opaque);

    addr >>= 2;

    if (addr >= ASPEED_SBC_NR_REGS) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Out-of-bounds write at offset 0x%" HWADDR_PRIx "\n",
                      __func__, addr << 2);
        return;
    }

    switch (addr) {
    case R_STATUS:
    case R_QSR:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: write to read only register 0x%" HWADDR_PRIx "\n",
                      __func__, addr << 2);
        return;
    case R_CMD:
        aspeed_sbc_handle_command(opaque, data);
        return;
    default:
        break;
    }

    s->regs[addr] = data;
}

static const MemoryRegionOps aspeed_sbc_ops = {
    .read = aspeed_sbc_read,
    .write = aspeed_sbc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static void aspeed_sbc_reset(DeviceState *dev)
{
    struct AspeedSBCState *s = ASPEED_SBC(dev);

    memset(s->regs, 0, sizeof(s->regs));

    /* Set secure boot enabled with RSA4096_SHA256 and enable eMMC ABR */
    s->regs[R_STATUS] = OTP_IDLE | OTP_MEM_IDLE;

    if (s->emmc_abr) {
        s->regs[R_STATUS] &= ABR_EN;
    }

    if (s->signing_settings) {
        s->regs[R_STATUS] &= SECURE_BOOT_EN;
    }

    s->regs[R_QSR] = s->signing_settings;
}

static void aspeed_sbc_realize(DeviceState *dev, Error **errp)
{
    AspeedSBCState *s = ASPEED_SBC(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    AspeedSBCClass *sc = ASPEED_SBC_GET_CLASS(dev);
    char *otpmem;

    memory_region_init_io(&s->iomem, OBJECT(s), &aspeed_sbc_ops, s,
            TYPE_ASPEED_SBC, 0x1000);

    sysbus_init_mmio(sbd, &s->iomem);

    otpmem = object_property_get_str(qdev_get_machine(), "otpmem", errp);
    /*
     * If user doesn't specify the otpmem file location,
     * to disable the OTP memory feature due to no backend data
     */
    if ((otpmem == NULL) || !strlen(otpmem)) {
        sc->has_otpmem = false;
    }

    if (sc->has_otpmem) {
        object_initialize_child(OBJECT(s), "optmem",
                                &s->otpmem, TYPE_ASPEED_OTPMEM);
        aspeed_otpmem_set_backend(&s->otpmem, otpmem);
        qdev_realize(DEVICE(&s->otpmem), NULL, errp);
        trace_aspeed_sbc_otpmem_state("enabled");
    } else {
        trace_aspeed_sbc_otpmem_state("disabled");
    }
}

static const VMStateDescription vmstate_aspeed_sbc = {
    .name = TYPE_ASPEED_SBC,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, AspeedSBCState, ASPEED_SBC_NR_REGS),
        VMSTATE_END_OF_LIST(),
    }
};

static const Property aspeed_sbc_properties[] = {
    DEFINE_PROP_BOOL("emmc-abr", AspeedSBCState, emmc_abr, 0),
    DEFINE_PROP_UINT32("signing-settings", AspeedSBCState, signing_settings, 0),
};

static void aspeed_sbc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = aspeed_sbc_realize;
    device_class_set_legacy_reset(dc, aspeed_sbc_reset);
    dc->vmsd = &vmstate_aspeed_sbc;
    device_class_set_props(dc, aspeed_sbc_properties);
}

static const TypeInfo aspeed_sbc_info = {
    .name = TYPE_ASPEED_SBC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AspeedSBCState),
    .class_init = aspeed_sbc_class_init,
    .class_size = sizeof(AspeedSBCClass)
};

static void aspeed_ast2600_sbc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    AspeedSBCClass *sc = ASPEED_SBC_CLASS(klass);

    dc->desc = "AST2600 Secure Boot Controller";
    sc->has_otpmem = true;
}

static const TypeInfo aspeed_ast2600_sbc_info = {
    .name = TYPE_ASPEED_AST2600_SBC,
    .parent = TYPE_ASPEED_SBC,
    .class_init = aspeed_ast2600_sbc_class_init,
};

static void aspeed_ast10x0_sbc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    AspeedSBCClass *sc = ASPEED_SBC_CLASS(klass);

    dc->desc = "AST10X0 Secure Boot Controller";
    sc->has_otpmem = true;
}

static const TypeInfo aspeed_ast10x0_sbc_info = {
    .name = TYPE_ASPEED_AST10X0_SBC,
    .parent = TYPE_ASPEED_SBC,
    .class_init = aspeed_ast10x0_sbc_class_init,
};

static void aspeed_sbc_register_types(void)
{
    type_register_static(&aspeed_ast2600_sbc_info);
    type_register_static(&aspeed_ast10x0_sbc_info);
    type_register_static(&aspeed_sbc_info);
}

type_init(aspeed_sbc_register_types);
