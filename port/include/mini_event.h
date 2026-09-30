// SPDX-License-Identifier: GPL-3.0-only
/*
 * ESP32 port: public extras of the mini-libevent implementation
 * (port/mini_event.c) that go beyond the libevent API.
 */

#ifndef MINI_EVENT_EXTRAS_H
#define MINI_EVENT_EXTRAS_H

#include "event2/event.h"
#include "event2/bufferevent.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Stop the event loop for good (as opposed to event_base_loopbreak(),
 * which xfrpc uses internally for one round of its reconnect loop).
 *
 * After this call event_base_dispatch(base) returns permanently, so
 * run_control() exits and the xfrpc task can finish. Safe to call from
 * another task (e.g. xfrpc_stop() from the main task).
 */
void mini_event_base_stop(struct event_base *base);

/* ------------------------------------------------------------------ */
/* Optional transport backend (TLS)                                    */
/* ------------------------------------------------------------------ */

/*
 * mini_event.c itself has no crypto dependency: a bufferevent can be
 * switched to an opaque transport whose operations live behind this
 * vtable. The only implementation is port/mini_event_ssl.c (mbedtls),
 * installed by xfrpc_tls_wrap_bev().
 *
 * Retry convention: the `want_read` / `want_write` out-parameters are
 * reset to 0 by the caller before every call and set by the callee when it
 * could not make progress; the event loop then waits for the corresponding
 * readiness instead of busy-looping. MINI_BEV_SSL_EOF is a distinct return
 * value because "peer closed" and "nothing to read right now" must not be
 * confused — the former tears the connection down.
 */
#define MINI_BEV_SSL_EOF (-2)

struct mini_bev_ssl_ops {
    /* 0 = handshake finished, 1 = still in progress (at least one want flag
     * set, call again once the socket is ready), -1 = fatal error. A
     * handshake needs several round trips, so "finished" and "not yet" must
     * be distinguishable — that is what the separate 1 is for. */
    int  (*handshake)(void *tls, int *want_read, int *want_write);

    /* >0 = plaintext bytes read; MINI_BEV_SSL_EOF = peer closed;
     * -1 = error (errno set); 0 = nothing available, retry per *want_*. */
    int  (*read)(void *tls, unsigned char *buf, size_t cap,
                 int *want_read, int *want_write);

    /* *done = plaintext bytes this call accepted (0 = retry per *want_*).
     * Returns -1 on error. Callers must retry with the same arguments,
     * because mbedtls keeps the partially flushed record inline. */
    int  (*write)(void *tls, const unsigned char *buf, size_t len,
                  size_t *done, int *want_read, int *want_write);

    /* Decrypted bytes already buffered inside the transport. */
    int  (*pending)(void *tls);

    /* Release the transport. The socket is owned by the bufferevent. */
    void (*free_ctx)(void *tls);
};

/*
 * Attach `tls` to `bev`. Afterwards all reads/writes on `bev` go through
 * `ops`, and bufferevent_getfd() returns -1 so that callers using the raw
 * fd (control.c writes directly to the socket in the QUIC path) fall back
 * to bufferevent_write() instead of bypassing the encryption.
 *
 * `bev` must not have been connected yet: the handshake starts once
 * select() reports the socket writable, and BEV_EVENT_CONNECTED is
 * delivered only after it succeeds.
 */
void mini_bev_set_ssl(struct bufferevent *bev, void *tls,
                      const struct mini_bev_ssl_ops *ops);

#ifdef __cplusplus
}
#endif

#endif /* MINI_EVENT_EXTRAS_H */
