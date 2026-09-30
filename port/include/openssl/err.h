// SPDX-License-Identifier: GPL-3.0-only
/* ESP32 port: OpenSSL ERR compatibility header — no-ops (error stack unused). */

#ifndef MINI_OPENSSL_ERR_H
#define MINI_OPENSSL_ERR_H

#ifdef __cplusplus
extern "C" {
#endif

unsigned long ERR_get_error(void);
void ERR_error_string_n(unsigned long e, char *buf, size_t len);
void ERR_clear_error(void);

#ifdef __cplusplus
}
#endif

#endif /* MINI_OPENSSL_ERR_H */
