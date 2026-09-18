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
        "src/health_check.c"
        "src/utils.c"
        "src/xfrpc.c"
        "src/wire_v2.c"
        "src/aead_stream.c"
        "src/quic_client_transport.c"   # stub build (no HAVE_NGTCP2)
        # ESP32 port: compatibility layers
        "port/mini_event.c"
        "port/openssl_compat.c"
        "port/json_compat.c"
        "port/esp_platform.c"
        "port/module_stubs.c"
        "port/xfrpc_api.c"
        # third-party
        "vendor/snappy/snappy.c"
    INCLUDE_DIRS
        "."                # component root: upstream includes "vendor/snappy/snappy.h"
        "include"          # public API (must precede src/: both define xfrpc.h)
        "port/include"     # event2/, openssl/, json-c/ compat headers
        "src"
        "vendor"           # uthash.h
        "vendor/snappy"    # snappy.h
    REQUIRES
        mbedtls            # AES/GCM/PBKDF2/HMAC/digests
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
