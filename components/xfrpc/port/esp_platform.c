// SPDX-License-Identifier: GPL-3.0-only
/*
 * ESP32 port: platform glue for Linux-specific code paths in xfrpc.
 *
 *  - uname(): frp login message os/arch fields
 *  - xfrpc_fatal(): log + stop event loop (replaces upstream exit(1))
 *  - commandline.h stubs: no argv / config file / SIGHUP on ESP32
 *  - syslog.h no-ops: logging goes through debug.c → esp_log
 */

#include <stdio.h>
#include <string.h>

#include <esp_system.h>
#include <esp_idf_version.h>
#include <esp_mac.h>

#include "sdkconfig.h"

#include "sys/utsname.h"
#include "syslog.h"

#include "debug.h"
#include "control.h"
#include "commandline.h"
#include "event2/event.h"
#include "mini_event.h"

/* ------------------------------------------------------------------ */
/* uname                                                               */
/* ------------------------------------------------------------------ */

int uname(struct utsname *buf)
{
    if (!buf)
        return -1;

    /* os / arch end up in the frp login message */
    strncpy(buf->sysname,  "FreeRTOS", _UTSNAME_LENGTH - 1);
    strncpy(buf->nodename, "esp32",    _UTSNAME_LENGTH - 1);
    strncpy(buf->release,  esp_get_idf_version(), _UTSNAME_LENGTH - 1);
    strncpy(buf->version,  "1.0",      _UTSNAME_LENGTH - 1);
#ifdef CONFIG_IDF_TARGET
    strncpy(buf->machine, CONFIG_IDF_TARGET, _UTSNAME_LENGTH - 1);
#else
    strncpy(buf->machine, "esp32",     _UTSNAME_LENGTH - 1);
#endif
    return 0;
}

/* ------------------------------------------------------------------ */
/* fatal error handling                                                */
/* ------------------------------------------------------------------ */

/* read by the API layer (port/xfrpc_api.c) to report XFRPC_STATE_FATAL */
static int g_fatal;
int xfrpc_is_fatal(void)
{
    return g_fatal;
}

void xfrpc_fatal_reset(void)
{
    g_fatal = 0;
}

void xfrpc_fatal(const char *msg)
{
    g_fatal = 1;
    debug(LOG_ERR, "xfrpc fatal: %s", msg ? msg : "(no message)");

    /* stop the event loop so run_control() returns and the task exits */
    struct control *ctl = get_main_control();
    if (ctl && ctl->connect_base)
        mini_event_base_stop(ctl->connect_base);
}

/* ------------------------------------------------------------------ */
/* commandline stubs (no argv / config file on ESP32)                  */
/* ------------------------------------------------------------------ */

void parse_commandline(int argc, char **argv)
{
    (void)argc;
    (void)argv;
}

int get_daemon_status(void)
{
    return 0;
}

const char *get_config_file(void)
{
    return NULL; /* API-only configuration: no config file */
}

int check_reload_flag(void)
{
    return 0; /* no SIGHUP on ESP32 */
}

void clear_reload_flag(void)
{
}

/* ------------------------------------------------------------------ */
/* syslog no-ops (logging goes through debug.c → esp_log)              */
/* ------------------------------------------------------------------ */

void openlog(const char *ident, int option, int facility)
{
    (void)ident; (void)option; (void)facility;
}

void syslog(int priority, const char *format, ...)
{
    (void)priority; (void)format;
}

void vsyslog(int priority, const char *format, va_list ap)
{
    (void)priority; (void)format; (void)ap;
}

void closelog(void)
{
}
