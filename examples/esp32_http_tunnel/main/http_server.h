// SPDX-License-Identifier: GPL-3.0-only
/*
 * Minimal HTTP server used as the local service behind the xfrpc proxy.
 * Serves one static page for any request — good enough to verify the
 * frps -> ESP32 -> local service data path end to end with a browser.
 */

#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include <stdint.h>

/**
 * Start the HTTP server on its own FreeRTOS task.
 *
 * @param port local TCP port to listen on
 * @return 0 on success, -1 on failure.
 */
int http_server_start(uint16_t port);

#endif /* HTTP_SERVER_H */
