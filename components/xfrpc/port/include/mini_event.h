// SPDX-License-Identifier: GPL-3.0-only
/*
 * ESP32 port: public extras of the mini-libevent implementation
 * (port/mini_event.c) that go beyond the libevent API.
 */

#ifndef MINI_EVENT_EXTRAS_H
#define MINI_EVENT_EXTRAS_H

#include "event2/event.h"

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

#ifdef __cplusplus
}
#endif

#endif /* MINI_EVENT_EXTRAS_H */
