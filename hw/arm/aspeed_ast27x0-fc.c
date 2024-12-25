/*
 * ASPEED SoC 2700 family
 *
 * Copyright (C) 2024 ASPEED Technology Inc.
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 *
 * Implementation extracted from the AST2600 and adapted for AST2700.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "system/block-backend.h"
#include "system/system.h"
#include "hw/arm/aspeed.h"
#include "hw/boards.h"
#include "hw/qdev-clock.h"
#include "hw/arm/aspeed_soc.h"
#include "hw/loader.h"
#include "hw/arm/boot.h"
#include "hw/block/flash.h"

#define TYPE_AST2700FC MACHINE_TYPE_NAME("ast2700fc")
OBJECT_DECLARE_SIMPLE_TYPE(Ast2700FCState, AST2700FC);

static struct arm_boot_info ast2700fc_board_binfo = {
    .board_id = -1, /* device-tree-only board */
};

struct Ast2700FCState {
    MachineState parent_obj;

    MemoryRegion ca35_memory;
    MemoryRegion ca35_dram;
    MemoryRegion ca35_boot_rom;
    MemoryRegion ssp_memory;
    MemoryRegion tsp_memory;

    Clock *ssp_sysclk;
    Clock *tsp_sysclk;

    Aspeed27x0SoCState ca35;
    Aspeed27x0CM4SoCState ssp;
    Aspeed27x0CM4SoCState tsp;

    bool mmio_exec;
};

#define AST2700FC_BMC_RAM_SIZE (1 * GiB)
#define AST2700FC_BMC_SRAM_SIZE (1 * GiB)

#define AST2700FC_HW_STRAP1 0x000000C0
#define AST2700FC_HW_STRAP2 0x00000003
#define AST2700FC_FMC_MODEL "w25q01jvq"
#define AST2700FC_SPI_MODEL "w25q512jv"

static void ast2700fc_install_boot_rom(Ast2700FCState *s, BlockBackend *blk,
                                    uint64_t rom_size)
{
    AspeedSoCState *soc = ASPEED_SOC(&s->ca35);
    AspeedSoCClass *sc = ASPEED_SOC_GET_CLASS(soc);

    memory_region_init_rom(&s->ca35_boot_rom, NULL, "aspeed.boot_rom", rom_size,
                           &error_abort);
    memory_region_add_subregion_overlap(&soc->spi_boot_container, 0,
                                        &s->ca35_boot_rom, 1);
    write_boot_rom(blk, sc->memmap[ASPEED_DEV_SPI_BOOT],
                   rom_size, &error_abort);
}

static void ast2700fc_ca35_init(MachineState *machine)
{
    Ast2700FCState *s = AST2700FC(machine);
    AspeedSoCState *soc;
    AspeedSoCClass *sc;
    DriveInfo *emmc0 = NULL;
    bool boot_emmc;
    int i;

    object_initialize_child(OBJECT(s), "ca35", &s->ca35, "ast2700-a0");
    soc = ASPEED_SOC(&s->ca35);
    sc = ASPEED_SOC_GET_CLASS(soc);

    memory_region_init(&s->ca35_memory, OBJECT(&s->ca35), "ca35-memory",
                       UINT64_MAX);

    memory_region_init_ram(&s->ca35_dram, OBJECT(&s->ca35), "ca35-dram",
                           AST2700FC_BMC_SRAM_SIZE, &error_abort);
    object_property_set_link(OBJECT(&s->ca35), "memory",
                             OBJECT(&s->ca35_memory),
                             &error_abort);
    object_property_set_link(OBJECT(&s->ca35), "dram", OBJECT(&s->ca35_dram),
                             &error_abort);
    object_property_set_int(OBJECT(&s->ca35), "ram-size",
                            AST2700FC_BMC_RAM_SIZE,
                            &error_abort);
    object_property_set_int(OBJECT(&s->ca35), "hw-strap1", AST2700FC_HW_STRAP1,
                            &error_abort);
    object_property_set_int(OBJECT(&s->ca35), "hw-strap2", AST2700FC_HW_STRAP2,
                            &error_abort);
    aspeed_soc_uart_set_chr(soc, ASPEED_DEV_UART12, serial_hd(0));
    qdev_realize(DEVICE(&s->ca35), NULL, &error_abort);

    aspeed_board_init_flashes(&soc->fmc, AST2700FC_FMC_MODEL, 2, 0);
    aspeed_board_init_flashes(&soc->spi[0], AST2700FC_SPI_MODEL, 1, 2);

    for (i = 0; i < soc->sdhci.num_slots; i++) {
        sdhci_attach_drive(&soc->sdhci.slots[i],
                           drive_get(IF_SD, 0, i), false, false);
    }
    boot_emmc = sc->boot_from_emmc(soc);

    if (soc->emmc.num_slots) {
        emmc0 = drive_get(IF_SD, 0, soc->sdhci.num_slots);
        sdhci_attach_drive(&soc->emmc.slots[0], emmc0, true, boot_emmc);
    }

    if (!s->mmio_exec) {
        DeviceState *dev = ssi_get_cs(soc->fmc.spi, 0);
        BlockBackend *fmc0 = dev ? m25p80_get_blk(dev) : NULL;

        if (fmc0 && !boot_emmc) {
            uint64_t rom_size = memory_region_size(&soc->spi_boot);
            ast2700fc_install_boot_rom(s, fmc0, rom_size);
        } else if (emmc0) {
            ast2700fc_install_boot_rom(s, blk_by_legacy_dinfo(emmc0), 64 * KiB);
        }
    }

    ast2700fc_board_binfo.ram_size = machine->ram_size;
    ast2700fc_board_binfo.loader_start = sc->memmap[ASPEED_DEV_SDRAM];

    arm_load_kernel(ARM_CPU(first_cpu), machine, &ast2700fc_board_binfo);
}

