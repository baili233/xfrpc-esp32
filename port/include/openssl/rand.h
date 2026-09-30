// SPDX-License-Identifier: GPL-3.0-only
/* ESP32 port: OpenSSL RAND compatibility header. */

#ifndef MINI_OPENSSL_RAND_H
#define MINI_OPENSSL_RAND_H

#ifdef __cplusplus
extern "C" {
#endif

/* Fills buf with cryptographically secure random bytes (esp_fill_random).
 * Always returns 1. */
int RAND_bytes(unsigned char *buf, int num);

#ifdef __cplusplus
}
#endif

#endif /* MINI_OPENSSL_RAND_H */
