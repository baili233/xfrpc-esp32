// SPDX-License-Identifier: GPL-3.0-only
/*
 * ESP32 port: minimal event2/bufferevent_ssl.h — tls.h includes it, but the
 * SSL bufferevent API itself is only needed by tls.c (not compiled; see
 * port/module_stubs.c).
 */

#ifndef MINI_EVENT2_BUFFEREVENT_SSL_H
#define MINI_EVENT2_BUFFEREVENT_SSL_H

#include <event2/bufferevent.h>

/* bufferevent_openssl_filter_new() and friends are not provided by
 * mini_event; nothing in the compiled sources calls them. */

#endif /* MINI_EVENT2_BUFFEREVENT_SSL_H */