static void ast2700fc_ssp_init(MachineState *machine)
{
    AspeedSoCState *soc;
    Ast2700FCState *s = AST2700FC(machine);
    s->ssp_sysclk = clock_new(OBJECT(s), "SSP_SYSCLK");
    clock_set_hz(s->ssp_sysclk, 200000000ULL);

    object_initialize_child(OBJECT(s), "ssp", &s->ssp, "ast2700ssp-a0");
    memory_region_init(&s->ssp_memory, OBJECT(&s->ssp), "ssp-memory",
                       UINT64_MAX);

    qdev_connect_clock_in(DEVICE(&s->ssp), "sysclk", s->ssp_sysclk);
    object_property_set_link(OBJECT(&s->ssp), "memory", OBJECT(&s->ssp_memory),
                             &error_abort);

    soc = ASPEED_SOC(&s->ssp);
    aspeed_soc_uart_set_chr(soc, ASPEED_DEV_UART4, serial_hd(1));
    qdev_realize(DEVICE(&s->ssp), NULL, &error_abort);
}

static void ast2700fc_tsp_init(MachineState *machine)
{
    AspeedSoCState *soc;
    Ast2700FCState *s = AST2700FC(machine);
    s->tsp_sysclk = clock_new(OBJECT(s), "TSP_SYSCLK");
    clock_set_hz(s->tsp_sysclk, 200000000ULL);

    object_initialize_child(OBJECT(s), "tsp", &s->tsp, "ast2700tsp-a0");
    memory_region_init(&s->tsp_memory, OBJECT(&s->tsp), "tsp-memory",
                       UINT64_MAX);

    qdev_connect_clock_in(DEVICE(&s->tsp), "sysclk", s->tsp_sysclk);
    object_property_set_link(OBJECT(&s->tsp), "memory", OBJECT(&s->tsp_memory),
                             &error_abort);

    soc = ASPEED_SOC(&s->tsp);
    aspeed_soc_uart_set_chr(soc, ASPEED_DEV_UART4, serial_hd(2));
    qdev_realize(DEVICE(&s->tsp), NULL, &error_abort);
}

static void ast2700fc_init(MachineState *machine)
{
    ast2700fc_ca35_init(machine);
    ast2700fc_ssp_init(machine);
    ast2700fc_tsp_init(machine);
}

static void ast2700fc_instance_init(Object *obj)
{
    AST2700FC(obj)->mmio_exec = false;
}

static void ast2700fc_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "ast2700 full cores support";
    mc->init = ast2700fc_init;
    mc->no_floppy = 1;
    mc->no_cdrom = 1;
    mc->min_cpus = mc->max_cpus = mc->default_cpus = 6;
}

static const TypeInfo ast2700fc_types[] = {
    {
        .name           = MACHINE_TYPE_NAME("ast2700fc"),
        .parent         = TYPE_MACHINE,
        .class_init     = ast2700fc_class_init,
        .instance_size  = sizeof(Ast2700FCState),
        .instance_init  = ast2700fc_instance_init,
    },
};

DEFINE_TYPES(ast2700fc_types)
