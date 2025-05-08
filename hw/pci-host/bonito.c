/*
 * Algorithmics Ltd BONITO north bridge emulation
 *
 * Copyright (c) 2008 yajin (yajin@vm-kernel.org)
 * Copyright (c) 2010 Huacai Chen (zltjiangshi@gmail.com)
 *
 * This code is licensed under the GNU GPL v2.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 *
 * For 32-bit variant:
 * "BONITO - PCI/SDRAM System Controller for Vr43xx"
 * https://wiki.qemu.org/File:Bonito-spec.pdf
 *
 * "BONITO - Companion Chip for Vr43xx and Vr5xxx" (uPD65949S1-P00-F6)
 * https://repo.oss.cipunited.com/archives/docs/NEC/U15789EE1V0DS00.pdf
 *
 * For 64-bit variant:
 * "BONITO64 - "north bridge" controller for 64-bit MIPS CPUs"
 * https://wiki.qemu.org/File:Bonito-spec.pdf
 *
 * For Godson (Loongson) 2E variant:
 * "Godson 2E North Bridge User Manual" (in Chinese)
 * https://github.com/loongson-community/docs/blob/master/2E/Godson_2E_NB_UM.pdf
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "hw/pci/pci_device.h"
#include "hw/irq.h"
#include "hw/mips/mips.h"
#include "hw/pci-host/bonito.h"
#include "hw/pci/pci_host.h"
#include "migration/vmstate.h"
#include "system/runstate.h"
#include "hw/misc/unimp.h"
#include "hw/registerfields.h"
#include "qom/object.h"
#include "trace.h"

/* #define DEBUG_BONITO */

#ifdef DEBUG_BONITO
#define DPRINTF(fmt, ...) fprintf(stderr, "%s: " fmt, __func__, ##__VA_ARGS__)
#else
#define DPRINTF(fmt, ...)
#endif

/* from linux source code. include/asm-mips/mips-boards/bonito64.h*/
#define BONITO_BOOT_BASE        0x1fc00000
#define BONITO_BOOT_SIZE        0x00100000
#define BONITO_BOOT_TOP         (BONITO_BOOT_BASE + BONITO_BOOT_SIZE - 1)
#define BONITO_FLASH_BASE       0x1c000000
#define BONITO_FLASH_SIZE       0x03000000
#define BONITO_FLASH_TOP        (BONITO_FLASH_BASE + BONITO_FLASH_SIZE - 1)
#define BONITO_SOCKET_BASE      0x1f800000
#define BONITO_SOCKET_SIZE      0x00400000
#define BONITO_SOCKET_TOP       (BONITO_SOCKET_BASE + BONITO_SOCKET_SIZE - 1)
#define BONITO_REG_BASE         0x1fe00000
#define BONITO_REG_SIZE         0x00040000
#define BONITO_REG_TOP          (BONITO_REG_BASE + BONITO_REG_SIZE - 1)
#define BONITO_DEV_BASE         0x1ff00000
#define BONITO_DEV_SIZE         0x00100000
#define BONITO_DEV_TOP          (BONITO_DEV_BASE + BONITO_DEV_SIZE - 1)
#define BONITO_PCILO_BASE       0x10000000
#define BONITO_PCILO_BASE_VA    0xb0000000
#define BONITO_PCILO_SIZE       0x0c000000
#define BONITO_PCILO_TOP        (BONITO_PCILO_BASE + BONITO_PCILO_SIZE - 1)
#define BONITO_PCILO0_BASE      0x10000000
#define BONITO_PCILO1_BASE      0x14000000
#define BONITO_PCILO2_BASE      0x18000000
#define BONITO_PCIHI_BASE       0x20000000
#define BONITO_PCIHI_SIZE       0x60000000
#define BONITO_PCIHI_TOP        (BONITO_PCIHI_BASE + BONITO_PCIHI_SIZE - 1)
#define BONITO_PCIIO_BASE       0x1fd00000
#define BONITO_PCIIO_BASE_VA    0xbfd00000
#define BONITO_PCIIO_SIZE       0x00010000
#define BONITO_PCIIO_TOP        (BONITO_PCIIO_BASE + BONITO_PCIIO_SIZE - 1)
#define BONITO_PCICFG_BASE      0x1fe80000
#define BONITO_PCICFG_SIZE      0x00080000
#define BONITO_PCICFG_TOP       (BONITO_PCICFG_BASE + BONITO_PCICFG_SIZE - 1)


#define BONITO_PCICONFIGBASE    0x00
#define BONITO_REGBASE          0x100

#define BONITO_PCICONFIG_BASE   (BONITO_PCICONFIGBASE + BONITO_REG_BASE)
#define BONITO_PCICONFIG_SIZE   (0x100)

