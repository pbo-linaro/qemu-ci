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
#define DIAG_320_SUBC_STORE_VC      2

#define DIAG_320_RC_OK              0x0001
#define DIAG_320_RC_NOMEM           0x0202
#define DIAG_320_RC_INVAL_VCB_LEN   0x0204
#define DIAG_320_RC_BAD_RANGE       0x0302

#define VCSSB_MAX_LEN   128
#define VCE_HEADER_LEN  128
#define VCB_HEADER_LEN  64

#define DIAG_320_ISM_QUERY_VCSI     0x4000000000000000
#define DIAG_320_ISM_STORE_VC       0x2000000000000000

#define DIAG_320_VCE_FLAGS_VALID                0x80
#define DIAG_320_VCE_KEYTYPE_SELF_DESCRIBING    0
#define DIAG_320_VCE_FORMAT_X509_DER            1
#define DIAG_320_VCE_HASHTYPE_SHA2_256          1

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

struct vcb_header {
    uint32_t vcbinlen;
    uint32_t reserved0;
    uint16_t fvci;
    uint16_t lvci;
    uint32_t reserved1;
    uint32_t cstoken;
    uint32_t reserved2[3];
    uint32_t vcboutlen;
    uint8_t reserved3[3];
    uint8_t version;
    uint16_t svcc;
    uint16_t rvcc;
    uint32_t reserved4[5];
} QEMU_PACKED;
typedef struct vcb_header vcb_header;

struct VerficationCertificateBlock {
    vcb_header vcb_hdr;
    uint8_t vcb_buf[];
} QEMU_PACKED;
typedef struct VerficationCertificateBlock VerficationCertificateBlock;

struct vce_header {
    uint32_t len;
    uint8_t flags;
    uint8_t keytype;
    uint16_t certidx;
    uint32_t name[16];
    uint8_t format;
    uint8_t reserved0;
    uint16_t keyidlen;
    uint8_t reserved1;
    uint8_t hashtype;
    uint16_t hashlen;
    uint32_t reserved2;
    uint32_t certlen;
    uint32_t reserved3[2];
    uint16_t hashoffset;
    uint16_t certoffset;
    uint32_t reserved4[7];
} QEMU_PACKED;
typedef struct vce_header vce_header;

struct VerificationCertificateEntry {
    vce_header vce_hdr;
    uint8_t cert_data_buf[];
} QEMU_PACKED;
typedef struct VerificationCertificateEntry VerificationCertificateEntry;

#endif
