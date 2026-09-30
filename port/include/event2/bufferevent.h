// SPDX-License-Identifier: GPL-3.0-only
/*
 * ESP32 port: minimal libevent compatibility layer (event2/bufferevent.h).
 */

#ifndef MINI_EVENT2_BUFFEREVENT_H
#define MINI_EVENT2_BUFFEREVENT_H

#include <stddef.h>

#include "event2/event.h"
#include "event2/buffer.h"

#ifdef __cplusplus
extern "C" {
#endif

struct bufferevent;
struct sockaddr;
struct evdns_base;

/* Options */
#define BEV_OPT_CLOSE_ON_FREE   0x01
#define BEV_OPT_THREADSAFE      0x02  /* accepted, ignored (single task) */
#define BEV_OPT_DEFER_CALLBACKS 0x04  /* accepted, ignored (dispatched in loop) */
#define BEV_OPT_UNLOCK_CALLBACKS 0x08

/* Event flags for enable/disable */
#define BEV_EVENT_READING   0x01
#define BEV_EVENT_WRITING   0x02
#define BEV_EVENT_EOF       0x10
#define BEV_EVENT_ERROR     0x20
#define BEV_EVENT_TIMEOUT   0x40
#define BEV_EVENT_CONNECTED 0x80

/* Flush modes (accepted, ignored — writes are always flushed eagerly) */
#define BEV_NORMAL 0
#define BEV_FLUSH  1
#define BEV_FINISHED 2

typedef void (*bufferevent_data_cb)(struct bufferevent *bev, void *ctx);
typedef void (*bufferevent_event_cb)(struct bufferevent *bev, short what, void *ctx);

struct bufferevent *bufferevent_socket_new(struct event_base *base,
                                           evutil_socket_t fd, int options);
void bufferevent_free(struct bufferevent *);

void bufferevent_setcb(struct bufferevent *,
                       bufferevent_data_cb readcb, bufferevent_data_cb writecb,
                       bufferevent_event_cb eventcb, void *cbarg);
void bufferevent_getcb(struct bufferevent *,
                       bufferevent_data_cb *readcb, bufferevent_data_cb *writecb,
                       bufferevent_event_cb *eventcb, void **cbarg);

int  bufferevent_enable(struct bufferevent *, short events);
int  bufferevent_disable(struct bufferevent *, short events);
short bufferevent_get_enabled(struct bufferevent *);

int  bufferevent_write(struct bufferevent *, const void *data, size_t size);
int  bufferevent_write_buffer(struct bufferevent *, struct evbuffer *buf);
size_t bufferevent_read(struct bufferevent *, void *data, size_t size);
int  bufferevent_read_buffer(struct bufferevent *, struct evbuffer *buf);

struct evbuffer *bufferevent_get_input(struct bufferevent *);
struct evbuffer *bufferevent_get_output(struct bufferevent *);

evutil_socket_t bufferevent_getfd(struct bufferevent *);

int  bufferevent_socket_connect(struct bufferevent *,
                                const struct sockaddr *sa, int socklen);
/* dns_base is ignored (lwip getaddrinfo is used); family must be AF_INET. */
int  bufferevent_socket_connect_hostname(struct bufferevent *,
                                         struct evdns_base *dns_base,
                                         int family, const char *hostname, int port);

void bufferevent_set_timeouts(struct bufferevent *,
                              const struct timeval *tv_read,
                              const struct timeval *tv_write);
void bufferevent_setwatermark(struct bufferevent *, short events,
                              size_t lowmark, size_t highmark);

int  bufferevent_flush(struct bufferevent *, short iotype, short mode);

#ifdef __cplusplus
}
#endif

#endif /* MINI_EVENT2_BUFFEREVENT_H */
