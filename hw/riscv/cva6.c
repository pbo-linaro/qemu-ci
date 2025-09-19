/*
 * QEMU RISC-V Board for OpenHW CVA6 SoC
 *
 * Copyright (c) 2025 Codethink Ltd
 * Ben Dooks <ben.dooks@codethink.co.uk>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "hw/boards.h"
#include "hw/irq.h"
#include "hw/loader.h"
#include "hw/sysbus.h"
#include "hw/misc/unimp.h"

#include "hw/sd/sd.h"
#include "hw/ssi/ssi.h"

#include "hw/riscv/cva6.h"
#include "hw/riscv/boot.h"
#include "hw/intc/riscv_aclint.h"

#include "system/system.h"
#include "system/device_tree.h"

#include <libfdt.h>

#define CVA6_ROM_BASE  0x10000

static const MemMapEntry cva6_memmap[] = {
    [CVA6_DEBUG] =              {  0x0000000,  0x1000 },
    [CVA6_ROM] =                { CVA6_ROM_BASE, 0x10000 },
    [CVA6_CLINT] =              {  0x2000000, 0xC0000 },
    [CVA6_PLIC] =               {  0xC000000, 0x4000000 },
    [CVA6_UART] =               { 0x10000000, 0x1000 },
    [CVA6_TIMER] =              { 0x18000000, 0x10000 },
    [CVA6_SPI] =                { 0x20000000, 0x800000 },
    [CVA6_ETHERNET] =           { 0x30000000, 0x10000 },
    [CVA6_GPIO] =               { 0x40000000, 0x1000 },
    [CVA6_DRAM] =               { 0x80000000, 0x40000000 },
};

static void cva6_machine_init(MachineState *machine)
{
    MachineClass *mc = MACHINE_GET_CLASS(machine);
    MemoryRegion *sys_mem = get_system_memory();
    hwaddr dram_addr = cva6_memmap[CVA6_DRAM].base;
    hwaddr dram_size = cva6_memmap[CVA6_DRAM].size;
    CVA6State *s = CVA6_MACHINE(machine);
    RISCVBootInfo boot_info;

    object_initialize_child(OBJECT(machine), "soc", &s->soc, TYPE_RISCV_CVA6);
    qdev_realize(DEVICE(&s->soc), NULL, &error_fatal);

    if (machine->ram_size > mc->default_ram_size) {
        error_report("RAM size is too big for DRAM area");
        exit(EXIT_FAILURE);
    }

    memory_region_add_subregion(sys_mem, dram_addr, machine->ram);
    riscv_boot_info_init(&boot_info, &s->soc.cpus);

    /* support two booting methods, either by supplying the bootrom as
     * -firmware or supplying a kernel and fdt file that's loaded and
     * executed via a fake boot vector
     */
    
    if (machine->firmware) {
         hwaddr firmware_load_addr = cva6_memmap[CVA6_ROM].base;
         riscv_load_firmware(machine->firmware, &firmware_load_addr, NULL);
    }

     if (machine->kernel_filename) {
         uint64_t fdt_load_addr;

         riscv_load_kernel(machine, &boot_info, dram_addr, false, NULL);

         if (machine->dtb) {
             int fdt_size;

             machine->fdt = load_device_tree(machine->dtb, &fdt_size);
             if (!machine->fdt) {
                error_report("load_device_tree() failed");
                exit(1);
             }

             fdt_load_addr = riscv_compute_fdt_addr(dram_addr, dram_size,
                                                    machine, &boot_info);

             riscv_load_fdt(fdt_load_addr, machine->fdt);
         } else {
             warn_report_once("no device tree file provided for kernel boot");
             fdt_load_addr = 0x0;
         }

         /* kernel only, let's use the bootrom to build a simple resetvec
          * to start the kernel
          */

         riscv_setup_rom_reset_vec(machine, &s->soc.cpus,
                                   boot_info.image_low_addr,
                                   cva6_memmap[CVA6_ROM].base,
                                   cva6_memmap[CVA6_ROM].size,
                                   dram_addr, fdt_load_addr);
    }
}

static void cva6_machine_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "RISC-V board for CVA6";
    mc->init = cva6_machine_init;
    mc->max_cpus = 1;
    mc->default_ram_id = "cva6.ram";
    mc->default_cpu_type = TYPE_RISCV_CPU_CVA6;
    mc->default_ram_size = cva6_memmap[CVA6_DRAM].size;
};

static void cva6_soc_init(Object *obj)
{
    CVA6SoCState *s = RISCV_CVA6(obj);

    object_initialize_child(obj, "cpus", &s->cpus, TYPE_RISCV_HART_ARRAY);
}

