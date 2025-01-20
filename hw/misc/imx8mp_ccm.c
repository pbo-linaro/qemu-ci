/*
 * Copyright (c) 2025 Bernhard Beschow <shentey@gmail.com>
 *
 * i.MX8MP CCM, PMU and ANALOG IP blocks emulation code
 *
 * Based on hw/misc/imx7_ccm.c
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"

#include "hw/misc/imx8mp_ccm.h"
#include "migration/vmstate.h"

#include "trace.h"

#define CKIH_FREQ 16000000 /* 16MHz crystal input */

#define ANALOG_PLL_LOCK BIT(31)

static void imx8mp_analog_reset(DeviceState *dev)
{
    IMX8MPAnalogState *s = IMX8MP_ANALOG(dev);

    memset(s->analog, 0, sizeof(s->analog));

    s->analog[ANALOG_AUDIO_PLL1_GEN_CTRL] = 0x00002010;
    s->analog[ANALOG_AUDIO_PLL1_FDIV_CTL0] = 0x00145032;
    s->analog[ANALOG_AUDIO_PLL1_FDIV_CTL1] = 0x00000000;
    s->analog[ANALOG_AUDIO_PLL1_SSCG_CTRL] = 0x00000000;
    s->analog[ANALOG_AUDIO_PLL1_MNIT_CTRL] = 0x00100103;
    s->analog[ANALOG_AUDIO_PLL2_GEN_CTRL] = 0x00002010;
    s->analog[ANALOG_AUDIO_PLL2_FDIV_CTL0] = 0x00145032;
    s->analog[ANALOG_AUDIO_PLL2_FDIV_CTL1] = 0x00000000;
    s->analog[ANALOG_AUDIO_PLL2_SSCG_CTRL] = 0x00000000;
    s->analog[ANALOG_AUDIO_PLL2_MNIT_CTRL] = 0x00100103;
    s->analog[ANALOG_VIDEO_PLL1_GEN_CTRL] = 0x00002010;
    s->analog[ANALOG_VIDEO_PLL1_FDIV_CTL0] = 0x00145032;
    s->analog[ANALOG_VIDEO_PLL1_FDIV_CTL1] = 0x00000000;
    s->analog[ANALOG_VIDEO_PLL1_SSCG_CTRL] = 0x00000000;
    s->analog[ANALOG_VIDEO_PLL1_MNIT_CTRL] = 0x00100103;
    s->analog[ANALOG_DRAM_PLL_GEN_CTRL] = 0x00002010;
    s->analog[ANALOG_DRAM_PLL_FDIV_CTL0] = 0x0012c032;
    s->analog[ANALOG_DRAM_PLL_FDIV_CTL1] = 0x00000000;
    s->analog[ANALOG_DRAM_PLL_SSCG_CTRL] = 0x00000000;
    s->analog[ANALOG_DRAM_PLL_MNIT_CTRL] = 0x00100103;
    s->analog[ANALOG_GPU_PLL_GEN_CTRL] = 0x00000810;
    s->analog[ANALOG_GPU_PLL_FDIV_CTL0] = 0x000c8031;
    s->analog[ANALOG_GPU_PLL_LOCKD_CTRL] = 0x0010003f;
    s->analog[ANALOG_GPU_PLL_MNIT_CTRL] = 0x00280081;
    s->analog[ANALOG_VPU_PLL_GEN_CTRL] = 0x00000810;
    s->analog[ANALOG_VPU_PLL_FDIV_CTL0] = 0x0012c032;
    s->analog[ANALOG_VPU_PLL_LOCKD_CTRL] = 0x0010003f;
    s->analog[ANALOG_VPU_PLL_MNIT_CTRL] = 0x00280081;
    s->analog[ANALOG_ARM_PLL_GEN_CTRL] = 0x00000810;
    s->analog[ANALOG_ARM_PLL_FDIV_CTL0] = 0x000fa031;
    s->analog[ANALOG_ARM_PLL_LOCKD_CTRL] = 0x0010003f;
    s->analog[ANALOG_ARM_PLL_MNIT_CTRL] = 0x00280081;
    s->analog[ANALOG_SYS_PLL1_GEN_CTRL] = 0x0aaaa810;
    s->analog[ANALOG_SYS_PLL1_FDIV_CTL0] = 0x00190032;
    s->analog[ANALOG_SYS_PLL1_LOCKD_CTRL] = 0x0010003f;
    s->analog[ANALOG_SYS_PLL1_MNIT_CTRL] = 0x00280081;
    s->analog[ANALOG_SYS_PLL2_GEN_CTRL] = 0x0aaaa810;
    s->analog[ANALOG_SYS_PLL2_FDIV_CTL0] = 0x000fa031;
    s->analog[ANALOG_SYS_PLL2_LOCKD_CTRL] = 0x0010003f;
    s->analog[ANALOG_SYS_PLL2_MNIT_CTRL] = 0x00280081;
    s->analog[ANALOG_SYS_PLL3_GEN_CTRL] = 0x00000810;
    s->analog[ANALOG_SYS_PLL3_FDIV_CTL0] = 0x000fa031;
    s->analog[ANALOG_SYS_PLL3_LOCKD_CTRL] = 0x0010003f;
    s->analog[ANALOG_SYS_PLL3_MNIT_CTRL] = 0x00280081;
    s->analog[ANALOG_OSC_MISC_CFG] = 0x00000000;
    s->analog[ANALOG_ANAMIX_PLL_MNIT_CTL] = 0x00000000;
    s->analog[ANALOG_DIGPROG] = 0x00824010;

    /* all PLLs need to be locked */
    s->analog[ANALOG_AUDIO_PLL1_GEN_CTRL] |= ANALOG_PLL_LOCK;
    s->analog[ANALOG_AUDIO_PLL2_GEN_CTRL] |= ANALOG_PLL_LOCK;
    s->analog[ANALOG_VIDEO_PLL1_GEN_CTRL] |= ANALOG_PLL_LOCK;
    s->analog[ANALOG_DRAM_PLL_GEN_CTRL] |= ANALOG_PLL_LOCK;
    s->analog[ANALOG_GPU_PLL_GEN_CTRL] |= ANALOG_PLL_LOCK;
    s->analog[ANALOG_VPU_PLL_GEN_CTRL] |= ANALOG_PLL_LOCK;
    s->analog[ANALOG_ARM_PLL_GEN_CTRL] |= ANALOG_PLL_LOCK;
    s->analog[ANALOG_SYS_PLL1_GEN_CTRL] |= ANALOG_PLL_LOCK;
    s->analog[ANALOG_SYS_PLL2_GEN_CTRL] |= ANALOG_PLL_LOCK;
    s->analog[ANALOG_SYS_PLL3_GEN_CTRL] |= ANALOG_PLL_LOCK;
}

