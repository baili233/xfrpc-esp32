// SPDX-License-Identifier: GPL-3.0-only
/*
 * ESP32 port: weak stubs for optional modules that are not compiled in
 * this build (tls.c, visitor.c, xtcp_client.c, xtcp_visitor.c,
 * proxy_udp.c, oidc_auth.c, plugins/).
 *
 * The stubs are __attribute__((weak)) so that a real definition wins at
 * link time. That alone is NOT enough for modules that have a Kconfig
 * switch: this file's members are pulled out of libxfrpc.a first (control.c
 * references several of the stubs), and once a member defines xfrpc_tls_init, the
 * archive member holding the real xfrpc_tls_init() is never sought — a weak
 * definition does not create the undefined reference that pulls it in.
 * Modules with a switch are therefore also guarded by #if, which is what
 * actually makes CONFIG_XFRPC_ENABLE_* decide the implementation; the weak
 * attribute stays as a safety net for any other link order.
 */

#include <stdlib.h>

#include "sdkconfig.h"

#include "debug.h"
#include "tls.h"
#include "proxy.h"
#include "health_check.h"
#include "visitor.h"
#include "xtcp_client.h"
#include "xtcp_visitor.h"
#include "oidc_auth.h"

/* ---------------- TLS (tls.c) ---------------- */

#if !defined(CONFIG_XFRPC_ENABLE_TLS)

__attribute__((weak)) int xfrpc_tls_init(void)
{
    debug(LOG_ERR, "TLS support not compiled in");
    return -1;
}

__attribute__((weak)) struct bufferevent *xfrpc_tls_wrap_bev(struct event_base *base,
                                                       struct bufferevent *bev)
{
    (void)base;
    debug(LOG_ERR, "TLS support not compiled in");
    /* contract: original bev is freed on failure */
    bufferevent_free(bev);
    return NULL;
}

__attribute__((weak)) void xfrpc_tls_cleanup(void)
{
}

__attribute__((weak)) int xfrpc_tls_is_enabled(void)
{
    return 0;
}

__attribute__((weak)) void xfrpc_tls_log_errors(const char *context)
{
    (void)context;
}

__attribute__((weak)) int xfrpc_tls_load_certs_to_ctx(void *ctx)
{
    (void)ctx;
    return -1;
}

#endif /* !CONFIG_XFRPC_ENABLE_TLS */

/* ---------------- health check (health_check.c) ---------------- */

/*
 * XFRPC_ENABLE_HEALTH_CHECK off: no proxy service can carry a
 * health_check_type in an API-configured build, so this is a no-op (and
 * "healthy" is the honest answer — the proxy lifecycle does not depend on
 * a check that never ran).
 */
#if !defined(CONFIG_XFRPC_ENABLE_HEALTH_CHECK)

__attribute__((weak)) void health_check_start_all(struct event_base *base,
                                                  health_check_cb_t callback,
                                                  void *ctx)
{
    (void)base; (void)callback; (void)ctx;
}

__attribute__((weak)) void health_check_stop_all(void)
{
}

__attribute__((weak)) int health_check_is_healthy(const char *proxy_name)
{
    (void)proxy_name;
    return 1;
}

__attribute__((weak)) int health_check_count(void)
{
    return 0;
}

#endif /* !CONFIG_XFRPC_ENABLE_HEALTH_CHECK */

/* ---------------- UDP proxy (proxy_udp.c) ---------------- */

__attribute__((weak)) void handle_udp_packet(struct udp_packet *udp_pkt,
                                             struct proxy_client *client)
{
    (void)client;
    debug(LOG_ERR, "UDP proxy support not compiled in (dropping packet)");
    if (udp_pkt)
        udp_packet_free(udp_pkt);
}

__attribute__((weak)) void udp_proxy_c2s_cb(struct bufferevent *bev, void *ctx)
{
    (void)bev; (void)ctx;
    debug(LOG_ERR, "UDP proxy support not compiled in");
}

__attribute__((weak)) void udp_proxy_s2c_cb(struct bufferevent *bev, void *ctx)
{
    (void)bev; (void)ctx;
    debug(LOG_ERR, "UDP proxy support not compiled in");
}

/* ---------------- visitor (visitor.c) ---------------- */

__attribute__((weak)) void init_visitors(struct event_base *base)
{
    (void)base;
    /* no-op: no visitors can be configured without visitor.c */
}

__attribute__((weak)) void handle_visitor_conn_resp(const char *resp_json,
                                                    struct proxy_client *pc)
{
    (void)resp_json; (void)pc;
    debug(LOG_ERR, "visitor support not compiled in");
}

/* ---------------- xtcp (xtcp_client.c / xtcp_visitor.c) ---------------- */

__attribute__((weak)) void xtcp_client_run(struct event_base *base,
                                           struct proxy_client *client)
{
    (void)base;
    (void)client;
    debug(LOG_ERR, "xtcp support not compiled in");
}

__attribute__((weak)) int xtcp_client_handle_nat_hole_resp(const char *json_str)
{
    (void)json_str;
    return 0; /* not handled */
}

__attribute__((weak)) void xtcp_handle_nat_hole_resp_msg(const char *json_str)
{
    (void)json_str;
    debug(LOG_ERR, "xtcp visitor support not compiled in");
}

/* ---------------- OIDC auth (oidc_auth.c) ---------------- */

__attribute__((weak)) char *oidc_fetch_token(const char *token_endpoint_url,
                                             const char *client_id,
                                             const char *client_secret,
                                             const char *audience,
                                             const char *scope,
                                             const char *trusted_ca_file,
                                             int insecure_skip_verify)
{
    (void)token_endpoint_url; (void)client_id; (void)client_secret;
    (void)audience; (void)scope; (void)trusted_ca_file;
    (void)insecure_skip_verify;
    debug(LOG_ERR, "OIDC auth support not compiled in");
    return NULL;
}
