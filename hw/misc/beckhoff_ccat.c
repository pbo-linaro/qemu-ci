/*
 * Beckhoff Communication Controller Emulation
 *
 * Copyright (c) Beckhoff Automation GmbH. & Co. KG
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/register.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "system/block-backend.h"
#include "exec/address-spaces.h"
#include "exec/memory.h"
#include "system/dma.h"
#include "qemu/error-report.h"
#include "block/block.h"
#include "block/block_int.h"
#include "block/qdict.h"
#include "hw/block/block.h"

#ifndef CCAT_ERR_DEBUG
#define CCAT_ERR_DEBUG 0
#endif

#define DB_PRINT_L(level, ...) do { \
    if (CCAT_ERR_DEBUG > (level)) { \
        fprintf(stderr,  ": %s: ", __func__); \
        fprintf(stderr, ## __VA_ARGS__); \
    } \
} while (0)

#define DB_PRINT(...) DB_PRINT_L(0, ## __VA_ARGS__)

#define TYPE_BECKHOFF_CCAT "beckhoff-ccat"
#define BECKHOFF_CCAT(obj) \
    OBJECT_CHECK(BeckhoffCcat, (obj), TYPE_BECKHOFF_CCAT)

#define MAX_NUM_SLOTS 32

#define CCAT_EEPROM_OFFSET 0x100
#define CCAT_DMA_OFFSET 0x8000

#define CCAT_MEM_SIZE 0xFFFF
#define CCAT_DMA_SIZE 0x800
#define CCAT_EEPROM_SIZE 0x20

#define EEPROM_MEMORY_SIZE 0x1000

#define EEPROM_CMD_OFFSET (CCAT_EEPROM_OFFSET + 0x00)
    #define EEPROM_CMD_WRITE_MASK 0x2
    #define EEPROM_CMD_READ_MASK 0x1
#define EEPROM_ADR_OFFSET (CCAT_EEPROM_OFFSET + 0x04)
#define EEPROM_DATA_OFFSET (CCAT_EEPROM_OFFSET + 0x08)

#define DMA_BUFFER_OFFSET (CCAT_DMA_OFFSET + 0x00)
#define DMA_DIRECTION_OFFSET (CCAT_DMA_OFFSET + 0x7c0)
    #define DMA_DIRECTION_MASK 1
#define DMA_TRANSFER_OFFSET (CCAT_DMA_OFFSET + 0x7c4)
#define DMA_HOST_ADR_OFFSET (CCAT_DMA_OFFSET + 0x7c8)
#define DMA_TRANSFER_LENGTH_OFFSET (CCAT_DMA_OFFSET + 0x7cc)

/*
 * The informationblock  is always located at address 0x0.
 * Address and size are therefor replaced by two identifiers.
 * The Parameter give information about the maximal number of
 * function slots and the creation date (in this case 01.01.2001)
 */
#define CCAT_ID_1 0x88a4
#define CCAT_ID_2 0x54414343
#define CCAT_INFO_BLOCK_PARAMS (MAX_NUM_SLOTS << 0) | (0x1 << 8) | \
                              (0x1 << 16) | (0x1 << 24)

#define CCAT_FUN_TYPE_ENTRY 0x0001
#define CCAT_FUN_TYPE_EEPROM 0x0012
#define CCAT_FUN_TYPE_DMA 0x0013

typedef struct BeckhoffCcat {
    SysBusDevice parent_obj;

    MemoryRegion iomem;

    uint8_t mem[CCAT_MEM_SIZE];

    BlockBackend *eeprom_blk;
    uint8_t *eeprom_storage;
    int64_t eeprom_size;
} BeckhoffCcat;

typedef struct __attribute__((packed)) CcatFunctionBlock {
    uint16_t type;
    uint16_t revision;
    uint32_t parameter;
    uint32_t address_offset;
    uint32_t size;
} CcatFunctionBlock;

static void sync_eeprom(BeckhoffCcat *s)
{
    if (!s->eeprom_blk) {
        return;
    }
    blk_pwrite(s->eeprom_blk, 0, s->eeprom_size, s->eeprom_storage, 0);
}

static uint64_t beckhoff_ccat_eeprom_read(void *opaque, hwaddr addr,
                                          unsigned size)
{
    BeckhoffCcat *s = opaque;
    uint64_t val = 0;
    memcpy(&val, &s->mem[addr], size);
    return val;
}

