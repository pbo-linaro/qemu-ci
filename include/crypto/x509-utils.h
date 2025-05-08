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

int qcrypto_check_x509_cert_fmt(uint8_t *cert, size_t size,
                                 QCryptoCertFmt fmt, Error **errp);
int qcrypto_get_x509_hash_len(QCryptoHashAlgo alg, Error **errp);
int qcrypto_get_x509_keyid_len(QCryptoKeyidFlags flag, Error **errp);
int qcrypto_get_x509_signature_algorithm(uint8_t *cert, size_t size, Error **errp);

int qcrypto_get_x509_cert_version(uint8_t *cert, size_t size, Error **errp);
int qcrypto_check_x509_cert_times(uint8_t *cert, size_t size, Error **errp);
int qcrypto_get_x509_pk_algorithm(uint8_t *cert, size_t size, Error **errp);

int qcrypto_get_x509_cert_key_id(uint8_t *cert, size_t size,
                                 QCryptoKeyidFlags flag,
                                 uint8_t *result,
                                 size_t *resultlen,
                                 Error **errp);

#endif
