/*
 * i.MX RT500 platforms.
 *
 * Copyright (c) 2024 Google LLC
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/sysbus.h"
#include "hw/arm/boot.h"
#include "hw/boards.h"
#include "hw/irq.h"
#include "qemu/log.h"
#include "qemu/datadir.h"
#include "qemu/units.h"
#include "exec/address-spaces.h"
#include "sysemu/reset.h"
#include "sysemu/runstate.h"
#include "sysemu/sysemu.h"
#include "hw/arm/armv7m.h"
#include "hw/loader.h"
#include "hw/qdev-clock.h"
#include "hw/misc/unimp.h"
#include "hw/arm/rt500.h"
#include "hw/arm/svd/rt500.h"

#define MMAP_SRAM_CODE_BASE   (0x0)
#define MMAP_SRAM_DATA_BASE   (0x20000000)
#define MMAP_SRAM_SIZE        (5 * MiB)
#define MMAP_BOOT_ROM_BASE    (0x03000000)
#define MMAP_BOOT_ROM_SIZE    (192 * KiB)
#define MMAP_SDMA_RAM_BASE    (0x24100000)
#define MMAP_SDMA_RAM_SIZE    (32 * KiB)
#define MMAP_FLEXSPI0_BASE    (0x08000000)
#define MMAP_FLEXSPI0_SIZE    (128 * MiB)
#define MMAP_FLEXSPI1_BASE    (0x28000000)
#define MMAP_FLEXSPI1_SIZE    (128 * MiB)

#define SECURE_OFFSET (0x10000000)

#define RT500_NUM_IRQ (RT500_FLEXCOMM16_IRQn + 1)

typedef enum MemInfoType {
    MEM_RAM,
    MEM_ROM,
    MEM_ALIAS
} MemInfoType;

static void do_sys_reset(void *opaque, int n, int level)
{
    if (level) {
        qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
    }
}

static void rt500_init(Object *obj)
{
    RT500State *s = RT500(obj);

    /* Add ARMv7-M device */
    object_initialize_child(obj, "armv7m", &s->armv7m, TYPE_ARMV7M);

    for (int i = 0; i < RT500_FLEXCOMM_NUM; i++) {
        char *id = g_strdup_printf("flexcomm%d", i);

        object_initialize_child(obj, id, &s->flexcomm[i], TYPE_FLEXCOMM);
        DEVICE(&s->flexcomm[i])->id = id;
    }

    object_initialize_child(obj, "clkctl0", &s->clkctl0, TYPE_RT500_CLKCTL0);
    object_initialize_child(obj, "clkctl1", &s->clkctl1, TYPE_RT500_CLKCTL1);

    /* Initialize clocks */
    s->sysclk = qdev_init_clock_in(DEVICE(s), "sysclk", NULL, NULL, 0);
    s->refclk = qdev_init_clock_in(DEVICE(s), "refclk", NULL, NULL, 0);

    for (int i = 0; i < RT500_FLEXSPI_NUM; i++) {
        char *id = g_strdup_printf("flexspi%d", i);

        object_initialize_child(obj, id, &s->flexspi[i], TYPE_FLEXSPI);
        DEVICE(&s->flexspi[i])->id = id;
    }

    for (int i = 0; i < RT500_RSTCTL_NUM; i++) {
        static const char *types[] = {
            TYPE_RT500_RSTCTL0, TYPE_RT500_RSTCTL1
        };
        char *id = g_strdup_printf("rstctl%d", i);

        object_initialize_child(obj, id, &s->rstctl[i], types[i]);
        DEVICE(&s->rstctl[i])->id = id;
    }
}

