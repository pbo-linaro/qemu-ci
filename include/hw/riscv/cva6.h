/*
 * QEMU RISC-V Board for OpenHW CVA6 SoC
 * https://github.com/openhwgroup/cva6/tree/master/corev_apu
 *
 * Copyright (c) 2025 Codethink Ltd
 * Ben Dooks <ben.dooks@codethink.co.uk>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_CVA6_H
#define HW_CVA6_H

#include "hw/riscv/riscv_hart.h"
#include "hw/intc/sifive_plic.h"
#include "hw/char/serial-mm.h"

#include "hw/boards.h"
#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_RISCV_CVA6 "riscv.cva6.soc"
OBJECT_DECLARE_SIMPLE_TYPE(CVA6SoCState, RISCV_CVA6)

typedef struct CVA6SoCState {
    /*< private >*/
    DeviceState parent_obj;

    /*< public >*/
    RISCVHartArrayState cpus;
    DeviceState *plic;
    MemoryRegion rom;

    uint32_t resetvec;
} CVA6SoCState;

#define TYPE_CVA6_MACHINE MACHINE_TYPE_NAME("cva6")
OBJECT_DECLARE_SIMPLE_TYPE(CVA6State, CVA6_MACHINE)

typedef struct CVA6State {
    /*< private >*/
    MachineState parent_obj;

    /*< public >*/
    CVA6SoCState soc;
}
CVA6State;

enum {
    CVA6_DEBUG,
    CVA6_ROM,
    CVA6_CLINT,
    CVA6_PLIC,
    CVA6_UART,
    CVA6_TIMER,
    CVA6_SPI,
    CVA6_ETHERNET,
    CVA6_GPIO,
    CVA6_DRAM,
};

enum {
    CVA6_UART_IRQ       = 1,
    CVA6_SPI_IRQ        = 2,
    CVA6_ETH_IRQ        = 3,
    CVA6_TIMER0_OVF_IRQ = 4,
    CVA6_TIMER0_CMP_IRQ = 5,
    CVA6_TIMER1_OVF_IRQ = 6,
    CVA6_TIMER1_CMP_IRQ = 7,
};

#define CLINT_TIMEBASE_FREQ 25000000

/*
 * plic register interface in corev_apu/rv_plic/rtl/plic_regmap.sv
 * https://github.com/pulp-platform/rv_plic/blob/master/rtl/plic_regmap.sv
*/

#define CVA6_PLIC_NUM_SOURCES           32
#define CVA6_PLIC_NUM_PRIORITIES        7
#define CVA6_PLIC_PRIORITY_BASE         0x0000
#define CVA6_PLIC_PENDING_BASE          0x1000
#define CVA6_PLIC_ENABLE_BASE           0x2000
#define CVA6_PLIC_ENABLE_STRIDE         0x80
#define CVA6_PLIC_CONTEXT_BASE          0x200000
#define CVA6_PLIC_CONTEXT_STRIDE        0x1000

#endif /* HW_CVA6_H */