#define BONITO_INTERNAL_REG_BASE  (BONITO_REGBASE + BONITO_REG_BASE)
#define BONITO_INTERNAL_REG_SIZE  (0x70)

/* 1. Bonito h/w Configuration */
/* Power on register */

#define BONITO_BONPONCFG        (0x00 >> 2)      /* 0x100 */

/* PCI configuration register */
#define BONITO_BONGENCFG_OFFSET 0x4
#define BONITO_BONGENCFG        (BONITO_BONGENCFG_OFFSET >> 2)   /*0x104 */
REG32(BONGENCFG,        0x104)
FIELD(BONGENCFG, DEBUGMODE,      0, 1)
FIELD(BONGENCFG, SNOOP,          1, 1)
FIELD(BONGENCFG, CPUSELFRESET,   2, 1)
FIELD(BONGENCFG, BYTESWAP,       6, 1)
FIELD(BONGENCFG, UNCACHED,       7, 1)
FIELD(BONGENCFG, PREFETCH,       8, 1)
FIELD(BONGENCFG, WRITEBEHIND,    9, 1)
FIELD(BONGENCFG, PCIQUEUE,      12, 1)

/* 2. IO & IDE configuration */
#define BONITO_IODEVCFG         (0x08 >> 2)      /* 0x108 */

/* 3. IO & IDE configuration */
#define BONITO_SDCFG            (0x0c >> 2)      /* 0x10c */

/* 4. PCI address map control */
#define BONITO_PCIMAP           (0x10 >> 2)      /* 0x110 */
REG32(PCIMAP,        0x110)
FIELD(PCIMAP, LO0, 0, 6)
FIELD(PCIMAP, LO1, 6, 6)
FIELD(PCIMAP, LO2, 12, 6)
FIELD(PCIMAP, 2, 18, 1)

#define BONITO_PCIMEMBASECFG    (0x14 >> 2)      /* 0x114 */
REG32(PCIMEMBASECFG, 0x114)
FIELD(PCIMEMBASECFG, MASK0, 0, 5)
FIELD(PCIMEMBASECFG, TRANS0, 5, 5)
FIELD(PCIMEMBASECFG, CACHED0, 10, 1)
FIELD(PCIMEMBASECFG, IO0, 11, 1)
FIELD(PCIMEMBASECFG, MASK1, 12, 5)
FIELD(PCIMEMBASECFG, TRANS1, 17, 5)
FIELD(PCIMEMBASECFG, CACHED1, 22, 1)
FIELD(PCIMEMBASECFG, IO1, 23, 1)


#define BONITO_PCIMAP_CFG       (0x18 >> 2)      /* 0x118 */
REG32(PCIMAP_CFG,    0x118)
FIELD(PCIMAP_CFG, AD16UP, 0, 16)
FIELD(PCIMAP_CFG, TYPE1, 16, 1)

/* 5. ICU & GPIO regs */
/* GPIO Regs - r/w */
#define BONITO_GPIODATA_OFFSET  0x1c
#define BONITO_GPIODATA         (BONITO_GPIODATA_OFFSET >> 2)   /* 0x11c */
#define BONITO_GPIOIE           (0x20 >> 2)      /* 0x120 */

/* ICU Configuration Regs - r/w */
#define BONITO_INTEDGE          (0x24 >> 2)      /* 0x124 */
#define BONITO_INTSTEER         (0x28 >> 2)      /* 0x128 */
#define BONITO_INTPOL           (0x2c >> 2)      /* 0x12c */

/* ICU Enable Regs - IntEn & IntISR are r/o. */
#define BONITO_INTENSET         (0x30 >> 2)      /* 0x130 */
#define BONITO_INTENCLR         (0x34 >> 2)      /* 0x134 */
#define BONITO_INTEN            (0x38 >> 2)      /* 0x138 */
#define BONITO_INTISR           (0x3c >> 2)      /* 0x13c */

/* ICU Pins */
#define ICU_PIN_MBOXx(x)        (0 + (x))
#define ICU_PIN_DMARDY          4
#define ICU_PIN_DMAEMPTY        5
#define ICU_PIN_COPYRDY         6
#define ICU_PIN_COPYEMPTY       7
#define ICU_PIN_COPYERR         8
#define ICU_PIN_PCIIRQ          9
#define ICU_PIN_MASTERERR       10
#define ICU_PIN_SYSTEMERR       11
#define ICU_PIN_DRAMPERR        12
#define ICU_PIN_RETRYERR        13
#define ICU_PIN_INTTIMER        14
#define ICU_PIN_GPIOx(x)        (16 + (x))
#define ICU_PIN_GPINx(x)        (25 + (x))

