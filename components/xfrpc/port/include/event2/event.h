// SPDX-License-Identifier: GPL-3.0-only
/*
 * ESP32 port: minimal libevent compatibility layer (event2/event.h).
 *
 * Implements just the API surface xfrpc uses, on top of lwip sockets
 * (select) and a FreeRTOS-hosted event loop. See port/mini_event.c.
 */

#ifndef MINI_EVENT2_EVENT_H
#define MINI_EVENT2_EVENT_H

#include <sys/time.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Event flags */
#define EV_TIMEOUT  0x01
#define EV_READ     0x02
#define EV_WRITE    0x04
#define EV_SIGNAL   0x08
#define EV_PERSIST  0x10

typedef int evutil_socket_t;

struct event_base;
struct event;

typedef void (*event_callback_fn)(evutil_socket_t fd, short what, void *arg);

struct event_base *event_base_new(void);
void event_base_free(struct event_base *);
int  event_base_dispatch(struct event_base *);
int  event_base_loopbreak(struct event_base *);
const char *event_base_get_method(const struct event_base *);

struct event *event_new(struct event_base *, evutil_socket_t,
                        short, event_callback_fn, void *);
int  event_add(struct event *, const struct timeval *);
int  event_del(struct event *);
void event_free(struct event *);
int  event_get_events(const struct event *); /* pending flags (debug aid) */

/* Timer convenience macros (libevent-compatible) */
#define evtimer_new(base, cb, arg) \
        event_new((base), -1, 0, (cb), (arg))
#define evtimer_add(ev, tv)        event_add((ev), (tv))
#define evtimer_del(ev)            event_del((ev))

#ifdef __cplusplus
}
#endif

#endif /* MINI_EVENT2_EVENT_H */
