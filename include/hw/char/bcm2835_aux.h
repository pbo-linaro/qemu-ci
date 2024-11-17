/*
 * Rasperry Pi 2 emulation and refactoring Copyright (c) 2015, Microsoft
 * Written by Andrew Baumann
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef BCM2835_AUX_H
#define BCM2835_AUX_H

#include "chardev/char-fe.h"
#include "hw/sysbus.h"
#include "qemu/fifo8.h"
#include "qom/object.h"

#define TYPE_BCM2835_AUX "bcm2835-aux"
OBJECT_DECLARE_SIMPLE_TYPE(BCM2835AuxState, BCM2835_AUX)

struct BCM2835AuxState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    MemoryRegion iomem;
    CharBackend chr;
    qemu_irq irq;

    Fifo8 rx_fifo;
    Fifo8 tx_fifo;
    /* Registers */
    uint32_t ier, iir, lsr, cntl, stat;
};

#endif
