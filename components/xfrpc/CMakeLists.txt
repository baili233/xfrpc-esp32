# SPDX-License-Identifier: GPL-3.0-only
#
# xfrpc ESP-IDF component
#
# Sources:
#   src/    — xfrpc core, copied from upstream and adapted (see
#             "// ESP32 port:" comments)
#   port/   — compatibility layers (mini-libevent, OpenSSL->mbedtls,
#             json-c->cJSON) + public API implementation + platform glue
#   vendor/ — snappy / uthash, third-party, unmodified except where noted

# Optional modules (Kconfig "xfrpc" menu). Sources that are switched off
# are not compiled at all: the Kconfig-guarded stubs in port/module_stubs.c
# (TLS, health check) or the compile-time constant in port/wire_v2_off.c
# (wire protocol v2) take their place.
set(xfrpc_optional_srcs "")

if(CONFIG_XFRPC_ENABLE_TLS)
    list(APPEND xfrpc_optional_srcs
        "port/tls.c"
        "port/mini_event_ssl.c")
endif()

if(CONFIG_XFRPC_ENABLE_WIRE_V2)
    list(APPEND xfrpc_optional_srcs
        "src/wire_v2.c"
        "src/aead_stream.c")
else()
    # wire_protocol_is_v2() is referenced from code paths that are shared
    # with v1 (handle_control_work); the stub keeps it building and lets the
    # optimizer fold those branches away.
    list(APPEND xfrpc_optional_srcs "port/wire_v2_off.c")
endif()

if(CONFIG_XFRPC_ENABLE_COMPRESSION)
    list(APPEND xfrpc_optional_srcs "vendor/snappy/snappy.c")
endif()

if(CONFIG_XFRPC_ENABLE_HEALTH_CHECK)
    list(APPEND xfrpc_optional_srcs "src/health_check.c")
endif()

idf_component_register(
    SRCS
        # xfrpc core
        "src/common.c"
        "src/client.c"
        "src/config.c"
        "src/control.c"
        "src/crypto.c"
        "src/crypto_stream.c"
        "src/debug.c"
        "src/login.c"
        "src/msg.c"
        "src/proxy.c"
        "src/proxy_tcp.c"
        "src/tcpmux.c"
        "src/utils.c"
        "src/xfrpc.c"
        "src/quic_client_transport.c"   # stub build (no HAVE_NGTCP2)
        # ESP32 port: compatibility layers
        "port/mini_event.c"
        "port/openssl_compat.c"
        "port/json_compat.c"
        "port/esp_platform.c"
        "port/module_stubs.c"
        "port/xfrpc_api.c"
        # optional modules, selected by Kconfig
        ${xfrpc_optional_srcs}
    INCLUDE_DIRS
        "."                # component root: upstream includes "vendor/snappy/snappy.h"
        "include"          # public API (must precede src/: both define xfrpc.h)
        "port/include"     # event2/, openssl/, json-c/ compat headers
        "src"
        "vendor"           # uthash.h
        "vendor/snappy"    # snappy.h
    REQUIRES
        mbedtls            # AES/GCM/PBKDF2/HMAC/digests (+ TLS when enabled)
        json               # cJSON (json-c shim)
        esp_netif          # utils.c network info
    PRIV_REQUIRES
        esp_timer          # mini_event monotonic clock
        esp_hw_support     # esp_fill_random / esp_read_mac
        log
)

# Compile with XFRPC_DEBUG so debugconf.debuglevel (Kconfig
# XFRPC_LOG_LEVEL) controls verbosity instead of hard-filtering to errors.
target_compile_definitions(${COMPONENT_LIB} PUBLIC XFRPC_DEBUG)
