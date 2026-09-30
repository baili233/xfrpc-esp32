// SPDX-License-Identifier: GPL-3.0-only
/*
 * ESP32 port: minimal libevent compatibility layer (event2/dns.h).
 *
 * Stub only: lwip's DNS resolver (configured by esp_netif) is used by
 * bufferevent_socket_connect_hostname() directly via getaddrinfo().
 */

#ifndef MINI_EVENT2_DNS_H
#define MINI_EVENT2_DNS_H

struct event_base;

#define DNS_OPTION_NAMESERVERS 0x01
#define DNS_OPTION_HOSTSFILE   0x02

struct evdns_base *evdns_base_new(struct event_base *base, int initialize);
void evdns_base_free(struct evdns_base *base, int fail_requests);
int  evdns_base_count_nameservers(struct evdns_base *base);
int  evdns_base_nameserver_ip_add(struct evdns_base *base, const char *ip);
int  evdns_base_set_option(struct evdns_base *base, const char *option, const char *val);
int  evdns_base_resolv_conf_parse(struct evdns_base *base, int flags, const char *filename);

#endif /* MINI_EVENT2_DNS_H */
