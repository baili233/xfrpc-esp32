
// SPDX-License-Identifier: GPL-3.0-only
/*
 * Copyright (c) 2023 Dengfeng Liu <liudf0716@gmail.com>
 */

#ifndef XFRPC_CONFIG_H
#define XFRPC_CONFIG_H

#include "client.h"
#include "common.h"

// Default port definitions
// Remote Desktop ports
#define DEFAULT_MSTSC_PORT                    3389

// Proxy ports
#define DEFAULT_SOCKS5_PORT                   1980

// Plugin service ports
#define XFRPC_PLUGIN_TELNETD_PORT            23
#define XFRPC_PLUGIN_HTTPD_PORT              8000
#define XFRPC_PLUGIN_HTTPD_REMOTE_PORT       8001
#define XFRPC_PLUGIN_INSTALOADER_PORT        10000
#define XFRPC_PLUGIN_INSTALOADER_REMOTE_PORT 10001
#define XFRPC_PLUGIN_YOUTUBEDL_PORT          20002
#define XFRPC_PLUGIN_YOUTUBEDL_REMOTE_PORT   20003

/**
 * Common configuration structure for the client
 */
struct common_conf {
	/* Server settings */
	char    *server_addr;          /* default 127.0.0.1 */
	int     server_port;           /* default 7000 */
	char    *auth_token;

	/* OIDC settings */
	char    *auth_method;          /* "token" (default) or "oidc" */
	char    *oidc_client_id;
	char    *oidc_client_secret;
	char    *oidc_audience;
	char    *oidc_scope;
	char    *oidc_token_endpoint_url;
	char    *oidc_trusted_ca_file;
	int     oidc_insecure_skip_verify;
	char    *oidc_proxy_url;

	/* Connection settings */
	int     heartbeat_interval;    /* default 10 */
	int     heartbeat_timeout;     /* default 30 */
	int     tcp_mux;              /* default 0 */

	/* Transport protocol: "tcp" (default), "quic" */
	char    *protocol;

	/* Wire protocol: "v1" (default) or "v2" (frp >= 0.69) */
	char    *wire_protocol;

	/* QUIC settings */
	int     quic_bind_port;       /* frps QUIC port, default 0 (disabled) */

	/* TLS settings */
	int     tls_enable;           /* default 0 */
	char    *tls_cert_file;       /* client certificate file (optional) */
	char    *tls_key_file;        /* client private key file (optional) */
	char    *tls_trusted_ca_file; /* CA certificate file for verification */
	char    *tls_server_name;     /* SNI server name (optional) */

	/* ESP32 port: inline PEM material. The ESP32 has no config file, and
	 * reading certificates from a filesystem is not guaranteed to be
	 * available, so the public API / Kconfig supply the PEM text itself.
	 * Any of these takes precedence over the corresponding *_file above.
	 * NULL tls_trusted_ca_pem = peer certificate is not verified (the
	 * same rule the frp Go client applies). */
	char    *tls_trusted_ca_pem;
	char    *tls_cert_pem;
	char    *tls_key_pem;

	/* Identity settings */
	char    *user;                /* client user name (for visitor auth) */

	/* Environment settings */
	int     is_router;            /* indicates if running on router (OpenWrt/LEDE) */
};

/* Configuration management functions */
struct common_conf *get_common_config(void);
struct common_conf *init_common_config(void); /* ESP32 port: API config entry */
void free_common_config(void);
void load_config(const char *confile);
int validate_heartbeat_config(void);

/* Proxy service management functions */
struct proxy_service *get_proxy_service(const char *proxy_name);
struct proxy_service *get_all_proxy_services(void);
void free_proxy_service(struct proxy_service *ps);
void free_all_proxy_services(void);
int validate_proxy(struct proxy_service *ps);

/* ESP32 port: register a proxy service from the public API configuration */
struct proxy_service *config_add_proxy_service(const char *name,
	const char *proxy_type, const char *local_ip, int local_port,
	int remote_port, int use_encryption, int use_compression);

#endif //XFRPC_CONFIG_H
