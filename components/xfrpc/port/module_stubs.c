// SPDX-License-Identifier: GPL-3.0-only
/*
 * ESP32 port: weak stubs for optional modules that are not compiled in
 * this build (tls.c, visitor.c, xtcp_client.c, xtcp_visitor.c,
 * proxy_udp.c, oidc_auth.c, plugins/).
 *
 * All stubs are __attribute__((weak)): when a Kconfig option pulls in the
 * real module, its definitions override these automatically.
 */

#include <stdlib.h>

#include "debug.h"
#include "tls.h"
#include "proxy.h"
#include "visitor.h"
#include "xtcp_client.h"
#include "xtcp_visitor.h"
#include "oidc_auth.h"

/* ---------------- TLS (tls.c) ---------------- */

__attribute__((weak)) int tls_init(void)
{
    debug(LOG_ERR, "TLS support not compiled in");
    return -1;
}

__attribute__((weak)) struct bufferevent *tls_wrap_bev(struct event_base *base,
                                                       struct bufferevent *bev)
{
    (void)base;
    debug(LOG_ERR, "TLS support not compiled in");
    /* contract: original bev is freed on failure */
    bufferevent_free(bev);
    return NULL;
}

__attribute__((weak)) void tls_cleanup(void)
{
}

__attribute__((weak)) int tls_is_enabled(void)
{
    return 0;
}

__attribute__((weak)) void tls_log_errors(const char *context)
{
    (void)context;
}

__attribute__((weak)) int tls_load_certs_to_ctx(void *ctx)
{
    (void)ctx;
    return -1;
}

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
