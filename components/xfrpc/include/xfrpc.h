// SPDX-License-Identifier: GPL-3.0-only
/*
 * ESP32 port: public API of the xfrpc ESP-IDF component.
 *
 * Configuration is passed programmatically (no config file parsing).
 *
 * Basic usage:
 *
 *   xfrpc_client_config_t cfg = {
 *       .server_addr = "192.168.1.10",
 *       .server_port = 7000,
 *       .auth_token  = "secret",
 *   };
 *   xfrpc_tcp_proxy_t proxies[] = {
 *       { .name = "web", .local_ip = "127.0.0.1",
 *         .local_port = 8080, .remote_port = 6000 },
 *   };
 *   xfrpc_start(&cfg, proxies, 1, on_state, NULL);
 */

#ifndef XFRPC_ESP32_PUBLIC_API_H
#define XFRPC_ESP32_PUBLIC_API_H

#include <stdbool.h>
#include <stdint.h>

#include "xfrpc_events.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Client configuration. String fields are copied by xfrpc_start(). */
typedef struct {
    const char *server_addr;        /* frps address (required) */
    uint16_t    server_port;        /* frps port, 0 = 7000 */
    const char *auth_token;         /* token auth (optional, may be NULL) */
    const char *user;               /* user name (optional) */
    int         heartbeat_interval; /* seconds, 0 = 30 */
    int         heartbeat_timeout;  /* seconds, 0 = 90 */
    int         tcp_mux;            /* 0/1, -1 = default (1, matches frps) */
} xfrpc_client_config_t;

/** TCP proxy definition. String fields are copied by xfrpc_start(). */
typedef struct {
    const char *name;             /* proxy name (required, unique) */
    const char *local_ip;         /* local service address (required) */
    uint16_t    local_port;       /* local service port (required) */
    uint16_t    remote_port;      /* port exposed by frps (0 = allocate) */
    int         use_encryption;   /* AES-128-CFB stream encryption */
    int         use_compression;  /* snappy compression */
} xfrpc_tcp_proxy_t;

/** State transition callback (invoked on the xfrpc event task). */
typedef void (*xfrpc_event_cb_t)(xfrpc_state_t state, void *user);

/**
 * @brief Start the xfrpc client on a dedicated FreeRTOS task.
 *
 * @param cfg         client configuration (required)
 * @param proxies     TCP proxy list (may be NULL if proxy_count == 0)
 * @param proxy_count number of entries in proxies
 * @param cb          optional state transition callback
 * @param user        opaque pointer passed back to cb
 *
 * @return 0 on success, -1 on invalid configuration, 1 if already running
 */
int  xfrpc_start(const xfrpc_client_config_t *cfg,
                 const xfrpc_tcp_proxy_t *proxies, int proxy_count,
                 xfrpc_event_cb_t cb, void *user);

/**
 * @brief Request a clean stop; the event task exits asynchronously.
 *
 * Safe to call from any task.
 */
void xfrpc_stop(void);

/** @brief true while the control connection to frps is up and logged in. */
bool xfrpc_is_connected(void);

#ifdef __cplusplus
}
#endif

#endif /* XFRPC_ESP32_PUBLIC_API_H */
