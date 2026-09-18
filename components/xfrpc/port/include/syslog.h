// SPDX-License-Identifier: GPL-3.0-only
/*
 * ESP32 port: syslog.h compatibility header.
 *
 * ESP-IDF has no syslog daemon; levels are remapped onto the xfrpc debug
 * logger (src/debug.c) so debug(LOG_INFO, ...) keeps working. openlog /
 * vsyslog are no-ops (logging goes to stderr/UART via _debug()).
 */

#ifndef MINI_COMPAT_SYSLOG_H
#define MINI_COMPAT_SYSLOG_H

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LOG_EMERG   0
#define LOG_ALERT   1
#define LOG_CRIT    2
#define LOG_ERR     3
#define LOG_WARNING 4
#define LOG_NOTICE  5
#define LOG_INFO    6
#define LOG_DEBUG   7

#define LOG_PID     0x01
#define LOG_CONS    0x02
#define LOG_DAEMON  (3 << 3)

void openlog(const char *ident, int option, int facility);
void syslog(int priority, const char *format, ...);
void vsyslog(int priority, const char *format, va_list ap);
void closelog(void);

#ifdef __cplusplus
}
#endif

#endif /* MINI_COMPAT_SYSLOG_H */
