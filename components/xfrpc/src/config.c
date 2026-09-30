
// SPDX-License-Identifier: GPL-3.0-only
/*
 * Copyright (c) 2023 Dengfeng Liu <liudf0716@gmail.com>
 *
 * ESP32 port configuration management. Configuration can be supplied
 * programmatically (include/xfrpc.h) or loaded from INI/TOML text/file.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <strings.h>

#include "ini.h"
#include "toml_parser.h"
#include "uthash.h"
#include "sdkconfig.h"   /* optional-module switches (XFRPC_ENABLE_*) */
#include "config.h"
#include "client.h"
#include "debug.h"
#include "utils.h"
#include "version.h"

/**
 * @brief Array of valid proxy service types supported by the application
 */
static const char *valid_types[] = {
	"tcp",
	"udp",
	"socks5",
	"http",
	"https",
	"tcpmux",
	"stcp",
	"xtcp",
	"sudp",
	NULL
};

/**
 * @brief Global configuration structures
 */
static struct common_conf    *c_conf;    /* Common configuration settings */
static struct proxy_service *all_ps;     /* Hash table of all proxy services */

/**
 * @brief Gets the common configuration settings
 * @return struct common_conf* Pointer to common configuration structure
 */
struct common_conf *get_common_config(void)
{
	return c_conf;
}

/**
 * @brief Frees memory used by common configuration
 *
 * Deallocates memory for:
 * - Server address
 * - Authentication token
 */
void free_common_config(void)
{
	struct common_conf *c_conf = get_common_config();
	if (!c_conf)
		return;
	SAFE_FREE(c_conf->server_addr);
	SAFE_FREE(c_conf->auth_token);
	SAFE_FREE(c_conf->auth_method);
	SAFE_FREE(c_conf->oidc_client_id);
	SAFE_FREE(c_conf->oidc_client_secret);
	SAFE_FREE(c_conf->oidc_audience);
	SAFE_FREE(c_conf->oidc_scope);
	SAFE_FREE(c_conf->oidc_token_endpoint_url);
	SAFE_FREE(c_conf->oidc_trusted_ca_file);
	SAFE_FREE(c_conf->oidc_proxy_url);
	SAFE_FREE(c_conf->tls_cert_file);
	SAFE_FREE(c_conf->tls_key_file);
	SAFE_FREE(c_conf->tls_trusted_ca_file);
	SAFE_FREE(c_conf->tls_server_name);
	SAFE_FREE(c_conf->tls_trusted_ca_pem);
	SAFE_FREE(c_conf->tls_cert_pem);
	SAFE_FREE(c_conf->tls_key_pem);
	SAFE_FREE(c_conf->user);
	SAFE_FREE(c_conf->protocol);
	SAFE_FREE(c_conf->wire_protocol);
}

static int is_true(const char *val)
{
	if (!val)
		return 0;

	return strcasecmp(val, "true") == 0 ||
	       strcasecmp(val, "yes") == 0 ||
	       strcasecmp(val, "on") == 0 ||
	       strcmp(val, "1") == 0;
}

/**
 * @brief Validates if a proxy type string is supported
 *
 * @param val Type string to validate
 * @return const char* Returns the valid type string or NULL if invalid
 */
static const char *get_valid_type(const char *val)
{
	if (!val) {
		return NULL;
	}

	for (int i = 0; valid_types[i]; i++) {
		if (strcmp(val, valid_types[i]) == 0) {
			return valid_types[i];
		}
	}

	return NULL;
}

/**
 * @brief Dumps the common configuration settings to debug log
 *
 * Outputs the following common configuration parameters:
 * - Server address
 * - Server port
 * - Authentication token
 * - Heartbeat interval
 * - Heartbeat timeout
 *
 * @note Does nothing if c_conf is NULL
 */
static void dump_common_conf(void)
{
	if (!c_conf) {
		debug(LOG_ERR, "Error: c_conf is NULL");
		return;
	}

	debug(LOG_DEBUG, "Section[common]: {server_addr:%s, server_port:%d, auth_token:%s, interval:%d, timeout:%d, tls:%d}",
		c_conf->server_addr,
		c_conf->server_port,
		c_conf->auth_token,
		c_conf->heartbeat_interval,
		c_conf->heartbeat_timeout,
		c_conf->tls_enable);
}

/**
 * @brief Dumps configuration details for a single proxy service
 *
 * @param index Index number of the proxy service being dumped
 * @param ps Pointer to proxy service structure to dump
 *
 * @note Exits via xfrpc_fatal() if proxy validation fails
 */
