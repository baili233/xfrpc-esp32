// SPDX-License-Identifier: GPL-3.0-only
/*
 * ESP32 port: minimal libevent compatibility layer (event2/util.h).
 */

#ifndef MINI_EVENT2_UTIL_H
#define MINI_EVENT2_UTIL_H

#include <stddef.h>
#include <errno.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define evutil_socket_t int

#define evutil_socket_geterror(sock)  (errno)
#define evutil_socket_error_to_string(e) (strerror(e))

int  evutil_make_socket_nonblocking(int fd);
int  evutil_closesocket(int fd);
int  evutil_socketpair(int d, int type, int protocol, int sv[2]); /* stub: -1 */

#ifdef __cplusplus
}
#endif

#endif /* MINI_EVENT2_UTIL_H */
