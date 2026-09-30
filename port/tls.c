// SPDX-License-Identifier: GPL-3.0-only
/*
 * ESP32 port: TLS for the frps control connection, implemented over mbedtls.
 *
 * This is not a port of upstream src/tls.c: that one is built on OpenSSL's
 * bufferevent filter and SSL_set1_host/X509_STORE_CTX. The public interface
 * is kept identical (src/tls.h), so the call sites in src/control.c are
 * unchanged, but the implementation lives here in the port layer where the
 * mbedtls/mini-libevent glue already is.
 *
 * Only compiled when CONFIG_XFRPC_ENABLE_TLS is set; otherwise the weak
 * stubs in port/module_stubs.c provide these symbols.
 *
 * Verification semantics follow the frp Go client, not OpenSSL defaults:
 *   no CA configured  -> MBEDTLS_SSL_VERIFY_NONE   (frps self-signed certs)
 *   CA configured    -> MBEDTLS_SSL_VERIFY_REQUIRED
 * (frp/pkg/transport/tls.go NewClientTLSConfig sets InsecureSkipVerify
 * exactly when caPath is empty.)
 */

#include <string.h>
#include <stdlib.h>

#include "sdkconfig.h"

#include <mbedtls/ssl.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/error.h>

#include "debug.h"
#include "config.h"
#include "utils.h"
#include "tls.h"
#include "mini_event.h"

/* from port/mini_event_ssl.c */
extern int mini_tls_attach(struct bufferevent *bev, int fd,
                           const mbedtls_ssl_config *config,
                           const char *hostname);
extern const char *mini_tls_last_error_str(void);

/* Process-wide TLS state, created by xfrpc_tls_init() and freed by xfrpc_tls_cleanup(). */
static struct {
    int                     initialized;
    int                     ok;          /* init succeeded */
    mbedtls_ssl_config      conf;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context drbg;
    mbedtls_x509_crt        ca_chain;    /* trusted CAs (verification) */
    mbedtls_x509_crt        own_cert;    /* client certificate (mTLS) */
    mbedtls_pk_context      own_key;     /* client private key (mTLS) */
    int                     verify;      /* MBEDTLS_SSL_VERIFY_* */
    char                    err[96];
} g_tls;

static void tls_set_err(int rc, const char *what)
{
    mbedtls_strerror(rc, g_tls.err, sizeof(g_tls.err));
    if (g_tls.err[0] == '\0')
        snprintf(g_tls.err, sizeof(g_tls.err),
                 "mbedtls error -0x%04X", -rc);
    debug(LOG_ERR, "TLS: %s failed: %s", what, g_tls.err);
}

/* ------------------------------------------------------------------ */
/* certificate material                                                */
/* ------------------------------------------------------------------ */

/*
 * The API/Kconfig supply PEM text, but a config file path is also accepted
 * (the fields already exist and the ESP32 may have a mounted filesystem).
 * Returns a malloc'd NUL-terminated copy, or NULL if nothing is configured.
 */
