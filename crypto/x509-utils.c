/*
 * X.509 certificate related helpers
 *
 * Copyright (c) 2024 Dorjoy Chowdhury <dorjoychy111@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "crypto/x509-utils.h"

/*
 * Surround with GNUTLS marco to ensure the interfaces are
 * still available when GNUTLS is not enabled.
 */
#ifdef CONFIG_GNUTLS
#include <gnutls/gnutls.h>
#include <gnutls/crypto.h>
#include <gnutls/x509.h>
#include <gnutls/pkcs7.h>

static const int qcrypto_to_gnutls_hash_alg_map[QCRYPTO_HASH_ALGO__MAX] = {
    [QCRYPTO_HASH_ALGO_MD5] = GNUTLS_DIG_MD5,
    [QCRYPTO_HASH_ALGO_SHA1] = GNUTLS_DIG_SHA1,
    [QCRYPTO_HASH_ALGO_SHA224] = GNUTLS_DIG_SHA224,
    [QCRYPTO_HASH_ALGO_SHA256] = GNUTLS_DIG_SHA256,
    [QCRYPTO_HASH_ALGO_SHA384] = GNUTLS_DIG_SHA384,
    [QCRYPTO_HASH_ALGO_SHA512] = GNUTLS_DIG_SHA512,
    [QCRYPTO_HASH_ALGO_RIPEMD160] = GNUTLS_DIG_RMD160,
};

static const int qcrypto_to_gnutls_keyid_flags_map[QCRYPTO_KEYID_FLAGS__MAX] = {
    [QCRYPTO_KEYID_FLAGS_SHA1] = GNUTLS_KEYID_USE_SHA1,
    [QCRYPTO_KEYID_FLAGS_SHA256] = GNUTLS_KEYID_USE_SHA256,
    [QCRYPTO_KEYID_FLAGS_SHA512] = GNUTLS_KEYID_USE_SHA512,
    [QCRYPTO_KEYID_FLAGS_BEST_KNOWN] = GNUTLS_KEYID_USE_BEST_KNOWN,
};

static const int qcrypto_to_gnutls_cert_fmt_map[QCRYPTO_CERT_FMT__MAX] = {
    [QCRYPTO_CERT_FMT_DER] = GNUTLS_X509_FMT_DER,
    [QCRYPTO_CERT_FMT_PEM] = GNUTLS_X509_FMT_PEM,
};

int qcrypto_check_x509_cert_fmt(uint8_t *cert, size_t size,
                                 QCryptoCertFmt fmt, Error **errp)
{
    int rc;
    int ret = 0;
    gnutls_x509_crt_t crt;
    gnutls_datum_t datum = {.data = cert, .size = size};

    if (fmt >= G_N_ELEMENTS(qcrypto_to_gnutls_cert_fmt_map)) {
        error_setg(errp, "Unknown certificate format");
        return ret;
    }

    if (gnutls_x509_crt_init(&crt) < 0) {
        error_setg(errp, "Failed to initialize certificate");
        goto cleanup;
    }

    rc = gnutls_x509_crt_import(crt, &datum, qcrypto_to_gnutls_cert_fmt_map[fmt]);
    if (rc == GNUTLS_E_ASN1_TAG_ERROR) {
        ret = 0;
        goto cleanup;
    }

    ret = 1;

cleanup:
    gnutls_x509_crt_deinit(crt);
    return ret;
}

static int qcrypto_get_x509_cert_fmt(uint8_t *cert, size_t size, Error **errp)
{
    int fmt;

    if (qcrypto_check_x509_cert_fmt(cert, size, QCRYPTO_CERT_FMT_DER, errp)) {
        fmt = GNUTLS_X509_FMT_DER;
    } else if (qcrypto_check_x509_cert_fmt(cert, size, QCRYPTO_CERT_FMT_PEM, errp)) {
        fmt = GNUTLS_X509_FMT_PEM;
    } else {
        return -1;
    }

    return fmt;
}

int qcrypto_get_x509_hash_len(QCryptoHashAlgo alg, Error **errp)
{
    if (alg >= G_N_ELEMENTS(qcrypto_to_gnutls_hash_alg_map)) {
        error_setg(errp, "Unknown hash algorithm");
        return 0;
    }

    return gnutls_hash_get_len(qcrypto_to_gnutls_hash_alg_map[alg]);
}