static void beckhoff_ccat_eeprom_write(void *opaque, hwaddr addr, uint64_t val,
                              unsigned size)
{
    BeckhoffCcat *s = opaque;
    uint64_t eeprom_adr;
    switch (addr) {
    case EEPROM_CMD_OFFSET:
        eeprom_adr = *(uint32_t *)&s->mem[EEPROM_ADR_OFFSET];
        eeprom_adr = (eeprom_adr * 2) % s->eeprom_size;
        if (val & EEPROM_CMD_READ_MASK) {
            uint64_t buf = 0;
            uint32_t bytes_to_read = 8;
            if (eeprom_adr > s->eeprom_size - 8) {
                bytes_to_read = s->eeprom_size - eeprom_adr;
            }
            memcpy(&buf, s->eeprom_storage + eeprom_adr, bytes_to_read);
            *(uint64_t *)&s->mem[EEPROM_DATA_OFFSET] = buf;

        } else if (val & EEPROM_CMD_WRITE_MASK) {
            uint32_t buf = *(uint32_t *)&s->mem[EEPROM_DATA_OFFSET];
            memcpy(s->eeprom_storage + eeprom_adr, &buf, 2);
            sync_eeprom(s);
        }
        break;
    default:
            memcpy(&s->mem[addr], &val, size);
    }
}

static uint64_t beckhoff_ccat_dma_read(void *opaque, hwaddr addr, unsigned size)
{
    BeckhoffCcat *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case DMA_TRANSFER_OFFSET:
        if (s->mem[DMA_TRANSFER_OFFSET] & 0x1) {
            DB_PRINT("DMA transfer finished\n");
            s->mem[DMA_TRANSFER_OFFSET] = 0;
        }
        break;
    }
    memcpy(&val, &s->mem[addr], size);
    return val;
}

static void beckhoff_ccat_dma_write(void *opaque, hwaddr addr, uint64_t val,
                           unsigned size)
{
    BeckhoffCcat *s = opaque;
    switch (addr) {
    case DMA_TRANSFER_OFFSET:
        uint8_t len = s->mem[DMA_TRANSFER_LENGTH_OFFSET];
        uint8_t *mem_buf = &s->mem[DMA_BUFFER_OFFSET];

        if (s->mem[DMA_DIRECTION_OFFSET] & DMA_DIRECTION_MASK) {
            dma_addr_t dmaAddr = *(uint32_t *)&s->mem[DMA_HOST_ADR_OFFSET];
            dma_memory_read(&address_space_memory, dmaAddr,
                            mem_buf, len * 8, MEMTXATTRS_UNSPECIFIED);
        } else {
            dma_addr_t dmaAddr = *(uint32_t *)&s->mem[DMA_HOST_ADR_OFFSET];
            dma_memory_write(&address_space_memory, dmaAddr + 8,
                                mem_buf, len * 8, MEMTXATTRS_UNSPECIFIED);
        }
        break;
    }
    memcpy(&s->mem[addr], &val, size);
}
static uint64_t beckhoff_ccat_read(void *opaque, hwaddr addr, unsigned size)
{
    DB_PRINT("CCAT_READ addr=0x%lx size=%u\n", addr, size);

    BeckhoffCcat *s = opaque;
    uint64_t val = 0;

    if (addr > CCAT_MEM_SIZE - size) {
        error_report("Overflow. Address or size is too large.\n");
        exit(1);
    }

    if (addr >= CCAT_EEPROM_OFFSET &&
                        addr <= CCAT_EEPROM_OFFSET + s->eeprom_size) {
        return beckhoff_ccat_eeprom_read(opaque, addr, size);
    } else if (addr >= CCAT_DMA_OFFSET &&
                        addr <= CCAT_DMA_OFFSET + CCAT_DMA_SIZE) {
        return beckhoff_ccat_dma_read(opaque, addr, size);
    } else {
         memcpy(&val, &s->mem[addr], size);
    }

    return val;
}

static void beckhoff_ccat_write(void *opaque, hwaddr addr, uint64_t val,
                                unsigned size)
{
    DB_PRINT("CCAT_WRITE addr=0x%lx size=%u val=0x%lx\n", addr, size, val);

    BeckhoffCcat *s = opaque;

    if (addr > CCAT_MEM_SIZE - size) {
        error_report("Overflow. Address or size is too large.\n");
        exit(1);
    }

    if (addr >= CCAT_EEPROM_OFFSET &&
                        addr <= CCAT_EEPROM_OFFSET + s->eeprom_size) {
        beckhoff_ccat_eeprom_write(opaque, addr, val, size);
    } else if (addr >= CCAT_DMA_OFFSET &&
                        addr <= CCAT_DMA_OFFSET + CCAT_DMA_SIZE) {
        beckhoff_ccat_dma_write(opaque, addr, val, size);
    } else {
        memcpy(&s->mem[addr], &val, size);
    }
}

