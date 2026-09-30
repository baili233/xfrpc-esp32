
// SPDX-License-Identifier: GPL-3.0-only
/*
 * Copyright (c) 2023 Dengfeng Liu <liudf0716@gmail.com>
 */

#include "uthash.h"
#include "common.h"

/* ESP32 port note: glibc-style __BYTE_ORDER/__BIG_ENDIAN are NOT defined by
 * ESP-IDF's newlib headers, and "#if UNDEF1 == UNDEF2" silently evaluates to
 * "#if 0 == 0" (true), which made hton64/ntoh64 identity functions on the
 * little-endian ESP32 and corrupted the frp v1 message length field
 * (frps: "message length error").  Use the compiler-predefined macros
 * instead — always available, no header dependency. */
#if defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__) && \
    (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
#define XFRPC_BIG_ENDIAN 1
#endif

uint64_t ntoh64(const uint64_t input)
{
#ifdef XFRPC_BIG_ENDIAN
    return input;
#else
    return ((uint64_t)ntohl(input & 0xFFFFFFFF) << 32) |
           ntohl((input >> 32) & 0xFFFFFFFF);
#endif
}

uint64_t hton64(const uint64_t input)
{
#ifdef XFRPC_BIG_ENDIAN
    return input;
#else
    return ((uint64_t)htonl(input & 0xFFFFFFFF) << 32) |
           htonl((input >> 32) & 0xFFFFFFFF);
#endif
}
