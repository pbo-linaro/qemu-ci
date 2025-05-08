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
#include "hw/s390x/s390-virtio-ccw.h"
#include "qemu/cutils.h"
#include "crypto/x509-utils.h"

static const char *s390_get_boot_certificates(void)
{
    return S390_CCW_MACHINE(qdev_get_machine())->boot_certificates;
}

static size_t cert2buf(char *path, size_t max_size, char **cert_buf)
{
    size_t size;
    g_autofree char *buf;

    /*
     * maximum allowed size of the certificate file to avoid consuming excessive memory
     * (malformed or maliciously large files)
     */
    if (!g_file_get_contents(path, &buf, &size, NULL) ||
        size == 0 || size > max_size) {
        return 0;
    }

    *cert_buf = g_steal_pointer(&buf);

    return size;
}

static S390IPLCertificate *init_cert_x509_der(size_t size, char *raw)
{
    S390IPLCertificate *q_cert = NULL;
    int key_id_size;
    int hash_size;
    int is_der;
    int hash_type;
    Error *err = NULL;

    is_der = qcrypto_check_x509_cert_fmt((uint8_t *)raw, size,
                                         QCRYPTO_CERT_FMT_DER, &err);
    /* return early if GNUTLS is not enabled */
    if (is_der == -ENOTSUP) {
        error_report("GNUTLS is not enabled");
        return q_cert;
    }
    if (!is_der) {
        error_report("The certificate is not in DER format");
        return q_cert;
    }

    hash_type = qcrypto_get_x509_signature_algorithm((uint8_t *)raw, size, &err);
    if (hash_type != QCRYPTO_SIG_ALGO_RSA_SHA256) {
        error_report("The certificate does not use SHA-256 hashing");
        return q_cert;
    }

    key_id_size = qcrypto_get_x509_keyid_len(QCRYPTO_KEYID_FLAGS_SHA256, &err);
    if (key_id_size == 0) {
        error_report("Failed to get certificate key ID size");
        return q_cert;
    }

    hash_size = qcrypto_get_x509_hash_len(QCRYPTO_HASH_ALGO_SHA256, &err);
    if (hash_size == 0) {
        error_report("Failed to get certificate hash size");
        return q_cert;
    }

    q_cert = g_new(S390IPLCertificate, 1);
    q_cert->size = size;
    q_cert->key_id_size = key_id_size;
    q_cert->hash_size = hash_size;
    q_cert->raw = raw;
    q_cert->format = QCRYPTO_CERT_FMT_DER;
    q_cert->hash_type = QCRYPTO_SIG_ALGO_RSA_SHA256;

    return q_cert;
}

static int check_path_type(const char *path)
{
    struct stat path_stat;

    if (stat(path, &path_stat) != 0) {
        perror("stat");
        return -1;
    }

    if (S_ISDIR(path_stat.st_mode)) {
        return S_IFDIR;
    } else if (S_ISREG(path_stat.st_mode)) {
        return S_IFREG;
    } else {
        return -1;
    }
}

static S390IPLCertificate *init_cert(char *paths)
{
    char *buf;
    char vc_name[VC_NAME_LEN_BYTES];
    g_autofree gchar *filename;
    size_t size;
    S390IPLCertificate *qcert = NULL;

    filename = g_path_get_basename(paths);

    size = cert2buf(paths, CERT_MAX_SIZE, &buf);
    if (size == 0) {
        error_report("Failed to load certificate: %s", paths);
        return qcert;
    }

    qcert = init_cert_x509_der(size, buf);
    if (qcert == NULL) {
        error_report("Failed to initialize certificate: %s", paths);
        g_free(buf);
        return qcert;
    }

    /*
     * Left justified certificate name with padding on the right with blanks.
     * Convert certificate name to EBCDIC.
     */
    strpadcpy(vc_name, VC_NAME_LEN_BYTES, filename, ' ');
    ebcdic_put(qcert->vc_name, vc_name, VC_NAME_LEN_BYTES);

    return qcert;
}

static void update_cert_store(S390IPLCertificateStore *cert_store,
                              S390IPLCertificate *qcert)
{
    size_t data_buf_size;
    size_t keyid_buf_size;
    size_t hash_buf_size;
    size_t cert_buf_size;

    /* length field is word aligned for later DIAG use */
    keyid_buf_size = ROUND_UP(qcert->key_id_size, 4);
    hash_buf_size = ROUND_UP(qcert->hash_size, 4);
    cert_buf_size = ROUND_UP(qcert->size, 4);
    data_buf_size = keyid_buf_size + hash_buf_size + cert_buf_size;

    if (cert_store->max_cert_size < data_buf_size) {
        cert_store->max_cert_size = data_buf_size;
    }

    cert_store->certs[cert_store->count] = *qcert;
    cert_store->total_bytes += data_buf_size;
    cert_store->count++;
}

static GPtrArray *get_cert_paths(void)
{
    const char *path;
    gchar **paths;
    gchar **paths_copy;
    int path_type;
    GDir *dir = NULL;
    gchar *cert_path;
    const gchar *filename;
    GPtrArray *cert_path_builder;

    cert_path_builder = g_ptr_array_new();

    path = s390_get_boot_certificates();
    if (path == NULL) {
        return cert_path_builder;
    }

    paths = g_strsplit(path, ":", -1);
    /* save the original pointer for freeing later */
    paths_copy = paths;
    while (*paths) {
        /* skip empty certificate path */
        if (!strcmp(*paths, "")) {
            paths += 1;
            continue;
        }

        cert_path = NULL;
        path_type = check_path_type(*paths);
        if (path_type == S_IFREG) {
            cert_path = g_strdup(*paths);
            g_ptr_array_add(cert_path_builder, cert_path);
        } else if (path_type == S_IFDIR) {
            dir = g_dir_open(*paths, 0, NULL);

            if (dir) {
                while ((filename = g_dir_read_name(dir))) {
                    cert_path = g_build_filename(*paths, filename, NULL);
                    g_ptr_array_add(cert_path_builder, (gpointer) cert_path);
                }

                g_dir_close(dir);
            }
        }

        paths += 1;
    }

    g_strfreev(paths_copy);
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
        S390IPLCertificate *qcert = init_cert((char *) cert_path_builder->pdata[i]);
        if (qcert) {
            update_cert_store(cert_store, qcert);
        }
    }

    g_ptr_array_free(cert_path_builder, true);
}
