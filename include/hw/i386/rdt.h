/*
 * Intel Resource Director Technology (RDT).
 *
 * Copyright 2024 Google LLC
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
 */

#ifndef HW_RDT_H
#define HW_RDT_H

#include <stdbool.h>
#include <stdint.h>

typedef struct RDTState RDTState;
typedef struct RDTStatePerCore RDTStatePerCore;
typedef struct RDTMonitor RDTMonitor;
typedef struct RDTAllocation RDTAllocation;

uint32_t rdt_get_cpuid_10_1_edx_cos_max(void);
uint32_t rdt_get_cpuid_10_2_edx_cos_max(void);
uint32_t rdt_get_cpuid_10_3_edx_cos_max(void);

bool rdt_associate_rmid_cos(uint64_t msr_ia32_pqr_assoc);

void rdt_write_msr_l3_mask(uint32_t pos, uint32_t val);
void rdt_write_msr_l2_mask(uint32_t pos, uint32_t val);
void rdt_write_mba_thrtl(uint32_t pos, uint32_t val);

uint32_t rdt_read_l3_mask(uint32_t pos);
uint32_t rdt_read_l2_mask(uint32_t pos);
uint32_t rdt_read_mba_thrtl(uint32_t pos);

uint64_t rdt_read_event_count(RDTStatePerCore *rdt, uint32_t rmid, uint32_t event_id);
uint32_t rdt_max_rmid(RDTStatePerCore *rdt);

#endif
