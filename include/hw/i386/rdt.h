/*
 * Intel Resource Director Technology (RDT).
 *
 * Copyright 2025 Google LLC
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

/* Max counts for allocation masks or CBMs. In other words, the size of
 * respective MSRs.
 * L3_MASK and L3_mask are architectural limitations. THRTL_COUNT is just
 * the space left until the next MSR.
 * */
#define RDT_MAX_L3_MASK_COUNT      127
#define RDT_MAX_L2_MASK_COUNT      63
#define RDT_MAX_MBA_THRTL_COUNT    63

/* RDT L3 Cache Monitoring Technology */
#define CPUID_F_0_EDX_L3               (1U << 1)
#define CPUID_F_1_EDX_L3_OCCUPANCY     (1U << 0)
#define CPUID_F_1_EDX_L3_TOTAL_BW      (1U << 1)
#define CPUID_F_1_EDX_L3_LOCAL_BW      (1U << 2)

/* RDT Cache Allocation Technology */
#define CPUID_10_0_EBX_L3_CAT           (1U << 1)
#define CPUID_10_0_EBX_L2_CAT           (1U << 2)
#define CPUID_10_0_EBX_MBA              (1U << 3)

/* RDT L3 Allocation features */
#define CPUID_10_1_EAX_CBM_LENGTH       0xf
#define CPUID_10_1_EBX_CBM              0x0
#define CPUID_10_1_ECX_CDP              0x0 /* to enable, it would be (1U << 2) */
#define CPUID_10_1_EDX_COS_MAX          RDT_MAX_L3_MASK_COUNT

/* RDT L2 Allocation features*/
#define CPUID_10_2_EAX_CBM_LENGTH       0xf
#define CPUID_10_2_EBX_CBM              0x0
#define CPUID_10_2_EDX_COS_MAX          RDT_MAX_L2_MASK_COUNT

/* RDT MBA features */
#define CPUID_10_3_EAX_THRTL_MAX        89
#define CPUID_10_3_ECX_LINEAR_RESPONSE (1U << 2)
#define CPUID_10_3_EDX_COS_MAX          RDT_MAX_MBA_THRTL_COUNT

typedef struct RDTState RDTState;
typedef struct RDTStatePerL3Cache RDTStatePerL3Cache;
typedef struct RDTStatePerCore RDTStatePerCore;
typedef struct RDTMonitor RDTMonitor;
typedef struct RDTAllocation RDTAllocation;

bool rdt_associate_rmid_cos(uint64_t msr_ia32_pqr_assoc);

void rdt_write_msr_l3_mask(uint32_t pos, uint32_t val);
void rdt_write_msr_l2_mask(uint32_t pos, uint32_t val);
void rdt_write_mba_thrtl(uint32_t pos, uint32_t val);

uint32_t rdt_read_l3_mask(uint32_t pos);
uint32_t rdt_read_l2_mask(uint32_t pos);
uint32_t rdt_read_mba_thrtl(uint32_t pos);

uint64_t rdt_read_event_count(RDTStatePerL3Cache *rdt, uint32_t rmid, uint32_t event_id);
uint32_t rdt_max_rmid(RDTStatePerL3Cache *rdt);

#endif
