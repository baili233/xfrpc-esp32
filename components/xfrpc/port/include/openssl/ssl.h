// SPDX-License-Identifier: GPL-3.0-only
/*
 * ESP32 port: OpenSSL SSL compatibility stub header.
 *
 * TLS is not implemented in the MVP (XFRPC_ENABLE_TLS is off). This header
 * only exists so ssl_compat.h / upstream includes resolve. If TLS gets
 * implemented later it will be via mbedtls with a real shim here.
 */

#ifndef MINI_OPENSSL_SSL_H
#define MINI_OPENSSL_SSL_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mini_ssl_ctx_st SSL_CTX;
typedef struct mini_ssl_st SSL;
typedef struct mini_ssl_method_st SSL_METHOD;

#define SSL_VERIFY_NONE 0x00
#define SSL_VERIFY_PEER 0x01

const SSL_METHOD *TLS_client_method(void);
SSL_CTX *SSL_CTX_new(const SSL_METHOD *);
void SSL_CTX_free(SSL_CTX *);

#ifdef __cplusplus
}
#endif

#endif /* MINI_OPENSSL_SSL_H */
