// SPDX-License-Identifier: GPL-3.0-only
/*
 * ESP32 port: public API implementation (include/xfrpc.h).
 *
 * xfrpc_start() copies the caller's configuration into the xfrpc common
 * config / proxy service structures and launches xfrpc_loop() on a
 * dedicated FreeRTOS task.
 */

#include <string.h>
#include <stdlib.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "sdkconfig.h"

#include "xfrpc.h"
#include "xfrpc_events.h"
#include "debug.h"
#include "config.h"
#include "control.h"
#include "mini_event.h"

/* from src/xfrpc.c (its own xfrpc.h is shadowed by the public header) */
extern void xfrpc_loop(void);
/* upstream main() calls init_login() before xfrpc_loop(); it initializes the
 * global login struct (version/os/arch/run_id) used by login_request_marshal */
extern void init_login(void);

/* from port/esp_platform.c */
extern int  xfrpc_is_fatal(void);
extern void xfrpc_fatal_reset(void);

static struct {
    TaskHandle_t     task;
    volatile bool    running;
    xfrpc_event_cb_t cb;
    void            *user;
} g_api;

/**
 * Called by the core (control.c) on state transitions; forwards to the
 * user callback registered in xfrpc_start().
 */
void xfrpc_report_state(xfrpc_state_t state)
{
    if (g_api.cb)
        g_api.cb(state, g_api.user);
}

static void xfrpc_task(void *arg)
{
    (void)arg;

    debug(LOG_INFO, "xfrpc task started");
    xfrpc_report_state(XFRPC_STATE_CONNECTING);

    init_login();   /* upstream main(): init_login() before xfrpc_loop() */
    xfrpc_loop();

    xfrpc_state_t final = xfrpc_is_fatal() ? XFRPC_STATE_FATAL : XFRPC_STATE_STOPPED;
    debug(LOG_INFO, "xfrpc task exiting (state %d)", (int)final);
    xfrpc_report_state(final);

    g_api.running = false;
    g_api.task = NULL;
    vTaskDelete(NULL);
}

static void set_str(char **field, const char *value)
{
    if (!value)
        return;
    free(*field);
    *field = strdup(value);
}

int xfrpc_start(const xfrpc_client_config_t *cfg,
                const xfrpc_tcp_proxy_t *proxies, int proxy_count,
                xfrpc_event_cb_t cb, void *user)
{
    if (!cfg || !cfg->server_addr || !cfg->server_addr[0]) {
        debug(LOG_ERR, "xfrpc_start: server_addr is required");
        return -1;
    }
    if (proxy_count < 0 || (proxy_count > 0 && !proxies)) {
        debug(LOG_ERR, "xfrpc_start: invalid proxy list");
        return -1;
    }
    if (g_api.running) {
        debug(LOG_ERR, "xfrpc_start: already running");
        return 1;
    }

    /* ---- common config ---- */
    struct common_conf *cc = init_common_config();
    if (!cc)
        return -1;

    set_str(&cc->server_addr, cfg->server_addr);
    if (cfg->server_port)
        cc->server_port = cfg->server_port;
    set_str(&cc->auth_token, cfg->auth_token);
    set_str(&cc->user, cfg->user);
    if (cfg->heartbeat_interval > 0)
        cc->heartbeat_interval = cfg->heartbeat_interval;
    if (cfg->heartbeat_timeout > 0)
        cc->heartbeat_timeout = cfg->heartbeat_timeout;
    if (cfg->tcp_mux == 0 || cfg->tcp_mux == 1)
        cc->tcp_mux = cfg->tcp_mux;

    if (!validate_heartbeat_config()) {
        return -1;
    }

    debug(LOG_INFO, "xfrpc config: server=%s:%d tcp_mux=%d user=%s heartbeat=%d/%d",
          cc->server_addr, cc->server_port, cc->tcp_mux,
          cc->user ? cc->user : "-", cc->heartbeat_interval, cc->heartbeat_timeout);

    /* ---- proxies ---- */
    for (int i = 0; i < proxy_count; i++) {
        const xfrpc_tcp_proxy_t *p = &proxies[i];
        if (!p->name || !p->local_ip || !p->local_port) {
            debug(LOG_ERR, "xfrpc_start: proxy %d missing name/local_ip/local_port", i);
            return -1;
        }
        struct proxy_service *ps = config_add_proxy_service(
            p->name, "tcp", p->local_ip, p->local_port, p->remote_port,
            p->use_encryption, p->use_compression);
        if (!ps) {
            debug(LOG_ERR, "xfrpc_start: failed to add proxy '%s'", p->name);
            return -1;
        }
        debug(LOG_INFO, "xfrpc proxy [%s]: %s:%d -> remote %d (enc=%d comp=%d)",
              p->name, p->local_ip, p->local_port, p->remote_port,
              p->use_encryption, p->use_compression);
    }

    /* ---- launch the event loop task ---- */
    g_api.cb = cb;
    g_api.user = user;
    g_api.running = true;

#if !defined(CONFIG_XFRPC_TASK_STACK_SIZE) || CONFIG_XFRPC_TASK_STACK_SIZE < 8192
#error "CONFIG_XFRPC_TASK_STACK_SIZE must be at least 8192"
#endif

    if (xTaskCreate(xfrpc_task, "xfrpc",
                    CONFIG_XFRPC_TASK_STACK_SIZE, NULL,
                    CONFIG_XFRPC_TASK_PRIORITY, &g_api.task) != pdPASS) {
        debug(LOG_ERR, "xfrpc_start: failed to create task");
        g_api.running = false;
        return -1;
    }

    return 0;
}

void xfrpc_stop(void)
{
    struct control *ctl = get_main_control();
    if (ctl && ctl->connect_base) {
        debug(LOG_INFO, "xfrpc_stop requested");
        mini_event_base_stop(ctl->connect_base);
    } else if (g_api.running) {
        /* task not inside the loop yet — mark so it stops after startup */
        debug(LOG_WARNING, "xfrpc_stop: event loop not running yet");
    }
}
