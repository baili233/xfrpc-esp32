// SPDX-License-Identifier: GPL-3.0-only
/*
 * Minimal HTTP server used as the local service behind the xfrpc proxy.
 * Serves one static page for any request — enough to verify the
 * frps -> ESP32 -> local service data path end to end with a browser.
 */

#include <stdio.h>
#include <string.h>
#include <lwip/sockets.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>

#include "http_server.h"

#define HTTP_STACK_SIZE 4096
#define HTTP_REQ_SIZE   1024
#define HTTP_BACKLOG    4

static const char *TAG = "httpd";

static const char PAGE[] =
    "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>ESP32 frpc</title>"
    "<style>body{font-family:sans-serif;margin:3em auto;max-width:32em;"
    "color:#333}h1{color:#0a7}code{background:#f4f4f4;padding:2px 6px}</style>"
    "</head><body>"
    "<h1>Hello from ESP32-S3 &#128075;</h1>"
    "<p>This page is served by the ESP32 through "
    "<code>xfrpc &rarr; frps</code>.</p>"
    "<p>If you can read this, the tunnel works.</p>"
    "</body></html>";

static int send_all(int fd, const char *buf, int len)
{
    int sent = 0;
    while (sent < len) {
        int w = send(fd, buf + sent, len - sent, 0);
        if (w <= 0) {
            ESP_LOGW(TAG, "send failed");
            return -1;
        }
        sent += w;
    }
    return 0;
}

static void http_session(int fd)
{
    char req[HTTP_REQ_SIZE];
    int total = 0;

    /* read request headers (stop at blank line, buffer full, or peer close) */
    while (total < (int)sizeof(req) - 1) {
        int n = recv(fd, req + total, sizeof(req) - 1 - total, 0);
        if (n <= 0) {
            close(fd);
            return;
        }
        total += n;
        req[total] = '\0';
        if (strstr(req, "\r\n\r\n")) {
            break;
        }
    }

    /* log the request line */
    char *eol = strpbrk(req, "\r\n");
    ESP_LOGI(TAG, "request: %.*s", eol ? (int)(eol - req) : total, req);

    char hdr[160];
    int hlen = snprintf(hdr, sizeof(hdr),
                        "HTTP/1.1 200 OK\r\n"
                        "Content-Type: text/html; charset=utf-8\r\n"
                        "Content-Length: %d\r\n"
                        "Connection: close\r\n"
                        "\r\n",
                        (int)(sizeof(PAGE) - 1));

    if (send_all(fd, hdr, hlen) == 0) {
        send_all(fd, PAGE, sizeof(PAGE) - 1);
    }
    close(fd);
}

static void http_server_task(void *arg)
{
    uint16_t port = (uint16_t)(uintptr_t)arg;

    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd < 0) {
        ESP_LOGE(TAG, "socket failed");
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_port = htons(port),
    };
    int opt = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    if (bind(lfd, (struct sockaddr *)&addr, sizeof(addr)) < 0 ||
        listen(lfd, HTTP_BACKLOG) < 0) {
        ESP_LOGE(TAG, "bind/listen on port %u failed", port);
        close(lfd);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "HTTP server listening on port %u", port);

    for (;;) {
        struct sockaddr_in cli;
        socklen_t clen = sizeof(cli);
        int cfd = accept(lfd, (struct sockaddr *)&cli, &clen);
        if (cfd < 0) {
            ESP_LOGW(TAG, "accept failed");
            continue;
        }
        ESP_LOGI(TAG, "connection from %s:%u",
                 inet_ntoa(cli.sin_addr), ntohs(cli.sin_port));
        http_session(cfd);
    }
}

int http_server_start(uint16_t port)
{
    if (xTaskCreate(http_server_task, "httpd", HTTP_STACK_SIZE,
                    (void *)(uintptr_t)port, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "failed to create httpd task");
        return -1;
    }
    return 0;
}