int qcrypto_get_x509_keyid_len(QCryptoKeyidFlags flag, Error **errp)
{
    QCryptoHashAlgo alg;

    if (flag >= G_N_ELEMENTS(qcrypto_to_gnutls_keyid_flags_map)) {
        error_setg(errp, "Unknown key id flag");
        return 0;
    }

    alg = QCRYPTO_HASH_ALGO_SHA1;
    if ((flag & qcrypto_to_gnutls_keyid_flags_map[QCRYPTO_KEYID_FLAGS_SHA512]) ||
        (flag & qcrypto_to_gnutls_keyid_flags_map[QCRYPTO_KEYID_FLAGS_BEST_KNOWN])) {
        alg = QCRYPTO_HASH_ALGO_SHA512;
    } else if (flag & qcrypto_to_gnutls_keyid_flags_map[QCRYPTO_KEYID_FLAGS_SHA256]) {
        alg = QCRYPTO_HASH_ALGO_SHA256;
    }

    return qcrypto_get_x509_hash_len(alg, errp);
}

int qcrypto_get_x509_cert_fingerprint(uint8_t *cert, size_t size,
                                      QCryptoHashAlgo alg,
                                      uint8_t *result,
                                      size_t *resultlen,
                                      Error **errp)
{
    int ret = -1;
    int hlen;
    gnutls_x509_crt_t crt;
    gnutls_datum_t datum = {.data = cert, .size = size};
    gnutls_x509_crt_fmt_t fmt;

    if (alg >= G_N_ELEMENTS(qcrypto_to_gnutls_hash_alg_map)) {
        error_setg(errp, "Unknown hash algorithm");
        return -1;
    }

    if (result == NULL) {
        error_setg(errp, "No valid buffer given");
        return -1;
    }

    fmt = qcrypto_get_x509_cert_fmt(cert, size, errp);
    if (fmt == -1) {
        error_setg(errp, "Certificate is neither in DER or PEM format");
        return -1;
    }

    gnutls_x509_crt_init(&crt);

    if (gnutls_x509_crt_import(crt, &datum, fmt) != 0) {
        error_setg(errp, "Failed to import certificate");
        goto cleanup;
    }

    hlen = gnutls_hash_get_len(qcrypto_to_gnutls_hash_alg_map[alg]);
    if (*resultlen < hlen) {
        error_setg(errp,
                   "Result buffer size %zu is smaller than hash %d",
                   *resultlen, hlen);
        goto cleanup;
    }

    if (gnutls_x509_crt_get_fingerprint(crt,
                                        qcrypto_to_gnutls_hash_alg_map[alg],
                                        result, resultlen) != 0) {
        error_setg(errp, "Failed to get fingerprint from certificate");
        goto cleanup;
    }

    ret = 0;

 cleanup:
    gnutls_x509_crt_deinit(crt);
    return ret;
}

int qcrypto_get_x509_signature_algorithm(uint8_t *cert, size_t size, Error **errp)
{
    int rc = -1;
    gnutls_x509_crt_t crt;
    gnutls_datum_t datum = {.data = cert, .size = size};
    gnutls_x509_crt_fmt_t fmt;

    fmt = qcrypto_get_x509_cert_fmt(cert, size, errp);
    if (fmt == -1) {
        error_setg(errp, "Certificate is neither in DER or PEM format");
        return rc;
    }

    if (gnutls_x509_crt_init(&crt) < 0) {
        error_setg(errp, "Failed to initialize certificate");
        return rc;
    }

    if (gnutls_x509_crt_import(crt, &datum, fmt) != 0) {
        error_setg(errp, "Failed to import certificate");
        goto cleanup;
    }

    rc = gnutls_x509_crt_get_signature_algorithm(crt);

cleanup:
    gnutls_x509_crt_deinit(crt);
    return rc;
}

int qcrypto_get_x509_cert_version(uint8_t *cert, size_t size, Error **errp)
{
    int rc = -1;
    gnutls_x509_crt_t crt;
    gnutls_datum_t datum = {.data = cert, .size = size};
    gnutls_x509_crt_fmt_t fmt;

    fmt = qcrypto_get_x509_cert_fmt(cert, size, errp);
    if (fmt == -1) {
        error_setg(errp, "Certificate is neither in DER or PEM format");
        return rc;
    }

    if (gnutls_x509_crt_init(&crt) < 0) {
        error_setg(errp, "Failed to initialize certificate");
        return rc;
    }

    if (gnutls_x509_crt_import(crt, &datum, fmt) != 0) {
        error_setg(errp, "Failed to import certificate");
        goto cleanup;
    }

    rc = gnutls_x509_crt_get_version(crt);

cleanup:
    gnutls_x509_crt_deinit(crt);
    return rc;
}

