// SPDX-License-Identifier: GPL-3.0-only
/*
 * ESP32 port: minimal libevent compatibility layer (event2/listener.h).
 *
 * Stub: evconnlistener is only needed by the (optional, not ported)
 * visitor and tcp_redir features. Provided so common.h compiles.
 */

#ifndef MINI_EVENT2_LISTENER_H
#define MINI_EVENT2_LISTENER_H

#define LEV_OPT_REUSEABLE  0x01
#define LEV_OPT_CLOSE_ON_FREE 0x02

struct evconnlistener;
struct event_base;
struct sockaddr;

typedef void (*evconnlistener_cb)(struct evconnlistener *, int fd,
                                  struct sockaddr *, int socklen, void *);

#endif /* MINI_EVENT2_LISTENER_H */