/* PCI mail boxes */
#define BONITO_PCIMAIL0_OFFSET    0x40
#define BONITO_PCIMAIL1_OFFSET    0x44
#define BONITO_PCIMAIL2_OFFSET    0x48
#define BONITO_PCIMAIL3_OFFSET    0x4c
#define BONITO_PCIMAIL0         (0x40 >> 2)      /* 0x140 */
#define BONITO_PCIMAIL1         (0x44 >> 2)      /* 0x144 */
#define BONITO_PCIMAIL2         (0x48 >> 2)      /* 0x148 */
#define BONITO_PCIMAIL3         (0x4c >> 2)      /* 0x14c */

/* 6. PCI cache */
#define BONITO_PCICACHECTRL     (0x50 >> 2)      /* 0x150 */
#define BONITO_PCICACHETAG      (0x54 >> 2)      /* 0x154 */
#define BONITO_PCIBADADDR       (0x58 >> 2)      /* 0x158 */
#define BONITO_PCIMSTAT         (0x5c >> 2)      /* 0x15c */

/* 7. other*/
#define BONITO_TIMECFG          (0x60 >> 2)      /* 0x160 */
#define BONITO_CPUCFG           (0x64 >> 2)      /* 0x164 */
#define BONITO_DQCFG            (0x68 >> 2)      /* 0x168 */
#define BONITO_MEMSIZE          (0x6C >> 2)      /* 0x16c */

#define BONITO_REGS             (0x70 >> 2)

/* PCI Access Cycle Fields */
FIELD(TYPE0_CYCLE, FUNC, 8, 3)
FIELD(TYPE0_CYCLE, IDSEL, 11, 21)

FIELD(TYPE1_CYCLE, FUNC, 8, 3)
FIELD(TYPE1_CYCLE, DEV, 11, 5)
FIELD(TYPE1_CYCLE, BUS, 16, 8)
FIELD(TYPE1_CYCLE, IDSEL, 24, 8)

typedef struct BonitoState BonitoState;

struct PCIBonitoState {
    PCIDevice dev;

    BonitoState *pcihost;
    uint32_t regs[BONITO_REGS];
    uint32_t icu_pin_state;

    struct bonldma {
        uint32_t ldmactrl;
        uint32_t ldmastat;
        uint32_t ldmaaddr;
        uint32_t ldmago;
    } bonldma;

    /* Based at 1fe00300, bonito Copier */
    struct boncop {
        uint32_t copctrl;
        uint32_t copstat;
        uint32_t coppaddr;
        uint32_t copgo;
    } boncop;

    /* Bonito registers */
    MemoryRegion iomem;
    MemoryRegion iomem_ldma;
    MemoryRegion iomem_cop;
    MemoryRegion bonito_pciio;
    MemoryRegion bonito_localio;
};
typedef struct PCIBonitoState PCIBonitoState;

struct BonitoState {
    PCIHostState parent_obj;
    qemu_irq *pic;
    PCIBonitoState *pci_dev;
    MemoryRegion dma_mr;
    MemoryRegion pci_mem;
    AddressSpace dma_as;
    MemoryRegion *pcimem_lo_alias;
    MemoryRegion *pcimem_hi_alias;
    MemoryRegion *dma_alias;
};

#define TYPE_PCI_BONITO "Bonito"
OBJECT_DECLARE_SIMPLE_TYPE(PCIBonitoState, PCI_BONITO)

static void bonito_update_irq(PCIBonitoState *s)
{
    BonitoState *bs = s->pcihost;
    uint32_t inten = s->regs[BONITO_INTEN];
    uint32_t intisr = s->regs[BONITO_INTISR];
    uint32_t intpol = s->regs[BONITO_INTPOL];
    uint32_t intedge = s->regs[BONITO_INTEDGE];
    uint32_t pin_state = s->icu_pin_state;
    uint32_t level, edge;

    pin_state = (pin_state & ~intpol) | (~pin_state & intpol);

    level = pin_state & ~intedge;
    edge = (pin_state & ~intisr) & intedge;

    intisr = (intisr & intedge) | level;
    intisr |= edge;
    intisr &= inten;

    s->regs[BONITO_INTISR] = intisr;

    qemu_set_irq(*bs->pic, !!intisr);
}

static void bonito_set_irq(void *opaque, int irq, int level)
{
    BonitoState *bs = opaque;
    PCIBonitoState *s = bs->pci_dev;

    s->icu_pin_state = deposit32(s->icu_pin_state, irq, 1, !!level);

    bonito_update_irq(s);
}

static void bonito_update_pcimap(PCIBonitoState *s)
{
    uint32_t pcimap = s->regs[BONITO_PCIMAP];

    memory_region_set_alias_offset(&s->pcihost->pcimem_lo_alias[0],
                                   FIELD_EX32(pcimap, PCIMAP, LO0) << 26);
    memory_region_set_alias_offset(&s->pcihost->pcimem_lo_alias[1],
                                   FIELD_EX32(pcimap, PCIMAP, LO1) << 26);
    memory_region_set_alias_offset(&s->pcihost->pcimem_lo_alias[2],
                                   FIELD_EX32(pcimap, PCIMAP, LO2) << 26);
    memory_region_set_alias_offset(s->pcihost->pcimem_hi_alias,
                                   FIELD_EX32(pcimap, PCIMAP, 2) << 31);
}

