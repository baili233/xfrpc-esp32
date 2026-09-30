// SPDX-License-Identifier: GPL-3.0-only
/*
 * ESP32 port: mbedtls backend for the mini-libevent bufferevent.
 *
 * port/mini_event.c knows nothing about TLS; it drives an opaque
 * mini_bev_ssl_ops vtable (see port/include/mini_event.h). This file
 * implements that vtable on top of an mbedtls_ssl_context and owns the
 * BIO callbacks that connect mbedtls to the bufferevent's non-blocking
 * socket.
 *
 * Only compiled when CONFIG_XFRPC_ENABLE_TLS is set.
 */

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <lwip/sockets.h>

#include <mbedtls/ssl.h>
#include <mbedtls/net_sockets.h>   /* MBEDTLS_ERR_NET_SEND_FAILED / _RECV_FAILED */
#include <mbedtls/error.h>

#include "debug.h"
#include "mini_event.h"

/* Per-connection TLS state. One instance per bufferevent. */
struct mini_tls_conn {
    mbedtls_ssl_context ssl;
    int                 fd;
    int                 active;      /* ssl_setup() succeeded */
    int                 got_eof;     /* peer close_notify / clean EOF */
    int                 last_err;    /* last mbedtls error code, for logging */
    char                err_buf[96];
};

/*
 * Last human-readable TLS error seen by this module, for tls_log_errors().
 * control.c calls that function without a bufferevent at hand (it reports on
 * the control connection as a whole), so the error is kept module-wide
 * rather than per transport.
 */
static char g_tls_last_err[96];

/* ------------------------------------------------------------------ */
/* BIO: mbedtls <-> non-blocking lwip socket                           */
/* ------------------------------------------------------------------ */

static int tls_bio_send(void *ctx, const unsigned char *buf, size_t len)
{
    struct mini_tls_conn *c = ctx;
    int ret;

    do {
        ret = (int)send(c->fd, buf, len, 0);
    } while (ret < 0 && errno == EINTR);

    if (ret < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return MBEDTLS_ERR_SSL_WANT_WRITE;
        return MBEDTLS_ERR_NET_SEND_FAILED;
    }
    return ret;
}

static int tls_bio_recv(void *ctx, unsigned char *buf, size_t len)
{
    struct mini_tls_conn *c = ctx;
    int ret;

    do {
        ret = (int)recv(c->fd, buf, len, 0);
    } while (ret < 0 && errno == EINTR);

    if (ret < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return MBEDTLS_ERR_SSL_WANT_READ;
        return MBEDTLS_ERR_NET_RECV_FAILED;
    }
    /* 0 = clean TCP shutdown. Returning MBEDTLS_ERR_SSL_WANT_READ here would
     * spin the event loop forever (select() reports EOF immediately), so
     * this must be reported as the end of the stream. */
    if (ret == 0)
        return MBEDTLS_ERR_SSL_CONN_EOF;
    return ret;
}

/* ------------------------------------------------------------------ */
/* error translation                                                   */
/* ------------------------------------------------------------------ */

static void tls_record_error(struct mini_tls_conn *c, int rc)
{
    c->last_err = rc;
    mbedtls_strerror(rc, c->err_buf, sizeof(c->err_buf));
    if (c->err_buf[0] == '\0')
        snprintf(c->err_buf, sizeof(c->err_buf), "mbedtls error -0x%04X", -rc);
    strncpy(g_tls_last_err, c->err_buf, sizeof(g_tls_last_err) - 1);
    g_tls_last_err[sizeof(g_tls_last_err) - 1] = '\0';
}

/*
 * Classify an mbedtls return code.
 *   TLS_RET_OK    — made progress
 *   TLS_RET_WANT  — needs socket readiness (*want_read / *want_write)
 *   TLS_RET_EOF   — peer closed the stream
 *   TLS_RET_ERR   — fatal
 */
enum { TLS_RET_ERR = -1, TLS_RET_OK = 0, TLS_RET_WANT = 1, TLS_RET_EOF = 2 };

static int tls_classify(struct mini_tls_conn *c, int rc,
                        int *want_read, int *want_write)
{
    switch (rc) {
    case 0:
        return TLS_RET_OK;
    case MBEDTLS_ERR_SSL_WANT_READ:
        *want_read = 1;
        return TLS_RET_WANT;
    case MBEDTLS_ERR_SSL_WANT_WRITE:
        *want_write = 1;
        return TLS_RET_WANT;
    case MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY:
    case MBEDTLS_ERR_SSL_CONN_EOF:
        c->got_eof = 1;
        return TLS_RET_EOF;
    default:
        /* TLS 1.3 NewSessionTicket is informational; mbedtls only signals it
         * when MBEDTLS_SSL_TLS1_3_SIGNAL_NEW_SESSION_TICKETS is enabled,
         * which the IDF default configuration does not, but treating it as
         * "no progress" is correct either way. */
        if (rc == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET) {
            *want_read = 1;
            return TLS_RET_WANT;
        }
        /* Renegotiation / a re-handshake request must be driven by calling
         * the handshake function again, which a WANT_READ equivalent does. */
        if (rc == MBEDTLS_ERR_SSL_WAITING_SERVER_HELLO_RENEGO ||
            rc == MBEDTLS_ERR_SSL_CRYPTO_IN_PROGRESS) {
            *want_read = 1;
            return TLS_RET_WANT;
        }
        tls_record_error(c, rc);
        debug(LOG_ERR, "TLS: %s (rc -0x%04X)", c->err_buf, -rc);
        return TLS_RET_ERR;
    }
}

