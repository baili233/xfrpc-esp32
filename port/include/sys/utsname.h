// SPDX-License-Identifier: GPL-3.0-only
/*
 * ESP32 port: sys/utsname.h compatibility header.
 *
 * login.c calls uname() to fill the frp login message (os/arch fields).
 * esp_platform.c provides the implementation.
 */

#ifndef MINI_COMPAT_SYS_UTSNAME_H
#define MINI_COMPAT_SYS_UTSNAME_H

#define _UTSNAME_LENGTH 32

struct utsname {
    char sysname[_UTSNAME_LENGTH];
    char nodename[_UTSNAME_LENGTH];
    char release[_UTSNAME_LENGTH];
    char version[_UTSNAME_LENGTH];
    char machine[_UTSNAME_LENGTH];
};

int uname(struct utsname *buf);

#endif /* MINI_COMPAT_SYS_UTSNAME_H */
