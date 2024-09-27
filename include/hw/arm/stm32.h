/*
 * STM32 chip configuration parameters.
 * These enums are used to configure STM32 chips, as well as their peripherals.
 *
 * Copyright 2024 Román Cárdenas <rcardenas.rod@gmail.com>
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 */

#ifndef STM32_H
#define STM32_H

enum {
    /* High Performance */
    STM32_F2,
    STM32_F4,
    STM32_H5,
    STM32_F7,
    STM32_H7,
    /* Mainstream */
    STM32_C0,
    STM32_F0,
    STM32_G0,
    STM32_F1,
    STM32_F3,
    STM32_G4,
    /* Ultra Low Power */
    STM32_L0,
    STM32_L4,
    STM32_L4P,
    STM32_L5,
    STM32_U5,
    /* Wireless */
    STM32_WL,
    STM32_WB0,
    STM32_WB,
    STM32_WBA,
};

#endif /* STM32_H */