/* ------------------------------------------------------------------ */
/* mini_bev_ssl_ops implementation                                     */
/* ------------------------------------------------------------------ */

static int tls_op_handshake(void *ctx, int *want_read, int *want_write)
{
    struct mini_tls_conn *c = ctx;
    int rc = mbedtls_ssl_handshake(&c->ssl);

    if (rc == 0) {
        debug(LOG_INFO, "TLS: handshake done, %s, cipher suite %s",
              mbedtls_ssl_get_version(&c->ssl),
              mbedtls_ssl_get_ciphersuite(&c->ssl));
        return 0;
    }
    int cls = tls_classify(c, rc, want_read, want_write);
    if (cls == TLS_RET_ERR)
        return -1;
    if (cls == TLS_RET_EOF)
        return -1;
    return 1;   /* still in progress */
}

static int tls_op_read(void *ctx, unsigned char *buf, size_t cap,
                       int *want_read, int *want_write)
{
    struct mini_tls_conn *c = ctx;

    if (c->got_eof)
        return MINI_BEV_SSL_EOF;

    int rc = mbedtls_ssl_read(&c->ssl, buf, cap);
    if (rc > 0)
        return rc;

    int cls = tls_classify(c, rc, want_read, want_write);
    switch (cls) {
    case TLS_RET_EOF:
        return MINI_BEV_SSL_EOF;
    case TLS_RET_ERR:
        return -1;
    case TLS_RET_WANT:
        return 0;
    default:
        return 0;   /* rc == 0: empty record; treat as "nothing yet" */
    }
}

/*
 * mbedtls_ssl_write() accepts at most one record's worth of data per call
 * and, when a previous record is still partially flushed, refuses the new
 * buffer entirely (it replays ssl->out_left first). It also requires the
 * *same* arguments on retry, which is why this call is given the whole
 * pending evbuffer and only the accepted byte count is reported back.
 */
static int tls_op_write(void *ctx, const unsigned char *buf, size_t len,
                        size_t *done, int *want_read, int *want_write)
{
    struct mini_tls_conn *c = ctx;
    *done = 0;

    if (c->got_eof)
        return -1;

    int rc = mbedtls_ssl_write(&c->ssl, buf, len);
    if (rc > 0) {
        *done = (size_t)rc;
        return 0;
    }
    /* Classified purely for the want flags and the error log: a retryable
     * code leaves *done at 0 and the caller retries the same buffer.
     * TLS_RET_OK is unreachable here (rc <= 0). */
    return (tls_classify(c, rc, want_read, want_write) == TLS_RET_ERR) ? -1 : 0;
}

static int tls_op_pending(void *ctx)
{
    struct mini_tls_conn *c = ctx;
    int avail = (int)mbedtls_ssl_get_bytes_avail(&c->ssl);
    if (avail > 0)
        return avail;
    /* mbedtls_check_pending() also reports a record that has been read into
     * the internal buffer but not yet handed to the application. */
    return mbedtls_ssl_check_pending(&c->ssl) ? 1 : 0;
}

static void tls_op_free_ctx(void *ctx)
{
    struct mini_tls_conn *c = ctx;
    if (!c)
        return;
    if (c->active)
        mbedtls_ssl_free(&c->ssl);
    free(c);
}

static const struct mini_bev_ssl_ops mini_tls_bev_ops = {
    .handshake = tls_op_handshake,
    .read      = tls_op_read,
    .write     = tls_op_write,
    .pending   = tls_op_pending,
    .free_ctx  = tls_op_free_ctx,
};

/* ------------------------------------------------------------------ */
/* construction                                                        */
/* ------------------------------------------------------------------ */

/*
 * Create the transport for `bev` (which wraps the socket `fd`) and install
 * it. `config` is the shared mbedtls_ssl_config built by tls_init().
 * `hostname` enables SNI and certificate name checking and may be NULL.
 *
 * Returns 0 on success (the bev now owns the transport), -1 otherwise.
 */
int mini_tls_attach(struct bufferevent *bev, int fd,
                    const mbedtls_ssl_config *config, const char *hostname)
{
    if (!bev || fd < 0 || !config)
        return -1;

    struct mini_tls_conn *c = calloc(1, sizeof(struct mini_tls_conn));
    if (!c)
        return -1;

    c->fd = fd;
    mbedtls_ssl_init(&c->ssl);

    int rc = mbedtls_ssl_setup(&c->ssl, config);
    if (rc != 0) {
        tls_record_error(c, rc);
        debug(LOG_ERR, "TLS: mbedtls_ssl_setup failed: %s", c->err_buf);
        goto fail;
    }
    c->active = 1;

    /* mbedtls calls set_hostname() itself with the configured name; doing it
     * here keeps SNI and the CN/SAN check in sync. */
    if (hostname && hostname[0]) {
        rc = mbedtls_ssl_set_hostname(&c->ssl, hostname);
        if (rc != 0) {
            tls_record_error(c, rc);
            debug(LOG_ERR, "TLS: set_hostname('%s') failed: %s",
                  hostname, c->err_buf);
            goto fail;
        }
    }

    mbedtls_ssl_set_bio(&c->ssl, c, tls_bio_send, tls_bio_recv, NULL);

    mini_bev_set_ssl(bev, c, &mini_tls_bev_ops);
    return 0;

fail:
    mbedtls_ssl_free(&c->ssl);
    free(c);
    return -1;
}

/*
 * Last human-readable TLS error seen by this module, for tls_log_errors().
 * Returns NULL when nothing has failed since the last reset.
 */
const char *mini_tls_last_error_str(void)
{
    return g_tls_last_err[0] ? g_tls_last_err : NULL;
}