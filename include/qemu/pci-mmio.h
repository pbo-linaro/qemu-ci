/*
 * QEMU PCI MMIO API
 *
 * Copyright 2025 IBM Corp.
 * Author(s): Farhan Ali <alifm@linux.ibm.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef QEMU_PCI_MMIO_H
#define QEMU_PCI_MMIO_H

#ifdef __s390x__
#include "s390x_pci_mmio.h"
#endif

static inline uint8_t qemu_pci_mmio_read_8(const void *ioaddr)
{
    uint8_t ret = 0;
#ifdef __s390x__
    ret = s390x_pci_mmio_read_8(ioaddr);
#else
    /* Prevent the compiler from optimizing away the load */
    ret = *((volatile uint8_t *)ioaddr);
#endif

    return ret;
}

static inline uint16_t qemu_pci_mmio_read_16(const void *ioaddr)
{
    uint16_t ret = 0;
#ifdef __s390x__
    ret = s390x_pci_mmio_read_16(ioaddr);
#else
    /* Prevent the compiler from optimizing away the load */
    ret = *((volatile uint16_t *)ioaddr);
#endif

    return le16_to_cpu(ret);
}

static inline uint32_t qemu_pci_mmio_read_32(const void *ioaddr)
{
    uint32_t ret = 0;
#ifdef __s390x__
    ret = s390x_pci_mmio_read_32(ioaddr);
#else
    /* Prevent the compiler from optimizing away the load */
    ret = *((volatile uint32_t *)ioaddr);
#endif

    return le32_to_cpu(ret);
}

static inline uint64_t qemu_pci_mmio_read_64(const void *ioaddr)
{
    uint64_t ret = 0;
#ifdef __s390x__
    ret = s390x_pci_mmio_read_64(ioaddr);
#else
    /* Prevent the compiler from optimizing away the load */
    ret = *((volatile uint64_t *)ioaddr);
#endif

    return le64_to_cpu(ret);
}

static inline void qemu_pci_mmio_write_8(void *ioaddr, uint8_t val)
{

#ifdef __s390x__
    s390x_pci_mmio_write_8(ioaddr, val);
#else
    /* Prevent the compiler from optimizing away the store */
    *((volatile uint8_t *)ioaddr) = val;
#endif
}

static inline void qemu_pci_mmio_write_16(void *ioaddr, uint16_t val)
{
    val = cpu_to_le16(val);

#ifdef __s390x__
    s390x_pci_mmio_write_16(ioaddr, val);
#else
    /* Prevent the compiler from optimizing away the store */
    *((volatile uint16_t *)ioaddr) = val;
#endif
}

static inline void qemu_pci_mmio_write_32(void *ioaddr, uint32_t val)
{
    val = cpu_to_le32(val);

#ifdef __s390x__
    s390x_pci_mmio_write_32(ioaddr, val);
#else
    /* Prevent the compiler from optimizing away the store */
    *((volatile uint32_t *)ioaddr) = val;
#endif
}

static inline void qemu_pci_mmio_write_64(void *ioaddr, uint64_t val)
{
    val = cpu_to_le64(val);

#ifdef __s390x__
    s390x_pci_mmio_write_64(ioaddr, val);
#else
    /* Prevent the compiler from optimizing away the store */
    *((volatile uint64_t *)ioaddr) = val;
#endif
}

#endif
