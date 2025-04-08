/*
 * S/390 DIAGNOSE 320 definitions and structures
 *
 * Copyright 2025 IBM Corp.
 * Author(s): Zhuoying Cai <zycai@linux.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#ifndef S390X_DIAG320_H
#define S390X_DIAG320_H

#define DIAG_320_SUBC_QUERY_ISM     0
#define DIAG_320_SUBC_QUERY_VCSI    1

#define DIAG_320_RC_OK              0x0001
#define DIAG_320_RC_NOMEM           0x0202

#define VCSSB_MAX_LEN   128
#define VCE_HEADER_LEN  128
#define VCB_HEADER_LEN  64

#define DIAG_320_ISM_QUERY_VCSI     0x4000000000000000

struct VerificationCertificateStorageSizeBlock {
    uint32_t length;
    uint8_t reserved0[3];
    uint8_t version;
    uint32_t reserved1[6];
    uint16_t totalvc;
    uint16_t maxvc;
    uint32_t reserved3[7];
    uint32_t maxvcelen;
    uint32_t reserved4[3];
    uint32_t largestvcblen;
    uint32_t totalvcblen;
    uint32_t reserved5[10];
} QEMU_PACKED;
typedef struct VerificationCertificateStorageSizeBlock \
VerificationCertificateStorageSizeBlock;

#endif