static void pcibasecfg_decode(uint32_t mask, uint32_t trans, bool io,
                                     uint32_t *base, uint32_t *size)
{
    uint32_t val;

    mask = (mask << 23 | 0xF0000000);
    val = ctz32(mask);
    *size = 1 << val;
    *base = (trans & ~(*size - 1)) | io << 28;
}

static void bonito_update_pcibase(PCIBonitoState *s)
{
    uint32_t pcibasecfg = s->regs[BONITO_PCIMEMBASECFG];
    uint32_t size, base;
    uint32_t pcibase, wmask;

    pcibasecfg_decode(FIELD_EX32(pcibasecfg, PCIMEMBASECFG, MASK0),
                      FIELD_EX32(pcibasecfg, PCIMEMBASECFG, TRANS0),
                      FIELD_EX32(pcibasecfg, PCIMEMBASECFG, IO0),
                      &base, &size);

    wmask = ~(size - 1);
    /* Mask will also influence PCIBase register writable range */
    pci_set_long(s->dev.wmask + PCI_BASE_ADDRESS_0, wmask);
    /* Clear RO bits in PCIBase */
    pcibase = pci_get_long(s->dev.config + PCI_BASE_ADDRESS_0);
    pcibase &= wmask;
    pci_set_long(s->dev.config + PCI_BASE_ADDRESS_0, pcibase);
    /* Adjust DMA spaces */
    memory_region_set_size(&s->pcihost->dma_alias[0], size);
    memory_region_set_alias_offset(&s->pcihost->dma_alias[0], base);
    memory_region_set_address(&s->pcihost->dma_alias[0], pcibase);

    /* Ditto for PCIMEMBASECFG1 */
    pcibasecfg_decode(FIELD_EX32(pcibasecfg, PCIMEMBASECFG, MASK1),
                      FIELD_EX32(pcibasecfg, PCIMEMBASECFG, TRANS1),
                      FIELD_EX32(pcibasecfg, PCIMEMBASECFG, IO1),
                      &base, &size);

    wmask = ~(size - 1);
    pci_set_long(s->dev.wmask + PCI_BASE_ADDRESS_1, wmask);
    pcibase = pci_get_long(s->dev.config + PCI_BASE_ADDRESS_1);
    pcibase &= wmask;
    pci_set_long(s->dev.config + PCI_BASE_ADDRESS_1, pcibase);

    memory_region_set_size(&s->pcihost->dma_alias[1], size);
    memory_region_set_alias_offset(&s->pcihost->dma_alias[1], base);
    memory_region_set_address(&s->pcihost->dma_alias[1], pcibase);
}

static void bonito_writel(void *opaque, hwaddr addr,
                          uint64_t val, unsigned size)
{
    PCIBonitoState *s = opaque;
    uint32_t saddr;
    int reset = 0;

    saddr = addr >> 2;

    DPRINTF("bonito_writel "HWADDR_FMT_plx" val %lx saddr %x\n",
            addr, val, saddr);
    switch (saddr) {
    case BONITO_BONPONCFG:
    case BONITO_IODEVCFG:
    case BONITO_SDCFG:
    case BONITO_PCIMEMBASECFG:
    case BONITO_PCIMAP_CFG:
    case BONITO_GPIODATA:
    case BONITO_GPIOIE:
    case BONITO_INTEDGE:
    case BONITO_INTSTEER:
    case BONITO_INTPOL:
    case BONITO_PCIMAIL0:
    case BONITO_PCIMAIL1:
    case BONITO_PCIMAIL2:
    case BONITO_PCIMAIL3:
    case BONITO_PCICACHECTRL:
    case BONITO_PCICACHETAG:
    case BONITO_PCIBADADDR:
    case BONITO_PCIMSTAT:
    case BONITO_TIMECFG:
    case BONITO_CPUCFG:
    case BONITO_DQCFG:
    case BONITO_MEMSIZE:
        s->regs[saddr] = val;
        break;
    case BONITO_PCIMAP:
        s->regs[BONITO_PCIMAP] = val;
        bonito_update_pcimap(s);
        break;
    case BONITO_BONGENCFG:
        if (!(s->regs[saddr] & 0x04) && (val & 0x04)) {
            reset = 1; /* bit 2 jump from 0 to 1 cause reset */
        }
        s->regs[saddr] = val;
        if (reset) {
            qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
        }
        break;
    case BONITO_INTENSET:
        s->regs[BONITO_INTEN] |= val;
        bonito_update_irq(s);
        break;
    case BONITO_INTENCLR:
        s->regs[BONITO_INTEN] &= ~val;
        bonito_update_irq(s);
        break;
    case BONITO_INTEN:
    case BONITO_INTISR:
        DPRINTF("write to readonly bonito register %x\n", saddr);
        break;
    default:
        DPRINTF("write to unknown bonito register %x\n", saddr);
        break;
    }
}