static void dump_proxy_service(const int index, struct proxy_service *ps)
{
	if (!ps)
		return;

	// Set default type
	if (!ps->proxy_type) {
		ps->proxy_type = strdup("tcp");
		assert(ps->proxy_type);
	}

	// Validate configuration
	if (!validate_proxy(ps)) {
		debug(LOG_ERR, "Error: validate_proxy failed");
		// ESP32 port: never exit() — stop the event loop instead
		xfrpc_fatal("validate_proxy failed");
		return;
	}

	// Log proxy service details
	debug(LOG_DEBUG,
		"Proxy service %d: {name:%s, local_port:%d, type:%s, use_encryption:%d, "
		"use_compression:%d, custom_domains:%s, subdomain:%s, locations:%s, "
		"host_header_rewrite:%s, http_user:%s, http_pwd:%s}",
		index,
		ps->proxy_name,
		ps->local_port,
		ps->proxy_type,
		ps->use_encryption,
		ps->use_compression,
		ps->custom_domains,
		ps->subdomain,
		ps->locations,
		ps->host_header_rewrite,
		ps->http_user,
		ps->http_pwd);

	// Log tcpmux-specific fields
	if (ps->proxy_type && strcmp(ps->proxy_type, "tcpmux") == 0) {
		debug(LOG_DEBUG,
			"  TCPMux: {multiplexer:%s, route_by_http_user:%s}",
			ps->multiplexer ? ps->multiplexer : "httpconnect (default)",
			ps->route_by_http_user ? ps->route_by_http_user : "(none)");
	}

	// Log stcp/xtcp/sudp-specific fields
	if (ps->proxy_type && (strcmp(ps->proxy_type, "stcp") == 0 ||
			strcmp(ps->proxy_type, "xtcp") == 0 ||
			strcmp(ps->proxy_type, "sudp") == 0)) {
		debug(LOG_DEBUG,
			"  STCP: {sk:%s, allow_users:%s}",
			ps->sk ? "****" : "(none)",
			ps->allow_users ? ps->allow_users : "(any)");
	}

	// Log health check configuration
	if (ps->health_check_type) {
		debug(LOG_DEBUG,
			"  HealthCheck: {type:%s, url:%s, interval:%ds, timeout:%ds, max_failed:%d}",
			ps->health_check_type,
			ps->health_check_url ? ps->health_check_url : "/",
			ps->health_check_interval,
			ps->health_check_timeout,
			ps->health_check_max_failed);
	}
}

/**
 * @brief Dumps debug information for all configured proxy services
 */
static void dump_all_ps(void)
{
	struct proxy_service *ps = NULL;
	struct proxy_service *tmp = NULL;
	int index = 0;

	HASH_ITER(hh, all_ps, ps, tmp) {
		dump_proxy_service(index++, ps);
	}
}

/**
 * @brief Creates a new proxy service structure with the given name
 *
 * @note Caller is responsible for freeing returned structure
 */
static struct proxy_service *new_proxy_service(const char *name)
{
	if (!name) {
		return NULL;
	}

	// Allocate and verify memory (calloc zeros all fields)
	struct proxy_service *ps = calloc(1, sizeof(struct proxy_service));
	assert(ps);
	assert(c_conf);

	// Initialize required fields
	ps->proxy_name = strdup(name);
	assert(ps->proxy_name);

	// Set non-zero defaults
	ps->service_type = NO_XDPI;
	ps->health_check_interval = 10;
	ps->health_check_timeout = 3;
	ps->health_check_max_failed = 1;

	return ps;
}

// create a new proxy service with suffix "_ftp_data_proxy"
/**
 * @brief Validates proxy service configuration parameters
 *
 * @param ps Pointer to proxy service structure to validate
 * @return int Returns 1 if validation passes, 0 if validation fails
 *
 * Validates proxy configuration based on service type:
 * - Common checks: proxy name and type must exist
 * - Socks5: requires remote port
 * - TCP/UDP: requires local port and IP
 * - HTTP/HTTPS: requires local port, IP, and either custom domains or subdomain
 *
 * Error messages are logged for any validation failures.
 */
int validate_proxy(struct proxy_service *ps)
{
	// Validate basic requirements
	if (!ps || !ps->proxy_name || !ps->proxy_type) {
		return 0;
	}

	// Common validation for services needing local endpoints
	int needs_local_endpoint = (strcmp(ps->proxy_type, "tcp") == 0 ||
							  strcmp(ps->proxy_type, "udp") == 0 ||
							  strcmp(ps->proxy_type, "http") == 0 ||
							  strcmp(ps->proxy_type, "https") == 0 ||
							  strcmp(ps->proxy_type, "tcpmux") == 0 ||
							  strcmp(ps->proxy_type, "stcp") == 0 ||
							  strcmp(ps->proxy_type, "xtcp") == 0 ||
							  strcmp(ps->proxy_type, "sudp") == 0);

	if (needs_local_endpoint && (ps->local_port == 0 || ps->local_ip == NULL)) {
		debug(LOG_ERR, "Proxy [%s] error: local_port or local_ip not found",
			  ps->proxy_name);
		return 0;
	}

	// Type-specific validation
	if (strcmp(ps->proxy_type, "socks5") == 0) {
		if (ps->remote_port == 0) {
			debug(LOG_ERR, "Proxy [%s] error: remote_port not found",
				  ps->proxy_name);
			return 0;
		}
	}
	else if (strcmp(ps->proxy_type, "http") == 0 || strcmp(ps->proxy_type, "https") == 0) {
		// Validate domain configuration
		if (ps->custom_domains && ps->subdomain) {
			debug(LOG_ERR, "Proxy [%s] error: custom_domains and subdomain cannot be set simultaneously",
				  ps->proxy_name);
			return 0;
		}
		if (!ps->custom_domains && !ps->subdomain) {
			debug(LOG_ERR, "Proxy [%s] error: either custom_domains or subdomain must be set",
				  ps->proxy_name);
			return 0;
		}
	}
	else if (strcmp(ps->proxy_type, "tcpmux") == 0) {
		// TCPMux requires domain configuration like http/https
		if (ps->custom_domains && ps->subdomain) {
			debug(LOG_ERR, "Proxy [%s] error: custom_domains and subdomain cannot be set simultaneously",
				  ps->proxy_name);
			return 0;
		}
		if (!ps->custom_domains && !ps->subdomain) {
			debug(LOG_ERR, "Proxy [%s] error: either custom_domains or subdomain must be set for tcpmux",
				  ps->proxy_name);
			return 0;
		}
	}
	else if (strcmp(ps->proxy_type, "stcp") == 0 ||
			 strcmp(ps->proxy_type, "xtcp") == 0 ||
			 strcmp(ps->proxy_type, "sudp") == 0) {
		// STCP/XTCP/SUDP require a secret key
		if (!ps->sk) {
			debug(LOG_ERR, "Proxy [%s] error: sk (secret_key) must be set for %s",
				  ps->proxy_name, ps->proxy_type);
			return 0;
		}
	}
	else if (strcmp(ps->proxy_type, "tcp") != 0 && strcmp(ps->proxy_type, "udp") != 0) {
		debug(LOG_ERR, "Proxy [%s] error: invalid proxy_type", ps->proxy_name);
		return 0;
	}

	return 1;
}

