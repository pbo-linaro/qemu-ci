/*
 * X.509 certificate related helpers
 *
 * Copyright (c) 2024 Dorjoy Chowdhury <dorjoychy111@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */

#ifndef QCRYPTO_X509_UTILS_H
#define QCRYPTO_X509_UTILS_H

#include "crypto/hash.h"

int qcrypto_get_x509_cert_fingerprint(uint8_t *cert, size_t size,
                                      QCryptoHashAlgo hash,
                                      uint8_t *result,
                                      size_t *resultlen,
                                      Error **errp);

/**
 * qcrypto_check_x509_cert_fmt
 * @cert: pointer to the raw certiricate data
 * @size: size of the certificate
 * @fmt: expected certificate format
 * @errp: error pointer
 *
 * Check whether the format of @cert matches @fmt.
 *
 * Returns: 0 if the format of @cert matches @fmt,
           -1 if the format does not match,
 *         -ENOTSUP if GNUTLS is not enabled.
 */
int qcrypto_check_x509_cert_fmt(uint8_t *cert, size_t size,
                                QCryptoCertFmt fmt, Error **errp);

/**
 * qcrypto_get_x509_hash_len
 * @alg: the hash algorithm
 *
 * Determine the length of the hash of the given @alg.
 *
 * Returns: the length on success,
            0 on error,
            -ENOTSUP if GNUTLS is not enabled.
 */
int qcrypto_get_x509_hash_len(QCryptoHashAlgo alg);

/**
 * qcrypto_get_x509_keyid_len
 * @flag: the key ID flag
 *
 * Determine the length of the key ID of the given @flag.
 *
 * Returns: the length on success,
            0 on error,
            -ENOTSUP if GNUTLS is not enabled.
 */
int qcrypto_get_x509_keyid_len(QCryptoKeyidFlags flag);

/**
 * qcrypto_get_x509_signature_algorithm
 * @cert: pointer to the raw certiricate data
 * @size: size of the certificate
 * @errp: error pointer
 *
 * Determine the signature algorithm used to sign the @cert.
 *
 * Returns: a value from the QCryptoSigAlgo enum on success,
 *          -1 on error,
 *          -ENOTSUP if GNUTLS is not enabled.
 */
int qcrypto_get_x509_signature_algorithm(uint8_t *cert, size_t size, Error **errp);

/**
 * qcrypto_get_x509_cert_version
 * @cert: pointer to the raw certiricate data
 * @size: size of the certificate
 * @errp: error pointer
 *
 * Determine the version of the @cert.
 *
 * Returns: version of certificate on success,
 *          negative error code on error,
 *          -ENOTSUP if GNUTLS is not enabled.
 */
int qcrypto_get_x509_cert_version(uint8_t *cert, size_t size, Error **errp);

/**
 * qcrypto_check_x509_cert_times
 * @cert: pointer to the raw certiricate data
 * @size: size of the certificate
 * @errp: error pointer
 *
 * Check whether the @cert activation and expiration times are valid at the current time.
 *
 * Returns: 0 if the certificate times are valid,
 *         -1 on error,
 *         -ENOTSUP if GNUTLS is not enabled.
 */
int qcrypto_check_x509_cert_times(uint8_t *cert, size_t size, Error **errp);

/**
 * qcrypto_get_x509_pk_algorithm
 * @cert: pointer to the raw certiricate data
 * @size: size of the certificate
 * @errp: error pointer
 *
 * Determine the public key algorithm of the @cert.
 *
 * Returns: a value from the QCryptoPkAlgo enum on success,
 *          -1 on error,
 *          -ENOTSUP if GNUTLS is not enabled.
 */
int qcrypto_get_x509_pk_algorithm(uint8_t *cert, size_t size, Error **errp);

/**
 * qcrypto_get_x509_cert_key_id
 * @cert: pointer to the raw certiricate data
 * @size: size of the certificate
 * @flag: the key ID flag
 * @result: pointer to a buffer to store output key ID (may not be null)
 * @resultlen: pointer to the size of the buffer
 * @errp: error pointer
 *
 * Retrieve the key ID from the @cert based on the specified @flag.
 *
 * Returns: 0 if key ID was successfully stored in @result,
 *         -1 on error,
 *         -ENOTSUP if GNUTLS is not enabled.
 */
int qcrypto_get_x509_cert_key_id(uint8_t *cert, size_t size,
                                 QCryptoKeyidFlags flag,
                                 uint8_t *result,
                                 size_t *resultlen,
                                 Error **errp);

/**
 * qcrypto_verify_x509_cert
 * @cert: pointer to the raw certiricate data
 * @cert_size: size of the certificate
 * @comp: pointer to the component to be verified
 * @comp_size: size of the component
 * @sig: pointer to the signature
 * @sig_size: size of the signature
 * @errp: error pointer
 *
 * Verify the provided @comp against the @sig and @cert.
 *
 * Returns: 0 on success,
 *          negative error code on error,
 *          -ENOTSUP if GNUTLS is not enabled.
 */
int qcrypto_verify_x509_cert(uint8_t *cert, size_t cert_size,
                             uint8_t *comp, size_t comp_size,
                             uint8_t *sig, size_t sig_size, Error **errp);

#endif
