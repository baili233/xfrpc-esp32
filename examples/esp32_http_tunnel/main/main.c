// SPDX-License-Identifier: GPL-3.0-only
/*
 * xfrpc-esp32 example app.
 *
 * Boot sequence:
 *   1. connect WiFi (station)
 *   2. start a local HTTP server serving a test page (proxy target)
 *   3. build an INI config from Kconfig and start xfrpc from that string
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

    char config[512];
    int config_len = snprintf(
        config, sizeof(config),
        "[common]\n"
        "user = %s\n"
        "token = %s\n"
        "\n"
        "tls_enable = false\n"
        "disable_custom_tls_first_byte = false\n"
        "\n"
        "server_addr = %s\n"
        "server_port = %d\n"
        "\n"
        "[%s]\n"
        "type = tcp\n"
        "local_ip = 127.0.0.1\n"
        "local_port = %d\n"
        "remote_port = %d\n",
        CONFIG_EXAMPLE_FRPS_USER,
        CONFIG_EXAMPLE_FRPS_AUTH_TOKEN,
        CONFIG_EXAMPLE_FRPS_SERVER_ADDR,
        CONFIG_EXAMPLE_FRPS_SERVER_PORT,
        CONFIG_EXAMPLE_PROXY_NAME,
        CONFIG_EXAMPLE_HTTP_SERVER_PORT,
        CONFIG_EXAMPLE_PROXY_REMOTE_PORT);
    if (config_len < 0 || config_len >= (int)sizeof(config)) {
        ESP_LOGE(TAG, "xfrpc config buffer too small");
        return;
    }

    ESP_LOGI(TAG, "xfrpc: %s:%d user=%s proxy=%s -> remote %d",
             CONFIG_EXAMPLE_FRPS_SERVER_ADDR, CONFIG_EXAMPLE_FRPS_SERVER_PORT,
             CONFIG_EXAMPLE_FRPS_USER[0] ? CONFIG_EXAMPLE_FRPS_USER : "-",
             CONFIG_EXAMPLE_PROXY_NAME,
             CONFIG_EXAMPLE_PROXY_REMOTE_PORT);

    int rc = xfrpc_start_from_string(config, (size_t)config_len,
                                     XFRPC_CONFIG_FORMAT_INI,
                                     on_xfrpc_state, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "xfrpc_start_from_string failed (%d)", rc);
    }
}