/**
 * @brief Initializes common configuration with default values
 *
 * Default values set:
 * - server_addr: "127.0.0.1"
 * - server_port: 7000
 * - heartbeat_interval: 30 seconds
 * - heartbeat_timeout: 90 seconds
 * - tcp_mux: enabled (1)
 * - is_router: disabled (0)
 *
 * @note Does nothing if config pointer is NULL
 */
static void init_common_conf(struct common_conf *config) {
	if (!config) {
		return;
	}

	// Set default values
	config->server_addr = strdup("127.0.0.1");
	assert(config->server_addr);

	config->server_port = 7000;
	config->heartbeat_interval = 30;
	config->heartbeat_timeout = 90;
	config->tcp_mux = 1;
	config->tls_enable = 0;
	config->protocol = strdup("tcp");
	config->wire_protocol = strdup("v1");
	config->quic_bind_port = 0;
	config->tls_cert_file = NULL;
	config->tls_key_file = NULL;
	config->tls_trusted_ca_file = NULL;
	config->tls_server_name = NULL;
	config->tls_trusted_ca_pem = NULL;
	config->tls_cert_pem = NULL;
	config->tls_key_pem = NULL;
	config->is_router = 0;

#if defined(CONFIG_XFRPC_ENABLE_TLS)
	/* Kconfig-provided inline PEM defaults; the public API overrides them.
	 * Compiling in any TLS material is taken as "this build wants TLS", so
	 * tls_enable follows — otherwise tls_init() would never be called
	 * (control.c inits TLS only when tls_enable is set). */
	if (CONFIG_XFRPC_TLS_CA_PEM[0]) {
		config->tls_trusted_ca_pem = strdup(CONFIG_XFRPC_TLS_CA_PEM);
		config->tls_enable = 1;
	}
	if (CONFIG_XFRPC_TLS_CERT_PEM[0]) {
		config->tls_cert_pem = strdup(CONFIG_XFRPC_TLS_CERT_PEM);
		config->tls_enable = 1;
	}
	if (CONFIG_XFRPC_TLS_KEY_PEM[0]) {
		config->tls_key_pem = strdup(CONFIG_XFRPC_TLS_KEY_PEM);
		config->tls_enable = 1;
	}
#endif
}

/**
 * @brief Validates heartbeat configuration parameters
 *
 * Ensures heartbeat interval is positive and timeout is greater than interval.
 *
 * @return 1 if valid, 0 otherwise (error logged)
 */
int validate_heartbeat_config(void) {
	if (!c_conf)
		return 0;

	if (c_conf->heartbeat_interval <= 0) {
		debug(LOG_ERR, "Error: heartbeat_interval must be positive");
		return 0;
	}

	if (c_conf->heartbeat_timeout < c_conf->heartbeat_interval) {
		debug(LOG_ERR, "Error: heartbeat_timeout must be greater than heartbeat_interval");
		return 0;
	}

	return 1;
}

enum {
	XFRPC_CFG_FORMAT_AUTO = 0,
	XFRPC_CFG_FORMAT_INI = 1,
	XFRPC_CFG_FORMAT_TOML = 2,
};

static int has_toml_extension(const char *path)
{
	const char *ext;

	if (!path)
		return 0;

	ext = strrchr(path, '.');
	return ext && strcasecmp(ext, ".toml") == 0;
}

