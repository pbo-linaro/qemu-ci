/*
 * S/390 DIAGNOSE 508 definitions and structures
 *
 * Copyright 2025 IBM Corp.
 * Author(s): Collin Walling <walling@linux.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#ifndef S390X_DIAG508_H
#define S390X_DIAG508_H

#define DIAG_508_SUBC_QUERY_SUBC    0x0000
#define DIAG_508_SUBC_SIG_VERIF     0x4000

#define DIAG_508_RC_OK              0x0001
#define DIAG_508_RC_NO_CERTS        0x0102
#define DIAG_508_RC_CERT_NOT_FOUND  0x0202
#define DIAG_508_RC_NO_MEM_FOR_CERT 0x0302
#define DIAG_508_RC_INVAL_COMP_DATA 0x0402
#define DIAG_508_RC_INVAL_X509_CERT 0x0502
#define DIAG_508_RC_INVAL_PKCS7_SIG 0x0602
#define DIAG_508_RC_FAIL_VERIF      0x0702

struct Diag508CertificateStoreInfo {
    uint8_t  idx;
    uint64_t len;
} QEMU_PACKED;
typedef struct Diag508CertificateStoreInfo Diag508CertificateStoreInfo;

struct Diag508SignatureVerificationBlock {
    Diag508CertificateStoreInfo csi;
    uint64_t comp_len;
    uint64_t comp_addr;
    uint64_t sig_len;
    uint64_t sig_addr;
} QEMU_PACKED;
typedef struct Diag508SignatureVerificationBlock Diag508SignatureVerificationBlock;

#endif