static const MemoryRegionOps beckhoff_ccat_ops = {
    .read = beckhoff_ccat_read,
    .write = beckhoff_ccat_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 8,
    },
};


static void beckhoff_ccat_reset(DeviceState *dev)
{
    BeckhoffCcat *s = BECKHOFF_CCAT(dev);

    CcatFunctionBlock function_blocks[MAX_NUM_SLOTS] = {0};

    CcatFunctionBlock info_block = {
        .type = CCAT_FUN_TYPE_ENTRY,
        .revision = 0x0001,
        .parameter = CCAT_INFO_BLOCK_PARAMS,
        .address_offset = CCAT_ID_1,
        .size = CCAT_ID_2
    };
    CcatFunctionBlock eeprom_block = {
        .type = CCAT_FUN_TYPE_EEPROM,
        .revision = 0x0001,
        .parameter = 0,
        .address_offset = CCAT_EEPROM_OFFSET,
        .size = CCAT_EEPROM_SIZE
    };
    CcatFunctionBlock dma_block = {
        .type = CCAT_FUN_TYPE_DMA,
        .revision = 0x0000,
        .parameter = 0,
        .address_offset = CCAT_DMA_OFFSET,
        .size = CCAT_DMA_SIZE
    };

    /*
     * The EEPROM interface is usually at function slot 11.
     * The DMA controller is usually at function slot 15.
     */
    function_blocks[0] = info_block;
    function_blocks[11] = eeprom_block;
    function_blocks[15] = dma_block;

    memcpy(&s->mem[0], function_blocks, sizeof(function_blocks));
}

static void beckhoff_ccat_realize(DeviceState *dev, Error **errp)
{
    BeckhoffCcat *s = BECKHOFF_CCAT(dev);
    BlockBackend *blk;

    blk = blk_by_name("ccat-eeprom");

    if (blk) {
        uint64_t blk_size = blk_getlength(blk);
        if (!is_power_of_2(blk_size)) {
            error_report("Blockend size is not a power of two.");
        }

        if (blk_size < 512) {
            error_report("Blockend size is too small. Using backup.");
            s->eeprom_size = EEPROM_MEMORY_SIZE;
            s->eeprom_storage = blk_blockalign(NULL, s->eeprom_size);
            memset(s->eeprom_storage, 0x00, s->eeprom_size);
        } else {
            DB_PRINT("EEPROM block backend found\n");
            blk_set_perm(blk, BLK_PERM_WRITE, BLK_PERM_ALL, errp);

            s->eeprom_size = blk_size;
            s->eeprom_blk = blk;
            s->eeprom_storage = blk_blockalign(s->eeprom_blk, s->eeprom_size);

            if (!blk_check_size_and_read_all(s->eeprom_blk, DEVICE(s),
                                             s->eeprom_storage, s->eeprom_size,
                                             errp)) {
                exit(1);
            }
        }
    } else {
        s->eeprom_size = EEPROM_MEMORY_SIZE;
        s->eeprom_storage = blk_blockalign(NULL, s->eeprom_size);
        memset(s->eeprom_storage, 0x00, s->eeprom_size);
    }

    beckhoff_ccat_reset(dev);
}

static void beckhoff_ccat_init(Object *obj)
{
    BeckhoffCcat *s = BECKHOFF_CCAT(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &beckhoff_ccat_ops, s,
                          TYPE_BECKHOFF_CCAT, CCAT_MEM_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
}

static void beckhoff_ccat_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->realize = beckhoff_ccat_realize;
}

static const TypeInfo beckhoff_ccat_info = {
 .name = TYPE_BECKHOFF_CCAT,
 .parent = TYPE_SYS_BUS_DEVICE,
 .instance_size = sizeof(BeckhoffCcat),
 .class_init = beckhoff_ccat_class_init,
 .instance_init = beckhoff_ccat_init,
};

static void beckhoff_ccat_register_types(void)
{
    type_register_static(&beckhoff_ccat_info);
}

type_init(beckhoff_ccat_register_types)