static void rt500_realize_memory(RT500State *s, Error **errp)
{
    static const struct {
        const char *name;
        hwaddr base;
        size_t size;
        MemInfoType type;
        int alias_for;
    } mem_info[] = {
        {
            .name = "SRAM (code bus)",
            .base = MMAP_SRAM_CODE_BASE,
            .size = MMAP_SRAM_SIZE,
            .type = MEM_RAM,
        },
        {
            .name = "BOOT-ROM",
            .base = MMAP_BOOT_ROM_BASE,
            .size = MMAP_BOOT_ROM_SIZE,
            .type = MEM_ROM,
        },
        {
            .name = "Smart DMA RAM",
            .base = MMAP_SDMA_RAM_BASE,
            .size = MMAP_SDMA_RAM_SIZE,
            .type = MEM_RAM,
        },
        {
            .name = "SRAM (data bus)",
            .base = MMAP_SRAM_DATA_BASE,
            .size = MMAP_SRAM_SIZE,
            .type = MEM_ALIAS,
            .alias_for = 0
        },
    };

    s->mem = g_malloc_n(2 * ARRAY_SIZE(mem_info), sizeof(MemoryRegion));
    for (int i = 0; i < ARRAY_SIZE(mem_info); i++) {
        const char *name = mem_info[i].name;
        int size = mem_info[i].size;
        int type = mem_info[i].type;
        int alias_for = mem_info[i].alias_for;
        MemoryRegion *mem = &s->mem[i];
        uint32_t base = mem_info[i].base;
        MemoryRegion *sec_mem;
        char sec_name[256];

        switch (type) {
        case MEM_RAM:
            memory_region_init_ram(mem, OBJECT(s), name, size, errp);
            break;
        case MEM_ROM:
            memory_region_init_rom(mem, OBJECT(s), name, size, errp);
            break;
        case MEM_ALIAS:
        {
            MemoryRegion *orig = &s->mem[alias_for];

            memory_region_init_alias(mem, OBJECT(s), name, orig, 0, size);
            break;
        }
        default:
            g_assert_not_reached();
        }

        memory_region_add_subregion(get_system_memory(), base, mem);

        /* create secure alias */
        snprintf(sec_name, sizeof(sec_name), "SECURE %s", name);
        sec_mem = &s->mem[ARRAY_SIZE(mem_info) + i];
        if (type == MEM_ALIAS) {
            mem = &s->mem[alias_for];
        }
        memory_region_init_alias(sec_mem, OBJECT(s), sec_name, mem, 0, size);
        memory_region_add_subregion(get_system_memory(), base + SECURE_OFFSET,
                                    sec_mem);

        if (mem_info[i].type == MEM_ROM) {
            char *fname = qemu_find_file(QEMU_FILE_TYPE_BIOS, "rt500.rom");

            if (fname) {
                int fsize = get_image_size(fname);
                int ret;

                if (fsize > size) {
                    error_setg(errp, "rom file too big: %d > %d", fsize, size);
                } else {
                    ret = load_image_targphys(fname, base, size);
                    if (ret < 0) {
                        error_setg(errp, "could not load rom: %s", fname);
                    }
                }
            }
            g_free(fname);
        }
    }
}