static void cva6_add_spi(CVA6SoCState *s, const MemMapEntry *map)
{
    DriveInfo *dinfo;
    BlockBackend *blk;
    DeviceState *card_dev;
    qemu_irq sd_cs;
    DeviceState *sddev;
    SysBusDevice *busdev;
    DeviceState *spi_dev;
    SSIBus *spi;

    spi_dev = qdev_new("xlnx.xps-spi");
    qdev_prop_set_uint8(spi_dev, "num-ss-bits", 1);
    qdev_prop_set_string(spi_dev, "endianness", "little");

    busdev = SYS_BUS_DEVICE(spi_dev);
    sysbus_realize_and_unref(busdev, &error_fatal);
    sysbus_mmio_map(busdev, 0, map->base);
    sysbus_connect_irq(busdev, 0, qdev_get_gpio_in(DEVICE(s->plic), CVA6_SPI_IRQ));

    spi = (SSIBus *)qdev_get_child_bus(spi_dev, "spi");

    sddev = ssi_create_peripheral(spi, "ssi-sd");
    sd_cs = qdev_get_gpio_in_named(sddev, SSI_GPIO_CS, 0);
    sysbus_connect_irq(busdev, 1, sd_cs);

    dinfo = drive_get(IF_SD, 0, 0);
    blk = dinfo ? blk_by_legacy_dinfo(dinfo) : NULL;
    card_dev = qdev_new(TYPE_SD_CARD_SPI);
    qdev_prop_set_drive_err(card_dev, "drive", blk, &error_fatal);

    qdev_realize_and_unref(card_dev, qdev_get_child_bus(sddev, "sd-bus"), &error_fatal);
}

static void not_implemented(const char *name, const MemMapEntry *map)
{
    create_unimplemented_device(name, map->base, map->size);
}

static void cva6_soc_realize(DeviceState *dev_soc, Error **errp)
{
    MemoryRegion *system_memory = get_system_memory();
    MachineState *ms = MACHINE(qdev_get_machine());
    CVA6SoCState *s = RISCV_CVA6(dev_soc);
    const MemMapEntry *memmap = cva6_memmap;
    MemoryRegion *rom = g_new(MemoryRegion, 1);
    g_autofree char *plic_hart_config;

    object_property_set_str(OBJECT(&s->cpus), "cpu-type", ms->cpu_type,
                            &error_abort);
    object_property_set_int(OBJECT(&s->cpus), "num-harts", ms->smp.cpus,
                            &error_abort);
    object_property_set_int(OBJECT(&s->cpus), "resetvec", CVA6_ROM_BASE,
                            &error_abort);
    sysbus_realize(SYS_BUS_DEVICE(&s->cpus), &error_fatal);

    /* boot rom */
    memory_region_init_rom(rom, OBJECT(dev_soc), "riscv.cva6.bootrom",
                           memmap[CVA6_ROM].size, &error_fatal);
    memory_region_add_subregion(system_memory, memmap[CVA6_ROM].base,
                                rom);

    /* create PLIC hart topology configuration string */
    plic_hart_config = riscv_plic_hart_config_string(ms->smp.cpus);

    /* MMIO */
    s->plic = sifive_plic_create(memmap[CVA6_PLIC].base,
        plic_hart_config, ms->smp.cpus, 0,
        CVA6_PLIC_NUM_SOURCES,
        CVA6_PLIC_NUM_PRIORITIES,
        CVA6_PLIC_PRIORITY_BASE,
        CVA6_PLIC_PENDING_BASE,
        CVA6_PLIC_ENABLE_BASE,
        CVA6_PLIC_ENABLE_STRIDE,
        CVA6_PLIC_CONTEXT_BASE,
        CVA6_PLIC_CONTEXT_STRIDE,
        memmap[CVA6_PLIC].size);

    riscv_aclint_swi_create(memmap[CVA6_CLINT].base, 0,
                            ms->smp.cpus, false);

    riscv_aclint_mtimer_create(
        memmap[CVA6_CLINT].base + RISCV_ACLINT_SWI_SIZE,
        RISCV_ACLINT_DEFAULT_MTIMER_SIZE, 0, ms->smp.cpus,
        RISCV_ACLINT_DEFAULT_MTIMECMP, RISCV_ACLINT_DEFAULT_MTIME,
        CLINT_TIMEBASE_FREQ, true);

    /* something in cva6-sdk uboot seems to prod the debug
     * unit by accident, so make it not implemented.
     */
    not_implemented("debug", &memmap[CVA6_DEBUG]);

    /* 16550 uart, one 32bit register per 32bit word */

    serial_mm_init(system_memory, memmap[CVA6_UART].base, 2,
                   qdev_get_gpio_in(DEVICE(s->plic), CVA6_UART_IRQ),
                   50*1000*10000,
                   serial_hd(0), DEVICE_LITTLE_ENDIAN);

    /* just unimplement the timers, network and gpio here for now.
     * no-one seems to be using the apb timer block anyway,
     */
    not_implemented("net", &memmap[CVA6_ETHERNET]);
    not_implemented("gpio", &memmap[CVA6_GPIO]);
    not_implemented("timer", &memmap[CVA6_TIMER]);

    /* connect xilinx spi block here */
    cva6_add_spi(s, &memmap[CVA6_SPI]);
}

static void cva6_soc_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = cva6_soc_realize;
    dc->user_creatable = false;
};

static const TypeInfo cva6_types[] = {
    {
        .name           = TYPE_RISCV_CVA6,
        .parent         = TYPE_DEVICE,
        .instance_size  = sizeof(CVA6SoCState),
        .instance_init  = cva6_soc_init,
        .class_init     = cva6_soc_class_init,
    }, {
        .name           = TYPE_CVA6_MACHINE,
        .parent         = TYPE_MACHINE,
        .instance_size  = sizeof(CVA6State),
        .class_init     = cva6_machine_class_init,
    }
};

DEFINE_TYPES(cva6_types)
