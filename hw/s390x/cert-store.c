/*
 * S390 certificate store implementation
 *
 * Copyright 2025 IBM Corp.
 * Author(s): Zhuoying Cai <zycai@linux.ibm.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "cert-store.h"
#include "qemu/error-report.h"
#include "qemu/option.h"
#include "qemu/config-file.h"
#include "hw/s390x/ebcdic.h"
#include "qemu/cutils.h"
#include "cert-store.h"

#ifdef CONFIG_GNUTLS
#include <gnutls/x509.h>
#include <gnutls/gnutls.h>
#endif /* #define CONFIG_GNUTLS */

static const char *s390_get_boot_certificates(void)
{
    QemuOpts *opts;
    const char *path;

    opts = qemu_find_opts_singleton("boot-certificates");
    path = qemu_opt_get(opts, "boot-certificates");

    return path;
}

static size_t cert2buf(char *path, size_t max_size, char **cert_buf)
{
    size_t size;
    g_autofree char *buf;
    buf = g_malloc(max_size);

    if (!g_file_get_contents(path, &buf, &size, NULL) ||
        size == 0 || size > max_size) {
        return 0;
    }

    *cert_buf = g_steal_pointer(&buf);

    return size;
}

#ifdef CONFIG_GNUTLS
int g_init_cert(uint8_t *raw_cert, size_t cert_size, gnutls_x509_crt_t *g_cert)
{
    int rc;

    if (gnutls_x509_crt_init(g_cert) < 0) {
        return -1;
    }

    gnutls_datum_t datum_cert = {raw_cert, cert_size};
    rc = gnutls_x509_crt_import(*g_cert, &datum_cert, GNUTLS_X509_FMT_DER);
    if (rc) {
        gnutls_x509_crt_deinit(*g_cert);
        return rc;
    }

    return 0;
}
#endif /* CONFIG_GNUTLS */

static int init_cert_x509_der(size_t size, char *raw, S390IPLCertificate **qcert)
{
#ifdef CONFIG_GNUTLS
    gnutls_x509_crt_t g_cert = NULL;
    g_autofree S390IPLCertificate *q_cert;
    size_t key_id_size;
    size_t hash_size;
    int rc;

    rc = g_init_cert((uint8_t *)raw, size, &g_cert);
    if (rc) {
        if (rc == GNUTLS_E_ASN1_TAG_ERROR) {
            error_report("The certificate is not in DER format");
        }
        return -1;
    }

    rc = gnutls_x509_crt_get_key_id(g_cert, GNUTLS_KEYID_USE_SHA256, NULL, &key_id_size);
    if (rc != GNUTLS_E_SHORT_MEMORY_BUFFER) {
        error_report("Failed to get certificate key ID size");
        goto out;
    }

    rc = gnutls_x509_crt_get_fingerprint(g_cert, GNUTLS_DIG_SHA256, NULL, &hash_size);
    if (rc != GNUTLS_E_SHORT_MEMORY_BUFFER) {
        error_report("Failed to get certificate hash size");
        goto out;
    }

    q_cert = g_malloc(sizeof(*q_cert));
    q_cert->size = size;
    q_cert->key_id_size = key_id_size;
    q_cert->hash_size = hash_size;
    q_cert->raw = raw;
    q_cert->format = GNUTLS_X509_FMT_DER;
    *qcert = g_steal_pointer(&q_cert);

    gnutls_x509_crt_deinit(g_cert);

    return 0;
out:
    gnutls_x509_crt_deinit(g_cert);
    return -1;
#else
    error_report("Cryptographic library is not enabled")
    return -1;
#endif /* #define CONFIG_GNUTLS */
}

static int check_path_type(const char *path)
{
    struct stat path_stat;

    stat(path, &path_stat);

    if (S_ISDIR(path_stat.st_mode)) {
        return S_IFDIR;
    } else if (S_ISREG(path_stat.st_mode)) {
        return S_IFREG;
    } else {
        return -1;
    }
}

static int init_cert(char *paths, S390IPLCertificate **qcert)
{
    char *buf;
    char vc_name[VC_NAME_LEN_BYTES];
    const gchar *filename;
    size_t size;

    filename = g_path_get_basename(paths);

    size = cert2buf(paths, CERT_MAX_SIZE, &buf);
    if (size == 0) {
        error_report("Failed to load certificate: %s", paths);
        return -1;
    }

    if (init_cert_x509_der(size, buf, qcert) < 0) {
        error_report("Failed to initialize certificate: %s", paths);
        return -1;
    }

    /*
     * Left justified certificate name with padding on the right with blanks.
     * Convert certificate name to EBCDIC.
     */
    strpadcpy(vc_name, VC_NAME_LEN_BYTES, filename, ' ');
    ebcdic_put((*qcert)->vc_name, vc_name, VC_NAME_LEN_BYTES);

    return 0;
}

static void update_cert_store(S390IPLCertificateStore *cert_store,
                              S390IPLCertificate *qcert)
{
    size_t data_size;

    data_size = qcert->size + qcert->key_id_size + qcert->hash_size;

    if (cert_store->max_cert_size < data_size) {
        cert_store->max_cert_size = data_size;
    }

    cert_store->certs[cert_store->count] = *qcert;
    cert_store->total_bytes += data_size;
    cert_store->count++;
}

static GPtrArray *get_cert_paths(void)
{
    const char *path;
    gchar **paths;
    int path_type;
    GDir *dir = NULL;
    const gchar *filename;
    GPtrArray *cert_path_builder;

    cert_path_builder = g_ptr_array_new();

    path = s390_get_boot_certificates();
    if (path == NULL) {
        return cert_path_builder;
    }

    paths = g_strsplit(path, ":", -1);
    while (*paths) {
        /* skip empty certificate path */
        if (!strcmp(*paths, "")) {
            paths += 1;
            continue;
        }

        path_type = check_path_type(*paths);
        if (path_type == S_IFREG) {
            g_ptr_array_add(cert_path_builder, (gpointer) *paths);
        } else if (path_type == S_IFDIR) {
            dir = g_dir_open(*paths, 0, NULL);

            while ((filename = g_dir_read_name(dir))) {
                gchar *cert_path = NULL;
                cert_path = g_build_filename(*paths, filename, NULL);
                g_ptr_array_add(cert_path_builder, (gpointer) cert_path);
            }

            g_dir_close(dir);
        }

        paths += 1;
    }

    return cert_path_builder;
}

void s390_ipl_create_cert_store(S390IPLCertificateStore *cert_store)
{
    GPtrArray *cert_path_builder;

    cert_path_builder = get_cert_paths();
    if (cert_path_builder->len == 0) {
        g_ptr_array_free(cert_path_builder, true);
        return;
    }

    cert_store->max_cert_size = 0;
    cert_store->total_bytes = 0;

    for (int i = 0; i < cert_path_builder->len; i++) {
        S390IPLCertificate *qcert = NULL;
        if (init_cert((char *) cert_path_builder->pdata[i], &qcert) < 0) {
            continue;
        }

        update_cert_store(cert_store, qcert);
    }

    g_ptr_array_free(cert_path_builder, true);
}