static void rt500_realize(DeviceState *dev, Error **errp)
{
    MachineState *ms = MACHINE(qdev_get_machine());
    RT500State *s = RT500(dev);

    rt500_realize_memory(s, errp);

    /* Setup ARMv7M CPU */
    qdev_prop_set_uint32(DEVICE(&s->armv7m), "num-irq", RT500_NUM_IRQ);
    qdev_prop_set_uint8(DEVICE(&s->armv7m), "num-prio-bits", 3);
    qdev_prop_set_string(DEVICE(&s->armv7m), "cpu-type", "cortex-m33-arm-cpu");
    object_property_set_link(OBJECT(&s->armv7m), "memory",
                             OBJECT(get_system_memory()), &error_abort);
    if (!ms->kernel_filename) {
        qdev_prop_set_uint32(DEVICE(&s->armv7m), "init-nsvtor",
                             MMAP_BOOT_ROM_BASE);
        qdev_prop_set_uint32(DEVICE(&s->armv7m), "init-svtor",
                             MMAP_BOOT_ROM_BASE + SECURE_OFFSET);
    }

    qdev_connect_clock_in(DEVICE(&s->armv7m), "cpuclk", s->sysclk);
    qdev_connect_clock_in(DEVICE(&s->armv7m), "refclk",
                     qdev_get_clock_out(DEVICE(&s->clkctl0), "systick_clk"));

    sysbus_realize_and_unref(SYS_BUS_DEVICE(&s->armv7m), errp);
    qdev_connect_gpio_out_named(DEVICE(&s->armv7m), "SYSRESETREQ", 0,
                                qemu_allocate_irq(&do_sys_reset, NULL, 0));

    /* Setup FLEXCOMM */
    for (int i = 0; i < RT500_FLEXCOMM_NUM; i++) {
        static const uint32_t addr[] = {
            RT500_FLEXCOMM0_BASE, RT500_FLEXCOMM1_BASE, RT500_FLEXCOMM2_BASE,
            RT500_FLEXCOMM3_BASE, RT500_FLEXCOMM4_BASE, RT500_FLEXCOMM5_BASE,
            RT500_FLEXCOMM6_BASE, RT500_FLEXCOMM7_BASE, RT500_FLEXCOMM8_BASE,
            RT500_FLEXCOMM8_BASE, RT500_FLEXCOMM10_BASE, RT500_FLEXCOMM11_BASE,
            RT500_FLEXCOMM12_BASE, RT500_FLEXCOMM13_BASE, RT500_FLEXCOMM14_BASE,
            RT500_FLEXCOMM15_BASE, RT500_FLEXCOMM16_BASE
        };
        static const int irq[] = {
            RT500_FLEXCOMM0_IRQn, RT500_FLEXCOMM1_IRQn, RT500_FLEXCOMM2_IRQn,
            RT500_FLEXCOMM3_IRQn, RT500_FLEXCOMM4_IRQn, RT500_FLEXCOMM5_IRQn,
            RT500_FLEXCOMM6_IRQn, RT500_FLEXCOMM7_IRQn, RT500_FLEXCOMM8_IRQn,
            RT500_FLEXCOMM9_IRQn, RT500_FLEXCOMM10_IRQn, RT500_FLEXCOMM11_IRQn,
            RT500_FLEXCOMM12_IRQn, RT500_FLEXCOMM13_IRQn, RT500_FLEXCOMM14_IRQn,
            RT500_FLEXCOMM15_IRQn, RT500_FLEXCOMM16_IRQn
        };
        static const int functions[] = {
            FLEXCOMM_FULL, FLEXCOMM_FULL, FLEXCOMM_FULL,
            FLEXCOMM_FULL, FLEXCOMM_FULL, FLEXCOMM_FULL,
            FLEXCOMM_FULL, FLEXCOMM_FULL, FLEXCOMM_FULL,
            FLEXCOMM_FULL, FLEXCOMM_FULL, FLEXCOMM_FULL,
            FLEXCOMM_FULL, FLEXCOMM_FULL, FLEXCOMM_HSSPI,
            FLEXCOMM_PMICI2C, FLEXCOMM_HSSPI
        };
        DeviceState *ds = DEVICE(&s->flexcomm[i]);

        qdev_prop_set_uint32(ds, "functions", functions[i]);
        sysbus_realize_and_unref(SYS_BUS_DEVICE(ds), errp);
        sysbus_mmio_map(SYS_BUS_DEVICE(ds), 0, addr[i]);
        sysbus_connect_irq(SYS_BUS_DEVICE(ds), 0,
                           qdev_get_gpio_in(DEVICE(&s->armv7m), irq[i]));
    }

    /* Setup CTLCTL0 */
    qdev_connect_clock_in(DEVICE(&s->clkctl0), "sysclk", s->sysclk);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(DEVICE(&s->clkctl0)), errp);
    sysbus_mmio_map(SYS_BUS_DEVICE(DEVICE(&s->clkctl0)), 0, RT500_CLKCTL0_BASE);

    /* Setup CTLCTL1 */
    qdev_connect_clock_in(DEVICE(&s->clkctl1), "sysclk", s->sysclk);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(DEVICE(&s->clkctl1)), errp);
    sysbus_mmio_map(SYS_BUS_DEVICE(DEVICE(&s->clkctl1)), 0, RT500_CLKCTL1_BASE);

    /* Setup FlexSPI */
    for (int i = 0; i < RT500_FLEXSPI_NUM; i++) {
        static const uint32_t addr[] = {
            RT500_FLEXSPI0_BASE, RT500_FLEXSPI1_BASE
        };
        static const uint32_t mmap_base[] = {
            MMAP_FLEXSPI0_BASE, MMAP_FLEXSPI1_BASE
        };
        static const uint32_t mmap_size[] = {
            MMAP_FLEXSPI0_SIZE, MMAP_FLEXSPI1_SIZE,
        };
        DeviceState *ds = DEVICE(&s->flexspi[i]);

        qdev_prop_set_uint32(ds, "mmap_size", mmap_size[i]);
        sysbus_realize_and_unref(SYS_BUS_DEVICE(ds), errp);
        sysbus_mmio_map(SYS_BUS_DEVICE(ds), 0, addr[i]);
        sysbus_mmio_map(SYS_BUS_DEVICE(ds), 1, mmap_base[i]);
    }

    /* Setup reset controllers */
    for (int i = 0; i < RT500_RSTCTL_NUM; i++) {
        DeviceState *ds = DEVICE(&s->rstctl[i]);
        static const uint32_t addr[] = {
            RT500_RSTCTL0_BASE, RT500_RSTCTL1_BASE
        };

        sysbus_realize_and_unref(SYS_BUS_DEVICE(ds), errp);
        sysbus_mmio_map(SYS_BUS_DEVICE(ds), 0, addr[i]);
    }
}

static void rt500_unrealize(DeviceState *ds)
{
    RT500State *s = RT500(ds);

    g_free(s->mem);
}

static void rt500_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = rt500_realize;
    dc->unrealize = rt500_unrealize;
    dc->desc = "RT500 (ARM Cortex-M33)";
}

static const TypeInfo rt500_types[] = {
    {
        .name = TYPE_RT500,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(RT500State),
        .instance_init = rt500_init,
        .class_init = rt500_class_init,
    },
};

DEFINE_TYPES(rt500_types);