static void imx8mp_ccm_reset(DeviceState *dev)
{
    IMX8MPCCMState *s = IMX8MP_CCM(dev);

    memset(s->ccm, 0, sizeof(s->ccm));
}

#define CCM_INDEX(offset)   (((offset) & ~(hwaddr)0xF) / sizeof(uint32_t))
#define CCM_BITOP(offset)   ((offset) & (hwaddr)0xF)

enum {
    CCM_BITOP_NONE = 0x00,
    CCM_BITOP_SET  = 0x04,
    CCM_BITOP_CLR  = 0x08,
    CCM_BITOP_TOG  = 0x0C,
};

static uint64_t imx8mp_set_clr_tog_read(void *opaque, hwaddr offset,
                                        unsigned size)
{
    const uint32_t *mmio = opaque;

    return mmio[CCM_INDEX(offset)];
}

static void imx8mp_set_clr_tog_write(void *opaque, hwaddr offset,
                                     uint64_t value, unsigned size)
{
    const uint8_t  bitop = CCM_BITOP(offset);
    const uint32_t index = CCM_INDEX(offset);
    uint32_t *mmio = opaque;

    switch (bitop) {
    case CCM_BITOP_NONE:
        mmio[index]  = value;
        break;
    case CCM_BITOP_SET:
        mmio[index] |= value;
        break;
    case CCM_BITOP_CLR:
        mmio[index] &= ~value;
        break;
    case CCM_BITOP_TOG:
        mmio[index] ^= value;
        break;
    };
}

static const struct MemoryRegionOps imx8mp_set_clr_tog_ops = {
    .read = imx8mp_set_clr_tog_read,
    .write = imx8mp_set_clr_tog_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        /*
         * Our device would not work correctly if the guest was doing
         * unaligned access. This might not be a limitation on the real
         * device but in practice there is no reason for a guest to access
         * this device unaligned.
         */
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static uint64_t imx8mp_analog_read(void *opaque, hwaddr offset, unsigned size)
{
    IMX8MPAnalogState *s = opaque;

    return s->analog[offset >> 2];
}

static void imx8mp_analog_write(void *opaque, hwaddr offset,
                                uint64_t value, unsigned size)
{
    IMX8MPAnalogState *s = opaque;

    if (offset >> 2 == ANALOG_DIGPROG) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "Guest write to read-only ANALOG_DIGPROG register\n");
    } else {
        s->analog[offset >> 2] = value;
    }
}