static char *tls_load_pem(const char *pem, const char *path, const char *what)
{
    if (pem && pem[0])
        return strdup(pem);

    if (path && path[0]) {
        FILE *f = fopen(path, "rb");
        if (!f) {
            debug(LOG_WARNING, "TLS: cannot open %s '%s'", what, path);
            return NULL;
        }
        if (fseek(f, 0, SEEK_END) != 0) {
            fclose(f);
            return NULL;
        }
        long sz = ftell(f);
        if (sz <= 0 || sz > 64 * 1024) {
            debug(LOG_WARNING, "TLS: %s '%s' has unsupported size %ld",
                  what, path, sz);
            fclose(f);
            return NULL;
        }
        rewind(f);
        char *buf = malloc((size_t)sz + 1);
        if (!buf) {
            fclose(f);
            return NULL;
        }
        size_t rd = fread(buf, 1, (size_t)sz, f);
        fclose(f);
        buf[rd] = '\0';
        return buf;
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* public interface (src/tls.h)                                        */
/* ------------------------------------------------------------------ */

int xfrpc_tls_init(void)
{
    if (g_tls.initialized)
        return g_tls.ok ? 0 : -1;
    g_tls.initialized = 1;
    g_tls.ok = 0;
    g_tls.verify = MBEDTLS_SSL_VERIFY_NONE;

    struct common_conf *c = get_common_config();
    if (!c) {
        debug(LOG_ERR, "TLS: no common config");
        return -1;
    }

    mbedtls_ssl_config_init(&g_tls.conf);
    mbedtls_entropy_init(&g_tls.entropy);
    mbedtls_ctr_drbg_init(&g_tls.drbg);
    mbedtls_x509_crt_init(&g_tls.ca_chain);
    mbedtls_x509_crt_init(&g_tls.own_cert);
    mbedtls_pk_init(&g_tls.own_key);

    int rc = mbedtls_ctr_drbg_seed(&g_tls.drbg, mbedtls_entropy_func,
                                   &g_tls.entropy, NULL, 0);
    if (rc != 0) {
        tls_set_err(rc, "ctr_drbg_seed");
        return -1;
    }

    rc = mbedtls_ssl_config_defaults(&g_tls.conf,
                                     MBEDTLS_SSL_IS_CLIENT,
                                     MBEDTLS_SSL_TRANSPORT_STREAM,
                                     MBEDTLS_SSL_PRESET_DEFAULT);
    if (rc != 0) {
        tls_set_err(rc, "ssl_config_defaults");
        return -1;
    }
    mbedtls_ssl_conf_rng(&g_tls.conf, mbedtls_ctr_drbg_random, &g_tls.drbg);
    mbedtls_ssl_conf_authmode(&g_tls.conf, MBEDTLS_SSL_VERIFY_NONE);

    /* --- verification: only when a CA is actually configured --- */
    char *ca = tls_load_pem(c->tls_trusted_ca_pem, c->tls_trusted_ca_file,
                            "CA certificate");
    if (ca) {
        rc = mbedtls_x509_crt_parse(&g_tls.ca_chain,
                                    (const unsigned char *)ca, strlen(ca) + 1);
        free(ca);
        if (rc != 0) {
            tls_set_err(rc, "x509_crt_parse(CA)");
            return -1;
        }
        mbedtls_ssl_conf_ca_chain(&g_tls.conf, &g_tls.ca_chain, NULL);
        mbedtls_ssl_conf_authmode(&g_tls.conf, MBEDTLS_SSL_VERIFY_REQUIRED);
        g_tls.verify = MBEDTLS_SSL_VERIFY_REQUIRED;
    } else {
        debug(LOG_WARNING, "TLS: no CA configured, peer certificate will NOT "
              "be verified (frp client behaviour without caFile)");
    }

    /* --- optional client certificate (mTLS) --- */
    char *cert = tls_load_pem(c->tls_cert_pem, c->tls_cert_file,
                              "client certificate");
    char *key = tls_load_pem(c->tls_key_pem, c->tls_key_file,
                             "client private key");
    if (cert) {
        rc = mbedtls_x509_crt_parse(&g_tls.own_cert,
                                    (const unsigned char *)cert, strlen(cert) + 1);
        free(cert);
        if (rc != 0) {
            tls_set_err(rc, "x509_crt_parse(client cert)");
            if (key)
                free(key);
            return -1;
        }
        if (!key) {
            debug(LOG_ERR, "TLS: client certificate given without a key");
            return -1;
        }
        rc = mbedtls_pk_parse_key(&g_tls.own_key,
                                  (const unsigned char *)key, strlen(key) + 1,
                                  NULL, 0,
                                  mbedtls_ctr_drbg_random, &g_tls.drbg);
        free(key);
        if (rc != 0) {
            tls_set_err(rc, "pk_parse_key");
            return -1;
        }
        rc = mbedtls_ssl_conf_own_cert(&g_tls.conf, &g_tls.own_cert,
                                       &g_tls.own_key);
        if (rc != 0) {
            tls_set_err(rc, "ssl_conf_own_cert");
            return -1;
        }
        debug(LOG_INFO, "TLS: client certificate (mTLS) configured");
    } else if (key) {
        free(key);
        debug(LOG_WARNING, "TLS: client key configured without a certificate");
    }

    g_tls.ok = 1;
    debug(LOG_INFO, "TLS: initialized (verify=%s)",
          g_tls.verify == MBEDTLS_SSL_VERIFY_REQUIRED ? "required" : "none");
    return 0;
}

/*
 * Wrap `bev` (a connected-or-connecting TCP bufferevent) with TLS.
 *
 * The contract from src/tls.h: on failure the original bev is freed and NULL
 * is returned. The socket fd is taken over by the TLS bev, which therefore
 * keeps BEV_OPT_CLOSE_ON_FREE — the ownership never leaves the bufferevent
 * layer, only the I/O path changes.
 */
struct bufferevent *xfrpc_tls_wrap_bev(struct event_base *base, struct bufferevent *bev)
{
    (void)base;

    if (!bev) {
        debug(LOG_ERR, "TLS: no bufferevent to wrap");
        return NULL;
    }
    if (!g_tls.initialized || !g_tls.ok) {
        debug(LOG_ERR, "TLS: xfrpc_tls_init() was not successful");
        bufferevent_free(bev);
        return NULL;
    }

    int fd = bufferevent_getfd(bev);
    if (fd < 0) {
        debug(LOG_ERR, "TLS: bufferevent has no socket");
        bufferevent_free(bev);
        return NULL;
    }

    /* SNI plus CN/SAN verification name: the explicit name wins, otherwise
     * the address the connection was made to. mbedtls_ssl_set_hostname()
     * only accepts names, so a literal IP is skipped (frps certificates are
     * usually issued for a hostname, and with no CA there is nothing to
     * verify against anyway). */
    struct common_conf *c = get_common_config();
    const char *host = (c && c->tls_server_name && c->tls_server_name[0])
                       ? c->tls_server_name
                       : (c ? c->server_addr : NULL);
    if (host && is_valid_ip_address(host)) {
        if (c && c->tls_server_name && c->tls_server_name[0])
            debug(LOG_WARNING, "TLS: server_name '%s' is an IP address, "
                  "SNI/certificate check will be skipped", host);
        host = NULL;
    }

    if (mini_tls_attach(bev, fd, &g_tls.conf, host) != 0) {
        debug(LOG_ERR, "TLS: failed to attach the mbedtls transport");
        bufferevent_free(bev);
        return NULL;
    }

    debug(LOG_INFO, "TLS: wrapping connection to %s (SNI %s)",
          c ? c->server_addr : "?", host ? host : "-");
    return bev;
}

int xfrpc_tls_is_enabled(void)
{
    struct common_conf *c = get_common_config();
    return (c && c->tls_enable) ? 1 : 0;
}

void xfrpc_tls_log_errors(const char *context)
{
    const char *tls_err = mini_tls_last_error_str();
    if (tls_err)
        debug(LOG_ERR, "%s: %s", context ? context : "TLS", tls_err);
    else if (g_tls.err[0])
        debug(LOG_ERR, "%s: %s", context ? context : "TLS", g_tls.err);
    else
        debug(LOG_DEBUG, "%s: no TLS error recorded", context ? context : "TLS");
}

void xfrpc_tls_cleanup(void)
{
    if (!g_tls.initialized)
        return;
    mbedtls_pk_free(&g_tls.own_key);
    mbedtls_x509_crt_free(&g_tls.own_cert);
    mbedtls_x509_crt_free(&g_tls.ca_chain);
    mbedtls_ssl_config_free(&g_tls.conf);
    mbedtls_ctr_drbg_free(&g_tls.drbg);
    mbedtls_entropy_free(&g_tls.entropy);
    memset(&g_tls, 0, sizeof(g_tls));
}

/*
 * Configure an externally created TLS context (used by the QUIC transport,
 * which builds its own SSL_CTX). Kept for interface compatibility; the
 * OpenSSL-shaped stub in port/openssl/ssl.h has no CA store, so there is
 * nothing to configure and the QUIC path does not call this yet.
 */
int xfrpc_tls_load_certs_to_ctx(void *ctx)
{
    (void)ctx;
    debug(LOG_DEBUG, "TLS: xfrpc_tls_load_certs_to_ctx() is a no-op in the mbedtls "
          "port (QUIC transport not enabled)");
    return g_tls.ok ? 0 : -1;
}