
// SPDX-License-Identifier: GPL-3.0-only
/*
 * Copyright (c) 2023 Dengfeng Liu <liudf0716@gmail.com>
 *
 * ESP32 port: Linux net/if.h / sys/ioctl.h / ifaddrs.h code replaced with
 * esp_netif / esp_read_mac based implementations.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
// ESP32 port: lwip's arpa/inet.h does not pull in the socket headers
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// ESP32 port: replaced Linux net/if.h, sys/ioctl.h, ifaddrs.h
#include <esp_mac.h>
#include <esp_netif.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "utils.h"

/**
 * High precision sleep function
 *
 * ESP32 port: lwip select() with no fds is unreliable — use vTaskDelay.
 *
 * @param s Number of seconds to sleep
 * @param u Number of microseconds to sleep (1 second = 1,000,000 microseconds)
 */
void s_sleep(unsigned int s, unsigned int u)
{
	uint64_t ms = (uint64_t)s * 1000U + (u + 999U) / 1000U;
	if (ms == 0)
		ms = 1;
	vTaskDelay(pdMS_TO_TICKS((uint32_t)ms));
}

/**
 * Validates IPv4 address string format
 *
 * This function checks if the given string represents a valid IPv4 address
 * in dotted decimal notation (e.g., "192.168.1.1").
 *
 * @param ip_address String containing the IP address to validate
 * @return 1 if address is valid, 0 if invalid
 */
int is_valid_ip_address(const char *ip_address)
{
	if (!ip_address) {
		return 0;
	}

	struct sockaddr_in sa;
	return inet_pton(AF_INET, ip_address, &(sa.sin_addr));
}

/**
 * Gets the MAC address of the WiFi station interface
 *
 * ESP32 port: uses the eFuse base MAC (esp_read_mac); the interface name
 * argument is accepted for API compatibility but ignored.
 *
 * @param net_if_name Name of network interface (ignored)
 * @param mac Output buffer to store MAC address string
 * @param mac_len Length of output buffer (must be >= 13 bytes for MAC XXYYZZAABBCC)
 * @return 0 on success, 1 on error (invalid parameters or system calls failed)
 */
int get_net_mac(const char *net_if_name, char *mac, int mac_len)
{
	uint8_t base_mac[6] = {0};

	// Validate input parameters: 12 hex chars + 1 null terminator = 13 bytes minimum
	if (!mac || mac_len < 13) {
		return 1;
	}

	(void)net_if_name;

	if (esp_read_mac(base_mac, ESP_MAC_WIFI_STA) != ESP_OK) {
		return 1;
	}

	// Format MAC address as string
	for (int i = 0; i < 6; i++) {
		snprintf(mac + (i * 2), mac_len - (i * 2), "%02X", base_mac[i]);
	}
	return 0;
}

/**
 * Displays information about the network interface
 *
 * ESP32 port: prints the station interface IP if available.
 *
 * @return Number of interfaces found, or -1 on error
 */
int show_net_ifname()
{
	esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
	if (!netif) {
		printf("no STA netif\n");
		return -1;
	}

	char ip[16] = {0};
	esp_netif_ip_info_t ip_info;
	if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
		esp_ip4addr_ntoa(&ip_info.ip, ip, sizeof(ip));
	}
	printf("sta (AF_INET)\n\t\taddress: <%s>\n", ip);
	return 1;
}

/**
 * Gets the primary network interface name of the system
 *
 * ESP32 port: always returns "sta" (the WiFi station netif).
 *
 * @param if_buf Output buffer to store interface name
 * @param blen Length of output buffer (must be >= 8 bytes)
 * @return 0 on success, -1 on invalid parameters
 */
int get_net_ifname(char *if_buf, int blen)
{
	// Validate input parameters
	if (if_buf == NULL || blen < 8) {
		return -1;
	}

	strncpy(if_buf, "sta", blen - 1);
	if_buf[blen - 1] = '\0';
	return 0;
}

/**
 * Converts domain name to lowercase and validates format
 *
 * This function takes a domain name string and:
 * 1. Converts all characters to lowercase until '/' is encountered
 * 2. Validates that the domain has at least one dot (.)
 * 3. Copies the result to the output buffer
 *
 * Example: wWw.Baidu.com/China -> www.baidu.com/China
 *
 * @param dname Input domain name string
 * @param udname_buf Output buffer for unified domain name
 * @param udname_buf_len Length of output buffer
 * @return 0 on success, 1 on failure (invalid domain or buffer too small)
 */
int dns_unified(const char *dname, char *udname_buf, int udname_buf_len)
{
	// Validate input parameters
	if (!dname || !udname_buf || udname_buf_len < strlen(dname) + 1) {
		return 1;
	}

	const int dlen = strlen(dname);
	bool has_dot = false;

	// Process each character until '/' or end of string.
	// The two return points are intentional: when a '/' is found we must
	// null-terminate at that position (stripping the path component), whereas
	// if no '/' is present we null-terminate at the full string length below.
	for (int i = 0; i < dlen; i++) {
		if (dname[i] == '/') {
			udname_buf[i] = '\0';
			// Domain must contain at least one dot
			return has_dot ? 0 : 1;
		}

		if (dname[i] == '.' && i != dlen - 1) {
			has_dot = true;
		}

		udname_buf[i] = tolower(dname[i]);
	}

	// Ensure the output is always null-terminated when no '/' was found
	udname_buf[dlen] = '\0';

	// Domain must contain at least one dot
	return has_dot ? 0 : 1;
}