int qcrypto_check_x509_cert_times(uint8_t *cert, size_t size, Error **errp)
{
    int rc = -1;
    gnutls_x509_crt_t crt;
    gnutls_datum_t datum = {.data = cert, .size = size};
    time_t now = time(0);
    gnutls_x509_crt_fmt_t fmt;

    if (now == ((time_t)-1)) {
        error_setg(errp, "Cannot get current time");
        return rc;
    }

    fmt = qcrypto_get_x509_cert_fmt(cert, size, errp);
    if (fmt == -1) {
        error_setg(errp, "Certificate is neither in DER or PEM format");
        return rc;
    }

    if (gnutls_x509_crt_init(&crt) < 0) {
        error_setg(errp, "Failed to initialize certificate");
        return rc;
    }

    if (gnutls_x509_crt_import(crt, &datum, fmt) != 0) {
        error_setg(errp, "Failed to import certificate");
        goto cleanup;
    }

    if (gnutls_x509_crt_get_expiration_time(crt) < now) {
        error_setg(errp, "The certificate has expired");
        goto cleanup;
    }

    if (gnutls_x509_crt_get_activation_time(crt) > now) {
        error_setg(errp, "The certificate is not yet active");
        goto cleanup;
    }

    rc = 0;

cleanup:
    gnutls_x509_crt_deinit(crt);
    return rc;
}

int qcrypto_get_x509_pk_algorithm(uint8_t *cert, size_t size, Error **errp)
{
    int rc = -1;
    unsigned int bits;
    gnutls_x509_crt_t crt;
    gnutls_datum_t datum = {.data = cert, .size = size};
    gnutls_x509_crt_fmt_t fmt;

    fmt = qcrypto_get_x509_cert_fmt(cert, size, errp);
    if (fmt == -1) {
        error_setg(errp, "Certificate is neither in DER or PEM format");
        return rc;
    }

    if (gnutls_x509_crt_init(&crt) < 0) {
        error_setg(errp, "Failed to initialize certificate");
        return rc;
    }

    if (gnutls_x509_crt_import(crt, &datum, fmt) != 0) {
        error_setg(errp, "Failed to import certificate");
        goto cleanup;
    }

    rc = gnutls_x509_crt_get_pk_algorithm(crt, &bits);

cleanup:
    gnutls_x509_crt_deinit(crt);
    return rc;
}

int qcrypto_get_x509_cert_key_id(uint8_t *cert, size_t size,
                                 QCryptoKeyidFlags flag,
                                 uint8_t *result,
                                 size_t *resultlen,
                                 Error **errp)
{
    int ret = -1;
    int keyid_len;
    gnutls_x509_crt_t crt;
    gnutls_datum_t datum = {.data = cert, .size = size};
    gnutls_x509_crt_fmt_t fmt;

    if (flag >= G_N_ELEMENTS(qcrypto_to_gnutls_keyid_flags_map)) {
        error_setg(errp, "Unknown key id flag");
        return -1;
    }

    if (result == NULL) {
        error_setg(errp, "No valid buffer given");
        return -1;
    }

    fmt = qcrypto_get_x509_cert_fmt(cert, size, errp);
    if (fmt == -1) {
        error_setg(errp, "Certificate is neither in DER or PEM format");
        return -1;
    }

    if (gnutls_x509_crt_init(&crt)) {
        error_setg(errp, "Failed to initialize certificate");
        return -1;
    }

    if (gnutls_x509_crt_import(crt, &datum, fmt) != 0) {
        error_setg(errp, "Failed to import certificate");
        goto cleanup;
    }

    keyid_len = qcrypto_get_x509_keyid_len(QCRYPTO_KEYID_FLAGS_SHA256, errp);
    if (*resultlen < keyid_len) {
        error_setg(errp,
                   "Result buffer size %zu is smaller than key id %d",
                   *resultlen, keyid_len);
        goto cleanup;
    }

    if (gnutls_x509_crt_get_key_id(crt,
                                   qcrypto_to_gnutls_keyid_flags_map[flag],
                                   result, resultlen) != 0) {
        error_setg(errp, "Failed to get fingerprint from certificate");
        goto cleanup;
    }

    ret = 0;

cleanup:
    gnutls_x509_crt_deinit(crt);
    return ret;
}

