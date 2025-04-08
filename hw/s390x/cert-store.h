/*
 * S390 certificate store
 *
 * Copyright 2025 IBM Corp.
 * Author(s): Zhuoying Cai <zycai@linux.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#ifndef HW_S390_CERT_STORE_H
#define HW_S390_CERT_STORE_H

#include "hw/s390x/ipl/qipl.h"

#ifdef CONFIG_GNUTLS
#include <gnutls/x509.h>
#include <gnutls/gnutls.h>
#endif /* #define CONFIG_GNUTLS */

#define VC_NAME_LEN_BYTES  64

struct S390IPLCertificate {
    uint8_t vc_name[VC_NAME_LEN_BYTES];
    size_t size;
    size_t key_id_size;
    size_t hash_size;
    char *raw;
#ifdef CONFIG_GNUTLS
    gnutls_x509_crt_fmt_t format;
#endif /* #define CONFIG_GNUTLS */
};
typedef struct S390IPLCertificate S390IPLCertificate;

struct S390IPLCertificateStore {
    uint16_t count;
    size_t max_cert_size;
    size_t total_bytes;
    S390IPLCertificate certs[MAX_CERTIFICATES];
} QEMU_PACKED;
typedef struct S390IPLCertificateStore S390IPLCertificateStore;

#ifdef CONFIG_GNUTLS
int g_init_cert(uint8_t *raw_cert, size_t cert_size, gnutls_x509_crt_t *g_cert);
#endif /* #define CONFIG_GNUTLS */

void s390_ipl_create_cert_store(S390IPLCertificateStore *cert_store);

#endif
