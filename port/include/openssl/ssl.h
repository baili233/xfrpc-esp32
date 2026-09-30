// SPDX-License-Identifier: GPL-3.0-only
/*
 * ESP32 port: OpenSSL SSL compatibility stub header.
 *
 * This is a placeholder, not a TLS implementation: TLS towards frps is
 * implemented on mbedtls in port/tls.c and does not use any of it. The
 * symbols exist only so that src/tls.h, src/ssl_compat.h and the
 * HAVE_NGTCP2 branch of src/quic_client_transport.c (not compiled here)
 * resolve, and are stubbed out in port/openssl_compat.c.
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