int qcrypto_verify_x509_cert(uint8_t *cert, size_t cert_size,
                            uint8_t *comp, size_t comp_size,
                            uint8_t *sig, size_t sig_size, Error **errp)
{
    int rc = -1;
    gnutls_x509_crt_t crt;
    gnutls_pkcs7_t signature;
    gnutls_datum_t cert_datum = {.data = cert, .size = cert_size};
    gnutls_datum_t data_datum = {.data = comp, .size = comp_size};
    gnutls_datum_t sig_datum = {.data = sig, .size = sig_size};
    gnutls_x509_crt_fmt_t fmt;

    fmt = qcrypto_get_x509_cert_fmt(cert, cert_size, errp);
    if (fmt == -1) {
        error_setg(errp, "Certificate is neither in DER or PEM format");
        return rc;
    }

    if (gnutls_x509_crt_init(&crt) < 0) {
        error_setg(errp, "Failed to initialize certificate");
        return rc;
    }

    if (gnutls_x509_crt_import(crt, &cert_datum, fmt) != 0) {
        error_setg(errp, "Failed to import certificate");
        goto cleanup;
    }

    if (gnutls_pkcs7_init(&signature) < 0) {
        error_setg(errp, "Failed to initalize pkcs7 data.");
        return rc;
    }

    if (gnutls_pkcs7_import(signature, &sig_datum , fmt) != 0) {
        error_setg(errp, "Failed to import signature");
    }

    rc = gnutls_pkcs7_verify_direct(signature, crt, 0, &data_datum, 0);

cleanup:
    gnutls_x509_crt_deinit(crt);
    gnutls_pkcs7_deinit(signature);
    return rc;
}

#else /* ! CONFIG_GNUTLS */

int qcrypto_check_x509_cert_fmt(uint8_t *cert, size_t size,
                                 QCryptoCertFmt fmt, Error **errp)
{
    error_setg(errp, "To get certificate format requires GNUTLS");
    return -ENOTSUP;
}

int qcrypto_get_x509_hash_len(QCryptoHashAlgo alg, Error **errp)
{
    error_setg(errp, "To get hash length requires GNUTLS");
    return -ENOTSUP;
}

int qcrypto_get_x509_keyid_len(QCryptoKeyidFlags flag, Error **errp)
{
    error_setg(errp, "To get key ID length requires GNUTLS");
    return -ENOTSUP;
}

int qcrypto_get_x509_cert_fingerprint(uint8_t *cert, size_t size,
                                      QCryptoHashAlgo hash,
                                      uint8_t *result,
                                      size_t *resultlen,
                                      Error **errp)
{
    error_setg(errp, "To get fingerprint requires GNUTLS");
    return -ENOTSUP;
}

int qcrypto_get_x509_signature_algorithm(uint8_t *cert, size_t size, Error **errp)
{
    error_setg(errp, "To get signature algorithm requires GNUTLS");
    return -ENOTSUP;
}

int qcrypto_get_x509_cert_version(uint8_t *cert, size_t size, Error **errp)
{
    error_setg(errp, "To get certificate version requires GNUTLS");
    return -ENOTSUP;
}

int qcrypto_check_x509_cert_times(uint8_t *cert, size_t size, Error **errp)
{
    error_setg(errp, "To get certificate times requires GNUTLS");
    return -ENOTSUP;
}

int qcrypto_get_x509_pk_algorithm(uint8_t *cert, size_t size, Error **errp)
{
    error_setg(errp, "To get public key algorithm requires GNUTLS");
    return -ENOTSUP;
}

int qcrypto_get_x509_cert_key_id(uint8_t *cert, size_t size,
                                 QCryptoKeyidFlags flag,
                                 uint8_t *result,
                                 size_t *resultlen,
                                 Error **errp)
{
    error_setg(errp, "To get key ID requires GNUTLS");
    return -ENOTSUP;
}

int qcrypto_verify_x509_cert(uint8_t *cert, size_t cert_size,
                             uint8_t *comp, size_t comp_size,
                             uint8_t *sig, size_t sig_size, Error **errp)
{
    error_setg(errp, "signature-verification support requires GNUTLS");
    return -ENOTSUP;
}

#endif /* ! CONFIG_GNUTLS */
