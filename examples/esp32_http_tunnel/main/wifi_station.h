// SPDX-License-Identifier: GPL-3.0-only
/*
 * WiFi station bootstrap for the xfrpc-esp32 example app.
 */

#ifndef WIFI_STATION_H
#define WIFI_STATION_H

#include <stdbool.h>

/* NOTE: intentionally not named wifi_station_* — those symbols exist inside
 * esp_wifi's prebuilt libraries and collide at link time. */

/**
 * Initialize NVS + netif + WiFi driver and connect to the AP configured
 * via Kconfig (EXAMPLE_WIFI_SSID / EXAMPLE_WIFI_PASSWORD).
 *
 * Blocks until the station got an IP address or 30 s elapsed.
 *
 * @return 0 on success, -1 on timeout/failure.
 */
int app_wifi_connect(void);

/** True once the station has an IP address. */
bool app_wifi_is_connected(void);

#endif /* WIFI_STATION_H */
