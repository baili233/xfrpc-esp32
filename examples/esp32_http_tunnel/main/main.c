// SPDX-License-Identifier: GPL-3.0-only
/*
 * xfrpc-esp32 example app.
 *
 * Boot sequence:
 *   1. connect WiFi (station)
 *   2. start a local HTTP server serving a test page (proxy target)
 *   3. start the xfrpc component against the frps server from Kconfig
 */

#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include <esp_log.h>
#include <esp_netif_sntp.h>

#include "wifi_station.h"
#include "http_server.h"
#include "xfrpc.h"

static const char *TAG = "app_main";

static void on_xfrpc_state(xfrpc_state_t state, void *user)
{
    (void)user;
    switch (state) {
        case XFRPC_STATE_CONNECTING:   ESP_LOGI(TAG, "xfrpc: connecting to frps"); break;
        case XFRPC_STATE_CONNECTED:    ESP_LOGI(TAG, "xfrpc: connected"); break;
        case XFRPC_STATE_LOGIN_OK:     ESP_LOGI(TAG, "xfrpc: login OK"); break;
        case XFRPC_STATE_RECONNECTING: ESP_LOGW(TAG, "xfrpc: connection lost, reconnecting"); break;
        case XFRPC_STATE_FATAL:        ESP_LOGE(TAG, "xfrpc: fatal error, client stopped"); break;
        case XFRPC_STATE_STOPPED:      ESP_LOGI(TAG, "xfrpc: stopped"); break;
        default: break;
    }
}

/* The frp token auth key is MD5(token + unix timestamp): without a synced
 * clock the ESP32 would sign with a 1970 timestamp and the server would
 * reject the login. */
static int sync_time_from_ntp(void)
{
    esp_sntp_config_t sntp_cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_EXAMPLE_SNTP_SERVER);
    esp_netif_sntp_init(&sntp_cfg);
    esp_err_t err = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(15000));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SNTP sync failed (0x%x); login may be rejected", err);
        return -1;
    }

    time_t now = time(NULL);
    ESP_LOGI(TAG, "time synced: %lld", (long long)now);
    return 0;
}

void app_main(void)
{
    ESP_LOGI(TAG, "xfrpc-esp32 example starting");

    if (app_wifi_connect() != 0) {
        ESP_LOGE(TAG, "wifi connect failed; rebooting in 10 s");
        return;
    }

    http_server_start(CONFIG_EXAMPLE_HTTP_SERVER_PORT);

    /* MD5(token+timestamp) auth needs a real clock (see sync_time_from_ntp). */
    sync_time_from_ntp();

    xfrpc_client_config_t cfg = {
        .server_addr = CONFIG_EXAMPLE_FRPS_SERVER_ADDR,
        .server_port = CONFIG_EXAMPLE_FRPS_SERVER_PORT,
        .auth_token  = CONFIG_EXAMPLE_FRPS_AUTH_TOKEN,
        .user        = CONFIG_EXAMPLE_FRPS_USER,
        .tcp_mux     = 1,   /* match frps default */
    };
    xfrpc_tcp_proxy_t proxies[] = {
        {
            .name            = CONFIG_EXAMPLE_PROXY_NAME,
            .local_ip        = "127.0.0.1",
            .local_port      = CONFIG_EXAMPLE_HTTP_SERVER_PORT,
            .remote_port     = CONFIG_EXAMPLE_PROXY_REMOTE_PORT,
            .use_encryption  = 0,
            .use_compression = 0,
        },
    };

    ESP_LOGI(TAG, "xfrpc: %s:%d user=%s proxy=%s -> remote %d",
             cfg.server_addr, cfg.server_port,
             cfg.user[0] ? cfg.user : "-",
             proxies[0].name, proxies[0].remote_port);

    int rc = xfrpc_start(&cfg, proxies, 1, on_xfrpc_state, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "xfrpc_start failed (%d)", rc);
    }
}
