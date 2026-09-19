// SPDX-License-Identifier: GPL-3.0-only
/*
 * ESP32 port: minimal event2/bufferevent_ssl.h.
 *
 * src/tls.h includes this header (upstream used libevent's OpenSSL
 * bufferevent filter). The TLS transport here is an in-bufferevent backend
 * instead — see mini_bev_set_ssl() in port/include/mini_event.h and its
 * mbedtls implementation in port/mini_event_ssl.c.
 */

#ifndef MINI_EVENT2_BUFFEREVENT_SSL_H
#define MINI_EVENT2_BUFFEREVENT_SSL_H

#include <event2/bufferevent.h>

/* bufferevent_openssl_filter_new() and friends are not provided by
 * mini_event; nothing in the compiled sources calls them. */

#endif /* MINI_EVENT2_BUFFEREVENT_SSL_H */
