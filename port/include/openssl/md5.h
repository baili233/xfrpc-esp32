// SPDX-License-Identifier: GPL-3.0-only
/* ESP32 port: OpenSSL MD5 compatibility header (unused stub declarations). */

#ifndef MINI_OPENSSL_MD5_H
#define MINI_OPENSSL_MD5_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MD5_DIGEST_LENGTH 16

unsigned char *MD5(const unsigned char *d, size_t n, unsigned char *md);

#ifdef __cplusplus
}
#endif

#endif /* MINI_OPENSSL_MD5_H */