static uint64_t bonito_readl(void *opaque, hwaddr addr,
                             unsigned size)
{
    PCIBonitoState *s = opaque;
    uint32_t saddr;

    saddr = addr >> 2;

    DPRINTF("bonito_readl "HWADDR_FMT_plx"\n", addr);
    switch (saddr) {
    case BONITO_INTISR:
        return s->regs[saddr];
    default:
        return s->regs[saddr];
    }
}

static const MemoryRegionOps bonito_ops = {
    .read = bonito_readl,
    .write = bonito_writel,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void bonito_pciconf_writel(void *opaque, hwaddr addr,
                                  uint64_t val, unsigned size)
{
    PCIBonitoState *s = opaque;
    PCIDevice *d = PCI_DEVICE(s);

    DPRINTF("bonito_pciconf_writel "HWADDR_FMT_plx" val %lx\n", addr, val);
    d->config_write(d, addr, val, 4);
}

static uint64_t bonito_pciconf_readl(void *opaque, hwaddr addr,
                                     unsigned size)
{

    PCIBonitoState *s = opaque;
    PCIDevice *d = PCI_DEVICE(s);

    DPRINTF("bonito_pciconf_readl "HWADDR_FMT_plx"\n", addr);
    return d->config_read(d, addr, 4);
}

/* north bridge PCI configure space. 0x1fe0 0000 - 0x1fe0 00ff */

static const MemoryRegionOps bonito_pciconf_ops = {
    .read = bonito_pciconf_readl,
    .write = bonito_pciconf_writel,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static uint64_t bonito_ldma_readl(void *opaque, hwaddr addr,
                                  unsigned size)
{
    uint32_t val;
    PCIBonitoState *s = opaque;

    if (addr >= sizeof(s->bonldma)) {
        return 0;
    }

    val = ((uint32_t *)(&s->bonldma))[addr / sizeof(uint32_t)];

    return val;
}

static void bonito_ldma_writel(void *opaque, hwaddr addr,
                               uint64_t val, unsigned size)
{
    PCIBonitoState *s = opaque;

    if (addr >= sizeof(s->bonldma)) {
        return;
    }

    ((uint32_t *)(&s->bonldma))[addr / sizeof(uint32_t)] = val & 0xffffffff;
}

static const MemoryRegionOps bonito_ldma_ops = {
    .read = bonito_ldma_readl,
    .write = bonito_ldma_writel,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static uint64_t bonito_cop_readl(void *opaque, hwaddr addr,
                                 unsigned size)
{
    uint32_t val;
    PCIBonitoState *s = opaque;

    if (addr >= sizeof(s->boncop)) {
        return 0;
    }

    val = ((uint32_t *)(&s->boncop))[addr / sizeof(uint32_t)];

    return val;
}

static void bonito_cop_writel(void *opaque, hwaddr addr,
                              uint64_t val, unsigned size)
{
    PCIBonitoState *s = opaque;

    if (addr >= sizeof(s->boncop)) {
        return;
    }

    ((uint32_t *)(&s->boncop))[addr / sizeof(uint32_t)] = val & 0xffffffff;
}

static const MemoryRegionOps bonito_cop_ops = {
    .read = bonito_cop_readl,
    .write = bonito_cop_writel,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static PCIDevice *bonito_pcihost_cfg_decode(PCIBonitoState *s, hwaddr addr)
{
    PCIHostState *phb = PCI_HOST_BRIDGE(s->pcihost);
    uint32_t pcimap_cfg = s->regs[BONITO_PCIMAP_CFG];
    uint32_t cycle, dev, func, bus;

    cycle = addr | FIELD_EX32(pcimap_cfg, PCIMAP_CFG, AD16UP) << 16;

    if (FIELD_EX32(pcimap_cfg, PCIMAP_CFG, TYPE1)) {
        dev = FIELD_EX32(cycle, TYPE1_CYCLE, DEV);
        func = FIELD_EX32(cycle, TYPE1_CYCLE, FUNC);
        bus = FIELD_EX32(cycle, TYPE1_CYCLE, BUS);
    } else {
        uint32_t idsel = FIELD_EX32(cycle, TYPE0_CYCLE, IDSEL);
        if (idsel == 0) {
            return NULL;
        }
        dev = ctz32(idsel);
        func = FIELD_EX32(cycle, TYPE0_CYCLE, FUNC);
        bus = 0;
    }

    return pci_find_device(phb->bus, bus, PCI_DEVFN(dev, func));
}

static void bonito_pcihost_signal_mabort(PCIBonitoState *s)
{
    PCIDevice *d = &s->dev;
    uint16_t status = pci_get_word(d->config + PCI_STATUS);

    status |= PCI_STATUS_REC_MASTER_ABORT;
    pci_set_word(d->config + PCI_STATUS, status);

    /* Generate a pulse, it's a edge triggered IRQ */
    bonito_set_irq(s->pcihost, ICU_PIN_MASTERERR, 1);
    bonito_set_irq(s->pcihost, ICU_PIN_MASTERERR, 0);
}

static MemTxResult bonito_pcihost_cfg_read(void *opaque, hwaddr addr,
                                           uint64_t *data, unsigned len,
                                           MemTxAttrs attrs)
{
    PCIBonitoState *s = opaque;
    PCIDevice *dev;

    dev = bonito_pcihost_cfg_decode(s, addr);
    if (!dev) {
        bonito_pcihost_signal_mabort(s);
        /*
         * Vanilla bonito will actually triiger a bus error on master abort,
         * Godson variant won't. We need to return all 1s.
         */
        *data = UINT64_MAX;
        return MEMTX_OK;
    }

    addr &= PCI_CONFIG_SPACE_SIZE - 1;
    *data = pci_host_config_read_common(dev, addr, pci_config_size(dev), len);

    return MEMTX_OK;
}

static MemTxResult bonito_pcihost_cfg_write(void *opaque, hwaddr addr,
                                            uint64_t data, unsigned len,
                                            MemTxAttrs attrs)
{
    PCIBonitoState *s = opaque;
    PCIDevice *dev;

    dev = bonito_pcihost_cfg_decode(s, addr);
    if (!dev) {
        bonito_pcihost_signal_mabort(s);
        return MEMTX_OK;
    }

    addr &= PCI_CONFIG_SPACE_SIZE - 1;
    pci_host_config_write_common(dev, addr, pci_config_size(dev), data, len);

    return MEMTX_OK;
}

/* PCI Configure Space access region. 0x1fe8 0000 - 0x1fef ffff */
static const MemoryRegionOps bonito_pcihost_cfg_ops = {
    .read_with_attrs = bonito_pcihost_cfg_read,
    .write_with_attrs = bonito_pcihost_cfg_write,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
    .impl.min_access_size = 1,
    .impl.max_access_size = 4,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void bonito_pci_write_config(PCIDevice *dev, uint32_t address,
                                    uint32_t val, int len)
{
    pci_default_write_config(dev, address, val, len);

    if (ranges_overlap(address, len, PCI_BASE_ADDRESS_0, 12)) {
        /* Bonito Host Bridge BARs are defined as DMA windows (pciBase) */
        bonito_update_pcibase(PCI_BONITO(dev));
    }
}

static int pci_bonito_map_irq(PCIDevice *pci_dev, int irq_num)
{
    /* Fuloong 2E PCI INTX are connected to Bonito GPIN[3:0] */
    return ICU_PIN_GPINx(irq_num);
}

static AddressSpace *bonito_pcihost_set_iommu(PCIBus *bus, void *opaque,
                                              int devfn)
{
    BonitoState *bs = opaque;

    return &bs->dma_as;
}

static const PCIIOMMUOps bonito_iommu_ops = {
    .get_address_space = bonito_pcihost_set_iommu,
};

static void bonito_reset_hold(Object *obj, ResetType type)
{
    PCIBonitoState *s = PCI_BONITO(obj);
    uint32_t val = 0;

    /* set the default value of north bridge registers */

    s->regs[BONITO_BONPONCFG] = 0xc40;
    val = FIELD_DP32(val, BONGENCFG, PCIQUEUE, 1);
    val = FIELD_DP32(val, BONGENCFG, WRITEBEHIND, 1);
    val = FIELD_DP32(val, BONGENCFG, PREFETCH, 1);
    val = FIELD_DP32(val, BONGENCFG, UNCACHED, 1);
    val = FIELD_DP32(val, BONGENCFG, CPUSELFRESET, 1);
    s->regs[BONITO_BONGENCFG] = val;

    s->regs[BONITO_IODEVCFG] = 0x2bff8010;
    s->regs[BONITO_SDCFG] = 0x255e0091;

    s->regs[BONITO_GPIODATA] = 0x1ff;
    s->regs[BONITO_GPIOIE] = 0x1ff;
    s->regs[BONITO_DQCFG] = 0x8;
    s->regs[BONITO_MEMSIZE] = 0x10000000;
    s->regs[BONITO_PCIMAP] = 0x6140;
    bonito_update_pcimap(s);

    pci_set_long(s->dev.config + PCI_BASE_ADDRESS_0, 0x80000000);
    pci_set_long(s->dev.config + PCI_BASE_ADDRESS_1, 0x0);
    bonito_update_pcibase(s);
}

static const VMStateDescription vmstate_bonito = {
    .name = "Bonito",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_PCI_DEVICE(dev, PCIBonitoState),
        VMSTATE_END_OF_LIST()
    }
};

static void bonito_host_realize(DeviceState *dev, Error **errp)
{
    PCIHostState *phb = PCI_HOST_BRIDGE(dev);
    BonitoState *bs = BONITO_PCI_HOST_BRIDGE(dev);
    MemoryRegion *pcimem_lo_alias = g_new(MemoryRegion, 3);

    memory_region_init(&bs->pci_mem, OBJECT(dev), "pci.mem", BONITO_PCIHI_SIZE);
    phb->bus = pci_register_root_bus(dev, "pci",
                                     bonito_set_irq, pci_bonito_map_irq,
                                     dev, &bs->pci_mem, get_system_io(),
                                     PCI_DEVFN(5, 0), 32, TYPE_PCI_BUS);

    for (size_t i = 0; i < 3; i++) {
        char *name = g_strdup_printf("pci.lomem%zu", i);

        memory_region_init_alias(&pcimem_lo_alias[i], NULL, name,
                                 &bs->pci_mem, i * 64 * MiB, 64 * MiB);
        memory_region_add_subregion(get_system_memory(),
                                    BONITO_PCILO_BASE + i * 64 * MiB,
                                    &pcimem_lo_alias[i]);
        g_free(name);
    }

    bs->pcimem_lo_alias = pcimem_lo_alias;

    create_unimplemented_device("pci.io", BONITO_PCIIO_BASE, 1 * MiB);
}

static void bonito_pci_realize(PCIDevice *dev, Error **errp)
{
    PCIBonitoState *s = PCI_BONITO(dev);
    MemoryRegion *host_mem = get_system_memory();
    PCIHostState *phb = PCI_HOST_BRIDGE(s->pcihost);
    BonitoState *bs = s->pcihost;
    MemoryRegion *pcimem_hi_alias = g_new(MemoryRegion, 1);
    MemoryRegion *dma_alias = g_new(MemoryRegion, 2);

    /*
     * Bonito North Bridge, built on FPGA,
     * VENDOR_ID/DEVICE_ID are "undefined"
     */
    pci_config_set_prog_interface(dev->config, 0x00);

    /* set the north bridge register mapping */
    memory_region_init_io(&s->iomem, OBJECT(s), &bonito_ops, s,
                          "north-bridge-register", BONITO_INTERNAL_REG_SIZE);
    memory_region_add_subregion(host_mem, BONITO_INTERNAL_REG_BASE, &s->iomem);

    /* set the north bridge pci configure  mapping */
    memory_region_init_io(&phb->conf_mem, OBJECT(s), &bonito_pciconf_ops, s,
                          "north-bridge-pci-config", BONITO_PCICONFIG_SIZE);
    memory_region_add_subregion(host_mem, BONITO_PCICONFIG_BASE,
                                &phb->conf_mem);

    /* set the pci config space accessor mapping */
    memory_region_init_io(&phb->data_mem, OBJECT(s), &bonito_pcihost_cfg_ops, s,
                          "pci-host-config-access", BONITO_PCICFG_SIZE);
    memory_region_add_subregion(host_mem, BONITO_PCICFG_BASE,
                                &phb->data_mem);

    create_unimplemented_device("bonito", BONITO_REG_BASE, BONITO_REG_SIZE);

    memory_region_init_io(&s->iomem_ldma, OBJECT(s), &bonito_ldma_ops, s,
                          "ldma", 0x100);
    memory_region_add_subregion(host_mem, 0x1fe00200, &s->iomem_ldma);

    /* PCI copier */
    memory_region_init_io(&s->iomem_cop, OBJECT(s), &bonito_cop_ops, s,
                          "cop", 0x100);
    memory_region_add_subregion(host_mem, 0x1fe00300, &s->iomem_cop);

    create_unimplemented_device("ROMCS", BONITO_FLASH_BASE, 60 * MiB);

    /* Map PCI IO Space  0x1fd0 0000 - 0x1fd1 0000 */
    memory_region_init_alias(&s->bonito_pciio, OBJECT(s), "isa_mmio",
                             get_system_io(), 0, BONITO_PCIIO_SIZE);
    memory_region_add_subregion(host_mem, BONITO_PCIIO_BASE,
                                &s->bonito_pciio);

    /* add pci local io mapping */

    memory_region_init_alias(&s->bonito_localio, OBJECT(s), "IOCS[0]",
                             get_system_io(), 0, 256 * KiB);
    memory_region_add_subregion(host_mem, BONITO_DEV_BASE,
                                &s->bonito_localio);
    create_unimplemented_device("IOCS[1]", BONITO_DEV_BASE + 1 * 256 * KiB,
                                256 * KiB);
    create_unimplemented_device("IOCS[2]", BONITO_DEV_BASE + 2 * 256 * KiB,
                                256 * KiB);
    create_unimplemented_device("IOCS[3]", BONITO_DEV_BASE + 3 * 256 * KiB,
                                256 * KiB);

    memory_region_init_alias(pcimem_hi_alias, NULL, "pci.memhi.alias",
                             &bs->pci_mem, 0, BONITO_PCIHI_SIZE);
    memory_region_add_subregion(host_mem, BONITO_PCIHI_BASE, pcimem_hi_alias);
    bs->pcimem_hi_alias = pcimem_hi_alias;
    create_unimplemented_device("PCI_2",
                                (hwaddr)BONITO_PCIHI_BASE + BONITO_PCIHI_SIZE,
                                2 * GiB);

    /* 32bit DMA */
    memory_region_init(&bs->dma_mr, OBJECT(s), "dma.pciBase", 4 * GiB);

    /* pciBase0, mapped to system RAM */
    memory_region_init_alias(&dma_alias[0], NULL, "pciBase0.mem.alias",
                             host_mem, 0x80000000, 256 * MiB);
    memory_region_add_subregion_overlap(&bs->dma_mr, 0, &dma_alias[0], 2);

    /* pciBase1, mapped to system RAM */
    memory_region_init_alias(&dma_alias[1], NULL, "pciBase1.mem.alias",
                            host_mem, 0, 256 * MiB);
    memory_region_add_subregion_overlap(&bs->dma_mr, 0, &dma_alias[1], 1);

    bs->dma_alias = dma_alias;

    address_space_init(&bs->dma_as, &bs->dma_mr, "pciBase.dma");
    pci_setup_iommu(phb->bus, &bonito_iommu_ops, bs);

    /* set the default value of north bridge pci config */
    pci_set_word(dev->config + PCI_COMMAND, 0x0000);
    pci_set_word(dev->config + PCI_STATUS, 0x0000);
    pci_set_word(dev->config + PCI_SUBSYSTEM_VENDOR_ID, 0x0000);
    pci_set_word(dev->config + PCI_SUBSYSTEM_ID, 0x0000);

    pci_set_byte(dev->config + PCI_INTERRUPT_LINE, 0x00);
    pci_config_set_interrupt_pin(dev->config, 0x01); /* interrupt pin A */

    pci_set_byte(dev->config + PCI_MIN_GNT, 0x3c);
    pci_set_byte(dev->config + PCI_MAX_LAT, 0x00);
}

PCIBus *bonito_init(qemu_irq *pic)
{
    DeviceState *dev;
    BonitoState *pcihost;
    PCIHostState *phb;
    PCIBonitoState *s;
    PCIDevice *d;

    dev = qdev_new(TYPE_BONITO_PCI_HOST_BRIDGE);
    phb = PCI_HOST_BRIDGE(dev);
    pcihost = BONITO_PCI_HOST_BRIDGE(dev);
    pcihost->pic = pic;
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);

    d = pci_new(PCI_DEVFN(0, 0), TYPE_PCI_BONITO);
    s = PCI_BONITO(d);
    s->pcihost = pcihost;
    pcihost->pci_dev = s;
    pci_realize_and_unref(d, phb->bus, &error_fatal);

    return phb->bus;
}

static void bonito_pci_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    k->config_write = bonito_pci_write_config;
    rc->phases.hold = bonito_reset_hold;
    k->realize = bonito_pci_realize;
    k->vendor_id = 0xdf53;
    k->device_id = 0x00d5;
    k->revision = 0x01;
    k->class_id = PCI_CLASS_BRIDGE_HOST;
    dc->desc = "Host bridge";
    dc->vmsd = &vmstate_bonito;
    /*
     * PCI-facing part of the host bridge, not usable without the
     * host-facing part, which can't be device_add'ed, yet.
     */
    dc->user_creatable = false;
}

static const TypeInfo bonito_pci_info = {
    .name          = TYPE_PCI_BONITO,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCIBonitoState),
    .class_init    = bonito_pci_class_init,
    .interfaces = (const InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void bonito_host_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = bonito_host_realize;
}

static const TypeInfo bonito_host_info = {
    .name          = TYPE_BONITO_PCI_HOST_BRIDGE,
    .parent        = TYPE_PCI_HOST_BRIDGE,
    .instance_size = sizeof(BonitoState),
    .class_init    = bonito_host_class_init,
};

static void bonito_register_types(void)
{
    type_register_static(&bonito_host_info);
    type_register_static(&bonito_pci_info);
}

type_init(bonito_register_types)
