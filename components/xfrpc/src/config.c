
// SPDX-License-Identifier: GPL-3.0-only
/*
 * Copyright (c) 2023 Dengfeng Liu <liudf0716@gmail.com>
 *
 * ESP32 port: trimmed for programmatic (API) configuration.
 *   - Removed: ini/toml file loading, plugin/user management (pwd/shadow),
 *     visitor section parsing — config is supplied via the public API
 *     (include/xfrpc.h) which calls init_common_config() and
 *     config_add_proxy_service().
 *   - Kept: struct management, defaults, validation, dump helpers.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "uthash.h"
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
	SAFE_FREE(c_conf->tls_cert_file);
	SAFE_FREE(c_conf->tls_key_file);
	SAFE_FREE(c_conf->tls_trusted_ca_file);
	SAFE_FREE(c_conf->tls_server_name);
	SAFE_FREE(c_conf->user);
	SAFE_FREE(c_conf->protocol);
	SAFE_FREE(c_conf->wire_protocol);
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
	config->is_router = 0;
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

	/* NB: must use HASH_ADD_KEYPTR with the string pointer itself.
	 * HASH_ADD_STR in this vendored uthash 1.9.8 passes &ps->proxy_name
	 * (the field address) as the key, so HASH_FIND_STR never matches —
	 * upstream xfrpc uses HASH_ADD_KEYPTR here for the same reason. */
	HASH_ADD_KEYPTR(hh, all_ps, ps->proxy_name, strlen(ps->proxy_name), ps);
	return ps;
}

/**
 * @brief ESP32 port: config-file loading is not supported (API config only).
 */
void load_config(const char *confile)
{
	debug(LOG_ERR, "load_config('%s'): config file loading not supported in this build (API configuration only)",
		confile ? confile : "(null)");
}
