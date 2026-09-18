
// SPDX-License-Identifier: GPL-3.0-only
/*
 * Copyright (c) 2023 Dengfeng Liu <liudf0716@gmail.com>
 *
 * ESP32 port: rewritten for ESP-IDF logging (esp_log / UART).
 * The debug() macro interface in debug.h is unchanged.
 */

#include <stdio.h>
#include <stdarg.h>
#include <syslog.h>

#include <esp_log.h>

#include "sdkconfig.h"
#include "debug.h"

#define	PROGNAME	"xfrpc"

static const char *TAG = PROGNAME;

debugconf_t debugconf = {
    /* ESP32 port: verbosity comes from Kconfig; default INFO */
    .debuglevel = CONFIG_XFRPC_LOG_LEVEL,
    .log_stderr = 1,   /* kept for upstream struct compatibility */
    .log_syslog = 0,
    .syslog_facility = 0
};

static esp_log_level_t to_esp_level(int level)
{
    switch (level) {
    case LOG_EMERG:
    case LOG_ALERT:
    case LOG_CRIT:
    case LOG_ERR:
        return ESP_LOG_ERROR;
    case LOG_WARNING:
        return ESP_LOG_WARN;
    case LOG_NOTICE:
    case LOG_INFO:
        return ESP_LOG_INFO;
    default:
        return ESP_LOG_DEBUG;
    }
}

void _debug(const char *filename, int line, int level, const char *format, ...)
{
    if (level > debugconf.debuglevel) {
        return;
    }

    char buf[512];
    int n = snprintf(buf, sizeof(buf), "(%s:%d) ", filename, line);
    if (n < 0 || (size_t)n >= sizeof(buf))
        return;

    va_list vlist;
    va_start(vlist, format);
    vsnprintf(buf + n, sizeof(buf) - (size_t)n, format, vlist);
    va_end(vlist);

    esp_log_write(to_esp_level(level), TAG, "%s\n", buf);
}