static int detect_config_format(const char *path, const char *text,
				size_t len, int requested)
{
	const char *p;

	if (requested == XFRPC_CFG_FORMAT_INI ||
	    requested == XFRPC_CFG_FORMAT_TOML)
		return requested;

	if (path)
		return has_toml_extension(path) ? XFRPC_CFG_FORMAT_TOML :
						 XFRPC_CFG_FORMAT_INI;

	if (!text || len == 0)
		return XFRPC_CFG_FORMAT_INI;

	p = text;
	if (len >= 3 && (unsigned char)p[0] == 0xEF &&
	    (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF)
		p += 3;
	while (*p && isspace((unsigned char)*p))
		p++;

	if (strncmp(p, "[[", 2) == 0 ||
	    strstr(text, "serverAddr") ||
	    strstr(text, "serverPort") ||
	    strstr(text, "auth.token") ||
	    strstr(text, "transport."))
		return XFRPC_CFG_FORMAT_TOML;

	return XFRPC_CFG_FORMAT_INI;
}

static void ps_set_string(char **field, const char *value)
{
	if (!field || !value)
		return;
	SAFE_FREE(*field);
	*field = strdup(value);
	assert(*field);
}

static struct proxy_service *get_or_create_proxy_service(const char *name)
{
	struct proxy_service *ps = get_proxy_service(name);

	if (ps)
		return ps;

	ps = new_proxy_service(name);
	if (!ps) {
		debug(LOG_ERR, "Failed to create proxy service");
		return NULL;
	}

	HASH_ADD_KEYPTR(hh, all_ps, ps->proxy_name, strlen(ps->proxy_name), ps);
	return ps;
}

static void add_or_replace_proxy_service(struct proxy_service *ps)
{
	struct proxy_service *old;

	if (!ps)
		return;

	old = get_proxy_service(ps->proxy_name);
	if (old) {
		HASH_DEL(all_ps, old);
		free_proxy_service(old);
	}
	HASH_ADD_KEYPTR(hh, all_ps, ps->proxy_name, strlen(ps->proxy_name), ps);
}

static enum xdpi_service_type convert_service_type(const char *value)
{
	if (!value)
		return NO_XDPI;
	if (strcmp(value, "ssh") == 0)
		return SERVICE_SSH;
	if (strcmp(value, "rdp") == 0)
		return SERVICE_RDP;
	if (strcmp(value, "vnc") == 0)
		return SERVICE_VNC;
	if (strcmp(value, "telnet") == 0)
		return SERVICE_TELNET;
	if (strcmp(value, "http") == 0)
		return SERVICE_HTTP;
	if (strcmp(value, "https") == 0)
		return SERVICE_HTTPS;
	if (strcmp(value, "mstsc") == 0)
		return SERVICE_MSTSC;
	return NO_XDPI;
}

static int common_handler(void *user, const char *section, const char *name,
			  const char *value)
{
	struct common_conf *config = (struct common_conf *)user;

	if (!config || strcmp(section, "common") != 0)
		return 1;

	if (strcmp(name, "server_addr") == 0) {
		ps_set_string(&config->server_addr, value);
	} else if (strcmp(name, "server_port") == 0) {
		config->server_port = atoi(value);
	} else if (strcmp(name, "heartbeat_interval") == 0) {
		config->heartbeat_interval = atoi(value);
	} else if (strcmp(name, "heartbeat_timeout") == 0) {
		config->heartbeat_timeout = atoi(value);
	} else if (strcmp(name, "token") == 0) {
		ps_set_string(&config->auth_token, value);
	} else if (strcmp(name, "tcp_mux") == 0) {
		config->tcp_mux = is_true(value) || atoi(value) != 0;
	} else if (strcmp(name, "protocol") == 0) {
		ps_set_string(&config->protocol, value);
	} else if (strcmp(name, "wire_protocol") == 0) {
		ps_set_string(&config->wire_protocol, value);
	} else if (strcmp(name, "quic_bind_port") == 0) {
		config->quic_bind_port = atoi(value);
	} else if (strcmp(name, "tls_enable") == 0) {
		config->tls_enable = is_true(value) || atoi(value) != 0;
	} else if (strcmp(name, "disable_custom_tls_first_byte") == 0) {
		/* frp compatibility option: xfrpc does not send the TLS first byte. */
	} else if (strcmp(name, "tls_cert_file") == 0) {
		ps_set_string(&config->tls_cert_file, value);
	} else if (strcmp(name, "tls_key_file") == 0) {
		ps_set_string(&config->tls_key_file, value);
	} else if (strcmp(name, "tls_trusted_ca_file") == 0) {
		ps_set_string(&config->tls_trusted_ca_file, value);
	} else if (strcmp(name, "tls_server_name") == 0) {
		ps_set_string(&config->tls_server_name, value);
	} else if (strcmp(name, "user") == 0) {
		ps_set_string(&config->user, value);
	} else if (strcmp(name, "auth_method") == 0) {
		ps_set_string(&config->auth_method, value);
	} else if (strcmp(name, "oidc_client_id") == 0) {
		ps_set_string(&config->oidc_client_id, value);
	} else if (strcmp(name, "oidc_client_secret") == 0) {
		ps_set_string(&config->oidc_client_secret, value);
	} else if (strcmp(name, "oidc_audience") == 0) {
		ps_set_string(&config->oidc_audience, value);
	} else if (strcmp(name, "oidc_scope") == 0) {
		ps_set_string(&config->oidc_scope, value);
	} else if (strcmp(name, "oidc_token_endpoint_url") == 0) {
		ps_set_string(&config->oidc_token_endpoint_url, value);
	} else if (strcmp(name, "oidc_trusted_ca_file") == 0) {
		ps_set_string(&config->oidc_trusted_ca_file, value);
	} else if (strcmp(name, "oidc_proxy_url") == 0) {
		ps_set_string(&config->oidc_proxy_url, value);
	} else if (strcmp(name, "oidc_insecure_skip_verify") == 0) {
		config->oidc_insecure_skip_verify =
			is_true(value) || atoi(value) != 0;
	} else {
		debug(LOG_WARNING, "Unknown option %s in section [%s]",
		      name, section);
	}

	return 1;
}

static int proxy_service_handler(void *user, const char *sect, const char *nm,
				 const char *value)
{
	const char *proxy_name = sect;
	struct proxy_service *ps;

	(void)user;

	if (strcmp(sect, "common") == 0)
		return 1;

	if (strstr(sect, "visitor") != NULL) {
		debug(LOG_WARNING, "Visitor configuration is not supported: [%s]",
		      sect);
		return 1;
	}

	if (strncmp(sect, "proxy:", 6) == 0) {
		proxy_name = sect + 6;
		if (*proxy_name == '\0') {
			debug(LOG_ERR, "Empty proxy name after 'proxy:' prefix");
			return 0;
		}
	}

	ps = get_or_create_proxy_service(proxy_name);
	if (!ps)
		return 0;

	if (strcmp(nm, "type") == 0) {
		if (!get_valid_type(value)) {
			debug(LOG_ERR, "Unsupported proxy type: %s", value);
			return 0;
		}
		ps_set_string(&ps->proxy_type, value);
	} else if (strcmp(nm, "local_ip") == 0) {
		ps_set_string(&ps->local_ip, value);
	} else if (strcmp(nm, "bind_addr") == 0) {
		ps_set_string(&ps->bind_addr, value);
	} else if (strcmp(nm, "local_port") == 0) {
		ps->local_port = atoi(value);
	} else if (strcmp(nm, "remote_port") == 0) {
		ps->remote_port = atoi(value);
	} else if (strcmp(nm, "use_encryption") == 0) {
		ps->use_encryption = is_true(value) || atoi(value) != 0;
	} else if (strcmp(nm, "use_compression") == 0) {
		ps->use_compression = is_true(value) || atoi(value) != 0;
	} else if (strcmp(nm, "http_user") == 0) {
		ps_set_string(&ps->http_user, value);
	} else if (strcmp(nm, "http_pwd") == 0) {
		ps_set_string(&ps->http_pwd, value);
	} else if (strcmp(nm, "request_headers") == 0) {
		ps_set_string(&ps->request_headers, value);
	} else if (strcmp(nm, "response_headers") == 0) {
		ps_set_string(&ps->response_headers, value);
	} else if (strcmp(nm, "subdomain") == 0) {
		ps_set_string(&ps->subdomain, value);
	} else if (strcmp(nm, "custom_domains") == 0) {
		ps_set_string(&ps->custom_domains, value);
	} else if (strcmp(nm, "locations") == 0) {
		ps_set_string(&ps->locations, value);
	} else if (strcmp(nm, "host_header_rewrite") == 0) {
		ps_set_string(&ps->host_header_rewrite, value);
	} else if (strcmp(nm, "group") == 0) {
		ps_set_string(&ps->group, value);
	} else if (strcmp(nm, "group_key") == 0) {
		ps_set_string(&ps->group_key, value);
	} else if (strcmp(nm, "plugin") == 0) {
		ps_set_string(&ps->plugin, value);
	} else if (strcmp(nm, "plugin_user") == 0) {
		ps_set_string(&ps->plugin_user, value);
	} else if (strcmp(nm, "plugin_pwd") == 0) {
		ps_set_string(&ps->plugin_pwd, value);
	} else if (strcmp(nm, "plugin_unix_path") == 0) {
		ps_set_string(&ps->plugin_unix_path, value);
	} else if (strcmp(nm, "root_dir") == 0) {
		ps_set_string(&ps->s_root_dir, value);
	} else if (strcmp(nm, "multiplexer") == 0) {
		ps_set_string(&ps->multiplexer, value);
	} else if (strcmp(nm, "route_by_http_user") == 0) {
		ps_set_string(&ps->route_by_http_user, value);
	} else if (strcmp(nm, "sk") == 0) {
		ps_set_string(&ps->sk, value);
	} else if (strcmp(nm, "allow_users") == 0) {
		ps_set_string(&ps->allow_users, value);
	} else if (strcmp(nm, "service_type") == 0) {
		ps->service_type = convert_service_type(value);
	} else if (strcmp(nm, "health_check_type") == 0) {
		ps_set_string(&ps->health_check_type, value);
	} else if (strcmp(nm, "health_check_url") == 0) {
		ps_set_string(&ps->health_check_url, value);
	} else if (strcmp(nm, "health_check_interval") == 0) {
		ps->health_check_interval = atoi(value);
	} else if (strcmp(nm, "health_check_timeout") == 0) {
		ps->health_check_timeout = atoi(value);
	} else if (strcmp(nm, "health_check_max_failed") == 0) {
		ps->health_check_max_failed = atoi(value);
	} else if (strcmp(nm, "start_time") == 0) {
		int hour = atoi(value);
		if (hour < 0 || hour > 23) {
			debug(LOG_ERR, "Invalid start_time value: %s", value);
			return 0;
		}
		ps->start_time = hour;
	} else if (strcmp(nm, "end_time") == 0) {
		int hour = atoi(value);
		if (hour < 0 || hour > 23) {
			debug(LOG_ERR, "Invalid end_time value: %s", value);
			return 0;
		}
		ps->end_time = hour;
	} else {
		debug(LOG_WARNING, "Unknown option %s in section [%s]",
		      nm, sect);
	}

	if (ps->proxy_type && strcmp(ps->proxy_type, "socks5") == 0 &&
	    ps->remote_port == 0)
		ps->remote_port = DEFAULT_SOCKS5_PORT;

	return 1;
}

static void load_toml_common(struct toml_doc *doc)
{
	void *root = toml_find_array_section(doc, "", -1);
	const char *v;

	if (!root) {
		debug(LOG_ERR, "TOML: no root section found");
		return;
	}

	if ((v = toml_get(root, "serverAddr")))
		ps_set_string(&c_conf->server_addr, v);
	if ((v = toml_get(root, "serverPort")))
		c_conf->server_port = atoi(v);
	if ((v = toml_get(root, "user")))
		ps_set_string(&c_conf->user, v);
	if ((v = toml_get(root, "auth.token")))
		ps_set_string(&c_conf->auth_token, v);
	if ((v = toml_get(root, "auth.method")))
		ps_set_string(&c_conf->auth_method, v);
	if ((v = toml_get(root, "auth.oidc.clientID")))
		ps_set_string(&c_conf->oidc_client_id, v);
	if ((v = toml_get(root, "auth.oidc.clientSecret")))
		ps_set_string(&c_conf->oidc_client_secret, v);
	if ((v = toml_get(root, "auth.oidc.audience")))
		ps_set_string(&c_conf->oidc_audience, v);
	if ((v = toml_get(root, "auth.oidc.scope")))
		ps_set_string(&c_conf->oidc_scope, v);
	if ((v = toml_get(root, "auth.oidc.tokenEndpointURL")))
		ps_set_string(&c_conf->oidc_token_endpoint_url, v);
	if ((v = toml_get(root, "auth.oidc.trustedCaFile")))
		ps_set_string(&c_conf->oidc_trusted_ca_file, v);
	if ((v = toml_get(root, "auth.oidc.proxyURL")))
		ps_set_string(&c_conf->oidc_proxy_url, v);
	if ((v = toml_get(root, "auth.oidc.insecureSkipVerify")))
		c_conf->oidc_insecure_skip_verify =
			is_true(v) || atoi(v) != 0;
	if ((v = toml_get(root, "transport.protocol")))
		ps_set_string(&c_conf->protocol, v);
	if ((v = toml_get(root, "transport.wireProtocol")))
		ps_set_string(&c_conf->wire_protocol, v);
	if ((v = toml_get(root, "transport.tcpMux")))
		c_conf->tcp_mux = is_true(v) || atoi(v) != 0;
	if ((v = toml_get(root, "transport.heartbeatInterval")))
		c_conf->heartbeat_interval = atoi(v);
	if ((v = toml_get(root, "transport.heartbeatTimeout")))
		c_conf->heartbeat_timeout = atoi(v);
	if ((v = toml_get(root, "transport.tls.enable")))
		c_conf->tls_enable = is_true(v) || atoi(v) != 0;
	if ((v = toml_get(root, "transport.tls.certFile")))
		ps_set_string(&c_conf->tls_cert_file, v);
	if ((v = toml_get(root, "transport.tls.keyFile")))
		ps_set_string(&c_conf->tls_key_file, v);
	if ((v = toml_get(root, "transport.tls.trustedCaFile")))
		ps_set_string(&c_conf->tls_trusted_ca_file, v);
	if ((v = toml_get(root, "transport.tls.serverName")))
		ps_set_string(&c_conf->tls_server_name, v);
	if ((v = toml_get(root, "quicBindPort")))
		c_conf->quic_bind_port = atoi(v);
}

static int load_toml_proxies(struct toml_doc *doc)
{
	int count = toml_count_array_sections(doc, "proxies");

	for (int i = 0; i < count; i++) {
		void *sec = toml_find_array_section(doc, "proxies", i);
		const char *name;
		const char *v;
		struct proxy_service *ps;

		if (!sec)
			continue;

		name = toml_get(sec, "name");
		if (!name) {
			debug(LOG_ERR, "TOML: proxies[%d] has no name", i);
			return -1;
		}

		if ((v = toml_get(sec, "enabled")) &&
		    !(is_true(v) || atoi(v) != 0)) {
			debug(LOG_DEBUG, "Proxy [%s] is disabled, skipping", name);
			continue;
		}

		ps = new_proxy_service(name);
		if (!ps)
			return -1;

		if ((v = toml_get(sec, "type")))
			ps_set_string(&ps->proxy_type, v);
		if (!get_valid_type(ps->proxy_type)) {
			debug(LOG_ERR, "TOML: proxy [%s] has invalid type: %s",
			      name, ps->proxy_type ? ps->proxy_type : "(null)");
			free_proxy_service(ps);
			return -1;
		}
		if ((v = toml_get(sec, "localIP")))
			ps_set_string(&ps->local_ip, v);
		if ((v = toml_get(sec, "bindAddr")))
			ps_set_string(&ps->bind_addr, v);
		if ((v = toml_get(sec, "localPort")))
			ps->local_port = atoi(v);
		if ((v = toml_get(sec, "remotePort")))
			ps->remote_port = atoi(v);
		if ((v = toml_get(sec, "serviceType")))
			ps->service_type = convert_service_type(v);
		if ((v = toml_get(sec, "startTime")))
			ps->start_time = atoi(v);
		if ((v = toml_get(sec, "endTime")))
			ps->end_time = atoi(v);
		if ((v = toml_get(sec, "transport.useEncryption")))
			ps->use_encryption =
				is_true(v) || atoi(v) != 0;
		if ((v = toml_get(sec, "transport.useCompression")))
			ps->use_compression =
				is_true(v) || atoi(v) != 0;
		if ((v = toml_get(sec, "customDomains")))
			ps_set_string(&ps->custom_domains, v);
		if ((v = toml_get(sec, "subdomain")))
			ps_set_string(&ps->subdomain, v);
		if ((v = toml_get(sec, "locations")))
			ps_set_string(&ps->locations, v);
		if ((v = toml_get(sec, "hostHeaderRewrite")))
			ps_set_string(&ps->host_header_rewrite, v);
		if ((v = toml_get(sec, "httpUser")))
			ps_set_string(&ps->http_user, v);
		if ((v = toml_get(sec, "httpPassword")))
			ps_set_string(&ps->http_pwd, v);
		if ((v = xfrpc_toml_get_table_pairs(sec, "requestHeaders.set")))
			ps_set_string(&ps->request_headers, v);
		if ((v = xfrpc_toml_get_table_pairs(sec, "responseHeaders.set")))
			ps_set_string(&ps->response_headers, v);
		if ((v = toml_get(sec, "loadBalancer.group")))
			ps_set_string(&ps->group, v);
		if ((v = toml_get(sec, "loadBalancer.groupKey")))
			ps_set_string(&ps->group_key, v);
		if ((v = toml_get(sec, "plugin.type")))
			ps_set_string(&ps->plugin, v);
		if ((v = toml_get(sec, "plugin.httpUser")))
			ps_set_string(&ps->plugin_user, v);
		if ((v = toml_get(sec, "plugin.httpPassword")))
			ps_set_string(&ps->plugin_pwd, v);
		if ((v = toml_get(sec, "plugin.unixPath")))
			ps_set_string(&ps->plugin_unix_path, v);
		if ((v = toml_get(sec, "plugin.localPath")))
			ps_set_string(&ps->s_root_dir, v);
		if ((v = toml_get(sec, "multiplexer")))
			ps_set_string(&ps->multiplexer, v);
		if ((v = toml_get(sec, "routeByHTTPUser")))
			ps_set_string(&ps->route_by_http_user, v);
		if ((v = toml_get(sec, "secretKey")))
			ps_set_string(&ps->sk, v);
		if ((v = toml_get(sec, "allowUsers")))
			ps_set_string(&ps->allow_users, v);
		if ((v = toml_get(sec, "healthCheck.type")))
			ps_set_string(&ps->health_check_type, v);
		if ((v = toml_get(sec, "healthCheck.path")))
			ps_set_string(&ps->health_check_url, v);
		if ((v = toml_get(sec, "healthCheck.intervalSeconds")))
			ps->health_check_interval = atoi(v);
		if ((v = toml_get(sec, "healthCheck.timeoutSeconds")))
			ps->health_check_timeout = atoi(v);
		if ((v = toml_get(sec, "healthCheck.maxFailed")))
			ps->health_check_max_failed = atoi(v);

		add_or_replace_proxy_service(ps);
	}

	return 0;
}

static int load_toml_config_text(struct toml_doc *doc)
{
	load_toml_common(doc);
	return load_toml_proxies(doc);
}

static int validate_loaded_config(void)
{
	struct proxy_service *ps = NULL;
	struct proxy_service *tmp = NULL;

	if (!c_conf || !c_conf->server_addr || !c_conf->server_addr[0]) {
		debug(LOG_ERR, "Config error: server_addr is required");
		return -1;
	}

	if (c_conf->wire_protocol &&
	    strcmp(c_conf->wire_protocol, "v1") != 0 &&
	    strcmp(c_conf->wire_protocol, "v2") != 0) {
		debug(LOG_ERR, "Config error: invalid wire_protocol '%s'",
		      c_conf->wire_protocol);
		return -1;
	}

	if (!validate_heartbeat_config())
		return -1;

	if (c_conf->protocol && strcmp(c_conf->protocol, "quic") == 0) {
		if (c_conf->tcp_mux) {
			debug(LOG_INFO, "QUIC protocol: disabling tcp_mux");
			c_conf->tcp_mux = 0;
		}
	}

	HASH_ITER(hh, all_ps, ps, tmp) {
		if (!validate_proxy(ps)) {
			debug(LOG_ERR, "Proxy [%s] configuration is invalid",
			      ps->proxy_name);
			return -1;
		}
	}

	dump_common_conf();
	dump_all_ps();
	return 0;
}

int load_config_file(const char *confile, int format)
{
	struct toml_doc *doc = NULL;
	int detected;
	int rc = 0;

	if (!confile || !confile[0]) {
		debug(LOG_ERR, "Config file path is required");
		return -1;
	}

	detected = detect_config_format(confile, NULL, 0, format);
	free_all_proxy_services();
	if (!init_common_config())
		return -1;

	debug(LOG_DEBUG, "Reading %s configuration file '%s'",
	      detected == XFRPC_CFG_FORMAT_TOML ? "TOML" : "INI", confile);

	if (detected == XFRPC_CFG_FORMAT_TOML) {
		if (xfrpc_toml_parse_file(confile, &doc) != 0 ||
		    load_toml_config_text(doc) != 0)
			rc = -1;
		xfrpc_toml_doc_free(doc);
	} else {
		rc = ini_parse(confile, common_handler, c_conf);
		if (rc == 0)
			rc = ini_parse(confile, proxy_service_handler, NULL);
		if (rc != 0) {
			debug(LOG_ERR, "INI config parse failed (line/error %d)", rc);
			rc = -1;
		}
	}

	if (rc == 0)
		rc = validate_loaded_config();
	if (rc != 0) {
		free_all_proxy_services();
		free_common_config();
	}
	return rc;
}

int load_config_string(const char *text, size_t len, int format)
{
	struct toml_doc *doc = NULL;
	char *copy;
	int detected;
	int rc = 0;

	if (!text) {
		debug(LOG_ERR, "Config string is required");
		return -1;
	}
	if (len == 0)
		len = strlen(text);
	if (len > INT_MAX) {
		debug(LOG_ERR, "Config string too large");
		return -1;
	}

	copy = malloc(len + 1);
	if (!copy)
		return -1;
	memcpy(copy, text, len);
	copy[len] = '\0';

	detected = detect_config_format(NULL, copy, len, format);
	free_all_proxy_services();
	if (!init_common_config()) {
		free(copy);
		return -1;
	}

	debug(LOG_DEBUG, "Reading %s configuration from string",
	      detected == XFRPC_CFG_FORMAT_TOML ? "TOML" : "INI");

	if (detected == XFRPC_CFG_FORMAT_TOML) {
		if (xfrpc_toml_parse_string(copy, len, &doc) != 0 ||
		    load_toml_config_text(doc) != 0)
			rc = -1;
		xfrpc_toml_doc_free(doc);
	} else {
		rc = ini_parse_string(copy, common_handler, c_conf);
		if (rc == 0)
			rc = ini_parse_string(copy, proxy_service_handler, NULL);
		if (rc != 0) {
			debug(LOG_ERR, "INI config parse failed (line/error %d)", rc);
			rc = -1;
		}
	}

	free(copy);

	if (rc == 0)
		rc = validate_loaded_config();
	if (rc != 0) {
		free_all_proxy_services();
		free_common_config();
	}
	return rc;
}

struct proxy_service *get_proxy_service(const char *proxy_name)
{
	if (!proxy_name)
		return NULL;

	struct proxy_service *ps = NULL;
	HASH_FIND_STR(all_ps, proxy_name, ps);
	return ps;
}

struct proxy_service *get_all_proxy_services()
{
	return all_ps;
}

void free_proxy_service(struct proxy_service *ps)
{
	if (!ps) {
		return;
	}

	SAFE_FREE(ps->proxy_name);
	SAFE_FREE(ps->proxy_type);
	SAFE_FREE(ps->local_ip);
	SAFE_FREE(ps->custom_domains);
	SAFE_FREE(ps->subdomain);
	SAFE_FREE(ps->locations);
	SAFE_FREE(ps->host_header_rewrite);
	SAFE_FREE(ps->http_user);
	SAFE_FREE(ps->http_pwd);
	SAFE_FREE(ps->request_headers);
	SAFE_FREE(ps->response_headers);
	SAFE_FREE(ps->group);
	SAFE_FREE(ps->group_key);
	SAFE_FREE(ps->plugin);
	SAFE_FREE(ps->plugin_user);
	SAFE_FREE(ps->plugin_pwd);
	SAFE_FREE(ps->plugin_unix_path);
	SAFE_FREE(ps->s_root_dir);
	SAFE_FREE(ps->bind_addr);
	SAFE_FREE(ps->multiplexer);
	SAFE_FREE(ps->route_by_http_user);
	SAFE_FREE(ps->sk);
	SAFE_FREE(ps->allow_users);
	SAFE_FREE(ps->health_check_type);
	SAFE_FREE(ps->health_check_url);
	SAFE_FREE(ps);
}

/**
 * @brief Frees all proxy_service structures from the all_ps hash table.
 */
void free_all_proxy_services(void)
{
	struct proxy_service *current_ps, *tmp;

	HASH_ITER(hh, all_ps, current_ps, tmp) {
		HASH_DEL(all_ps, current_ps);  /* delete it (all_ps advances to next) */
		free_proxy_service(current_ps); /* free it */
	}
	all_ps = NULL; /* Ensure the hash table head is NULL after clearing */
}

/* ------------------------------------------------------------------ */
/* ESP32 port: programmatic configuration API                         */
/* ------------------------------------------------------------------ */

/**
 * @brief Allocates (or resets) the common config and fills it with defaults.
 *
 * Called by the public API layer (port/xfrpc_api.c) before overriding
 * individual fields.
 */
struct common_conf *init_common_config(void)
{
	if (c_conf) {
		/* restart support: release previous values, keep the struct */
		free_common_config();
		memset(c_conf, 0, sizeof(*c_conf));
	} else {
		c_conf = calloc(1, sizeof(struct common_conf));
		assert(c_conf);
	}
	init_common_conf(c_conf);
	return c_conf;
}

/**
 * @brief Registers a proxy service from the public API configuration.
 *
 * Creates a proxy_service, fills the TCP-proxy fields and adds it to the
 * global hash. Duplicate names replace the previous entry.
 *
 * @return the registered proxy_service, or NULL on error
 */
struct proxy_service *config_add_proxy_service(const char *name,
	const char *proxy_type, const char *local_ip, int local_port,
	int remote_port, int use_encryption, int use_compression)
{
	if (!name || !local_ip || !get_valid_type(proxy_type)) {
		debug(LOG_ERR, "add proxy service: invalid arguments (name=%s type=%s)",
			name ? name : "(null)", proxy_type ? proxy_type : "(null)");
		return NULL;
	}

	/* replace existing entry with the same name */
	struct proxy_service *old = get_proxy_service(name);
	if (old) {
		HASH_DEL(all_ps, old);
		free_proxy_service(old);
	}

	struct proxy_service *ps = new_proxy_service(name);
	if (!ps)
		return NULL;

	ps->proxy_type      = strdup(proxy_type);
	ps->local_ip        = strdup(local_ip);
	ps->local_port      = local_port;
	ps->remote_port     = remote_port;
	ps->use_encryption  = !!use_encryption;
	ps->use_compression = !!use_compression;

#if !defined(CONFIG_XFRPC_ENABLE_COMPRESSION)
	/* snappy is not compiled: say so instead of silently not compressing. */
	if (ps->use_compression) {
		debug(LOG_WARNING, "proxy [%s]: compression requested but "
		      "XFRPC_ENABLE_COMPRESSION is off (sending uncompressed)",
		      ps->proxy_name);
		ps->use_compression = 0;
	}
#endif

	/* NB: must use HASH_ADD_KEYPTR with the string pointer itself.
	 * HASH_ADD_STR in this vendored uthash 1.9.8 passes &ps->proxy_name
	 * (the field address) as the key, so HASH_FIND_STR never matches —
	 * upstream xfrpc uses HASH_ADD_KEYPTR here for the same reason. */
	HASH_ADD_KEYPTR(hh, all_ps, ps->proxy_name, strlen(ps->proxy_name), ps);
	return ps;
}

void load_config(const char *confile)
{
	if (load_config_file(confile, XFRPC_CFG_FORMAT_AUTO) != 0)
		debug(LOG_ERR, "load_config('%s') failed",
		      confile ? confile : "(null)");
}
