/*
 * s390x PCI MMIO definitions
 *
 * Copyright 2025 IBM Corp.
 * Author(s): Farhan Ali <alifm@linux.ibm.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef S390X_PCI_MMIO_H
#define S390X_PCI_MMIO_H

uint64_t s390x_pci_mmio_read_64(const void *ioaddr);
uint32_t s390x_pci_mmio_read_32(const void *ioaddr);
void s390x_pci_mmio_write_64(void *ioaddr, uint64_t val);
void s390x_pci_mmio_write_32(void *ioaddr, uint32_t val);

#endif
