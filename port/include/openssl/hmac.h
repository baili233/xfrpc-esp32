// SPDX-License-Identifier: GPL-3.0-only
/* ESP32 port: OpenSSL HMAC compatibility header (one-shot HMAC via mbedtls). */

#ifndef MINI_OPENSSL_HMAC_H
#define MINI_OPENSSL_HMAC_H

#include <stddef.h>
#include "openssl/evp.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One-shot HMAC. md must have space for the digest size (32 for SHA256).
 * Returns md, or NULL on failure. */
unsigned char *HMAC(const EVP_MD *evp_md, const void *key, int key_len,
                    const unsigned char *data, size_t n,
                    unsigned char *md, unsigned int *md_len);

#ifdef __cplusplus
}
#endif

#endif /* MINI_OPENSSL_HMAC_H */