static const struct MemoryRegionOps imx8mp_analog_ops = {
    .read = imx8mp_analog_read,
    .write = imx8mp_analog_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static void imx8mp_ccm_init(Object *obj)
{
    SysBusDevice *sd = SYS_BUS_DEVICE(obj);
    IMX8MPCCMState *s = IMX8MP_CCM(obj);

    memory_region_init_io(&s->iomem,
                          obj,
                          &imx8mp_set_clr_tog_ops,
                          s->ccm,
                          TYPE_IMX8MP_CCM ".ccm",
                          sizeof(s->ccm));

    sysbus_init_mmio(sd, &s->iomem);
}

static void imx8mp_analog_init(Object *obj)
{
    SysBusDevice *sd = SYS_BUS_DEVICE(obj);
    IMX8MPAnalogState *s = IMX8MP_ANALOG(obj);

    memory_region_init(&s->mmio.container, obj, TYPE_IMX8MP_ANALOG, 0x10000);

    memory_region_init_io(&s->mmio.analog, obj, &imx8mp_analog_ops, s,
                          TYPE_IMX8MP_ANALOG, sizeof(s->analog));
    memory_region_add_subregion(&s->mmio.container, 0, &s->mmio.analog);

    sysbus_init_mmio(sd, &s->mmio.container);
}

static const VMStateDescription vmstate_imx8mp_ccm = {
    .name = TYPE_IMX8MP_CCM,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(ccm, IMX8MPCCMState, CCM_MAX),
        VMSTATE_END_OF_LIST()
    },
};

static uint32_t imx8mp_ccm_get_clock_frequency(IMXCCMState *dev, IMXClk clock)
{
    /*
     * This function is "consumed" by GPT emulation code. Some clocks
     * have fixed frequencies and we can provide requested frequency
     * easily. However for CCM provided clocks (like IPG) each GPT
     * timer can have its own clock root.
     * This means we need additional information when calling this
     * function to know the requester's identity.
     */
    uint32_t freq = 0;

    switch (clock) {
    case CLK_NONE:
        break;
    case CLK_32k:
        freq = CKIL_FREQ;
        break;
    case CLK_HIGH:
        freq = CKIH_FREQ;
        break;
    case CLK_IPG:
    case CLK_IPG_HIGH:
        /*
         * For now we don't have a way to figure out the device this
         * function is called for. Until then the IPG derived clocks
         * are left unimplemented.
         */
        qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Clock %d Not implemented\n",
                      TYPE_IMX8MP_CCM, __func__, clock);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: unsupported clock %d\n",
                      TYPE_IMX8MP_CCM, __func__, clock);
        break;
    }

    trace_ccm_clock_freq(clock, freq);

    return freq;
}

static void imx8mp_ccm_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    IMXCCMClass *ccm = IMX_CCM_CLASS(klass);

    device_class_set_legacy_reset(dc, imx8mp_ccm_reset);
    dc->vmsd  = &vmstate_imx8mp_ccm;
    dc->desc  = "i.MX8MP Clock Control Module";

    ccm->get_clock_frequency = imx8mp_ccm_get_clock_frequency;
}

static const VMStateDescription vmstate_imx8mp_analog = {
    .name = TYPE_IMX8MP_ANALOG,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(analog, IMX8MPAnalogState, ANALOG_MAX),
        VMSTATE_END_OF_LIST()
    },
};

static void imx8mp_analog_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, imx8mp_analog_reset);
    dc->vmsd  = &vmstate_imx8mp_analog;
    dc->desc  = "i.MX8MP Analog Module";
}

static const TypeInfo imx8mp_ccm_types[] = {
    {
        .name          = TYPE_IMX8MP_CCM,
        .parent        = TYPE_IMX_CCM,
        .instance_size = sizeof(IMX8MPCCMState),
        .instance_init = imx8mp_ccm_init,
        .class_init    = imx8mp_ccm_class_init,
    },
    {
        .name          = TYPE_IMX8MP_ANALOG,
        .parent        = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(IMX8MPAnalogState),
        .instance_init = imx8mp_analog_init,
        .class_init    = imx8mp_analog_class_init,
    }
};

DEFINE_TYPES(imx8mp_ccm_types);
