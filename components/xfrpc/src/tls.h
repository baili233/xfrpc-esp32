// SPDX-License-Identifier: GPL-3.0-only
/*
 * Copyright (c) 2023 Dengfeng Liu <liudf0716@gmail.com>
 *
 * ESP32 port: interface only. Upstream implements this on OpenSSL via
 * libevent's SSL bufferevent filter; here it is implemented on mbedtls in
 * port/tls.c (CONFIG_XFRPC_ENABLE_TLS=y) or stubbed in port/module_stubs.c
 * (CONFIG_XFRPC_ENABLE_TLS=n). src/control.c is the only caller.
 *
 * The xfrpc_ prefix keeps these names out of the way of the unrelated
 * tls_init() that lives in an ESP-IDF system library (wpa_supplicant).
 */

#ifndef XFRPC_TLS_H
#define XFRPC_TLS_H

#include <event2/bufferevent.h>
#include <event2/bufferevent_ssl.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

/**
 * Initialize the global TLS/SSL context using configuration from common_conf.
 * Must be called once before any TLS connections are made.
 *
 * @return 0 on success, -1 on failure
 */
int xfrpc_tls_init(void);

/**
 * Wrap an existing TCP bufferevent with TLS.
 * The original bev is consumed; returns a new SSL-wrapped bev on success,
 * or NULL on failure (original bev is freed on failure).
 *
 * @param base   Event base for the new bufferevent
 * @param bev    The raw TCP bufferevent to wrap (consumed on success)
 * @return       TLS-wrapped bufferevent, or NULL on error
 */
struct bufferevent *xfrpc_tls_wrap_bev(struct event_base *base, struct bufferevent *bev);

/**
 * Clean up and free the global TLS context.
 * Call during shutdown.
 */
void xfrpc_tls_cleanup(void);

/**
 * Check if TLS is enabled in configuration.
 *
 * @return 1 if TLS is enabled, 0 otherwise
 */
int xfrpc_tls_is_enabled(void);

/**
 * Print OpenSSL error details to debug log.
 *
 * @param context  Description string for the error context
 */
void xfrpc_tls_log_errors(const char *context);

/**
 * Load TLS certificates from config into an SSL_CTX.
 * Uses void* to avoid type conflicts between OpenSSL and wolfSSL headers.
 * The actual type is SSL_CTX* (OpenSSL) or WOLFSSL_CTX* (wolfSSL).
 *
 * @param ctx  The SSL_CTX to configure (passed as void* for compatibility)
 * @return 0 on success, -1 on failure
 */
int xfrpc_tls_load_certs_to_ctx(void *ctx);

#endif /* XFRPC_TLS_H */
