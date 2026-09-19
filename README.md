# xfrpc-esp32

[English](README.md) | [简体中文](README_zh.md)

An ESP-IDF component that brings [xfrpc](https://github.com/liudf0716/xfrpc) — a
lightweight C implementation of the [frp](https://github.com/fatedier/frp) client —
to the ESP32 series, so an MCU behind NAT can publish its local TCP services to a
public `frps` server without a full Linux userland.

The upstream code is not rewritten: it is compiled as-is inside the component
(`components/xfrpc/src/`), and the POSIX/libevent/OpenSSL/json-c dependencies are
satisfied by a thin porting layer (`components/xfrpc/port/`) built on ESP-IDF's own
mbedtls, cJSON and lwIP. Only a handful of lines in `src/` are marked
`// ESP32 port:` where a Linux-only call had to be replaced.

## Features

| Feature | Status |
| ------- | ------ |
| TCP proxy | Yes |
| `tcp_mux` (multiplexed control connection) | Yes, on by default |
| `use_encryption` (AES-128-CFB stream cipher) | Yes |
| `use_compression` (snappy) | Yes (optional at build time) |
| Heartbeat / automatic reconnect | Yes |
| Programmatic configuration via public API | Yes |
| TLS control connection | Yes (mbedtls, optional at build time), incl. mTLS |
| UDP proxy | Not yet (stub) |
| `stcp` / visitor / `xtcp` (P2P) | Not yet (stub) |
| HTTP/HTTPS proxy type, custom domains | Not yet (stub) |
| `transport.wireProtocol = v2` | Yes (optional at build time) |
| QUIC transport | Not yet (stub, no ngtcp2) |
| kcp / websocket / wss | No |

TLS, wire protocol v2, snappy and the health checker are each behind a Kconfig
switch and are **off by default** — a default build is the smallest one, and
turning on what your `frps` needs costs only that module (see
[Configuration](#configuration)).

`frps` requires `tcp_mux` to be enabled on the server side as well; it is on by
default in modern `frps` releases.

## Repository layout

```
xfrpc-esp32/
├── components/
│   └── xfrpc/                 ESP-IDF component
│       ├── include/xfrpc.h    public API
│       ├── src/               upstream xfrpc core (see upstream LICENSE)
│       ├── port/              ESP32 porting layer
│       │   ├── mini_event.c   minimal libevent replacement
│       │   ├── mini_event_ssl.c  mbedtls backend for the bufferevent (TLS)
│       │   ├── tls.c          mbedtls TLS for the frps control connection (TLS)
│       │   ├── wire_v2_off.c  wire_protocol_is_v2() == 0 when v2 is off
│       │   ├── openssl_compat.c  OpenSSL EVP/HMAC/MD5 -> mbedtls
│       │   ├── json_compat.c  json-c -> cJSON
│       │   ├── esp_platform.c uname(), fatal handling, commandline/syslog stubs
│       │   ├── module_stubs.c weak stubs for modules not compiled in
│       │   └── xfrpc_api.c    xfrpc_start()/xfrpc_stop() implementation
│       ├── vendor/            snappy, uthash
│       ├── CMakeLists.txt
│       └── Kconfig
├── examples/
│   └── esp32_http_tunnel/     standalone ESP-IDF project
└── LICENSE                    GPL-3.0-only (inherited from upstream)
```

## Requirements

- ESP-IDF **v5.5** (developed and tested against v5.5; v5.1+ is likely to work)
- A target with enough RAM for the xfrpc task (default stack: 16 KB)
- An `frps` server reachable from the ESP32, protocol version 0.71.x

The example defaults to `esp32s3`. To use another target, run
`idf.py set-target <target>` inside the example directory.

## Quick start

The example connects to WiFi, serves a small HTTP page on a local port, and
exposes that port through `frps`.

```bash
cd examples/esp32_http_tunnel

idf.py set-target esp32s3
idf.py menuconfig     # xfrpc example configuration -> WiFi SSID/password, frps address
idf.py build flash monitor
```

Then, from any machine that can reach the `frps` server:

```bash
curl http://<frps-host>:6000/
```

A response means the path `frps -> ESP32 -> local HTTP server` is working. See
[examples/esp32_http_tunnel/README.md](examples/esp32_http_tunnel/README.md) for
details.

On Windows, build from a shell with the ESP-IDF environment already exported
(`export.bat` / `export.ps1` from the ESP-IDF install, which puts `idf.py` on
`PATH`):

```bash
idf.py build
```

## Using the component in your own project

Copy `components/xfrpc/` into your project's `components/` directory, or point at it
from your project `CMakeLists.txt`:

```cmake
set(EXTRA_COMPONENT_DIRS "/path/to/xfrpc-esp32/components")
```

The component is built with `XFRPC_DEBUG` defined and logs through `esp_log`; runtime
verbosity is controlled by `CONFIG_XFRPC_LOG_LEVEL`.

## API

```c
#include "xfrpc.h"

xfrpc_client_config_t cfg = {
    .server_addr = "203.0.113.10",
    .server_port = 7000,
    .auth_token  = "secret",
    .tcp_mux     = 1,
};

xfrpc_tcp_proxy_t proxies[] = {
    {
        .name            = "web",
        .local_ip        = "127.0.0.1",
        .local_port      = 80,
        .remote_port     = 6000,
        .use_encryption  = 1,
        .use_compression = 1,
    },
};

xfrpc_start(&cfg, proxies, 1, on_state, NULL);
```

`xfrpc_start()` copies all string fields, so the caller's buffers may be stack
allocated. It launches a dedicated FreeRTOS task and returns immediately:

| Return value | Meaning |
| ------------ | ------- |
| `0`  | task started |
| `1`  | already running |
| `-1` | invalid configuration or task creation failed |

The other entry points are:

| Function | Description |
| -------- | ----------- |
| `xfrpc_stop(void)` | Request a clean stop; the task exits asynchronously. Safe from any task. |
| `xfrpc_is_connected(void)` | `true` while the control connection is up and logged in. |

State transitions are reported through the callback passed to `xfrpc_start()`, which
runs on the xfrpc event task:

| State | Meaning |
| ----- | ------- |
| `XFRPC_STATE_CONNECTING` | task started, dialing `frps` |
| `XFRPC_STATE_CONNECTED` | TCP connection established |
| `XFRPC_STATE_LOGIN_OK` | `frps` accepted the login |
| `XFRPC_STATE_RECONNECTING` | control connection lost, retrying |
| `XFRPC_STATE_FATAL` | unrecoverable error, task exited |
| `XFRPC_STATE_STOPPED` | `xfrpc_stop()` completed, task exited |

## Configuration

Component options (`idf.py menuconfig` → `xfrpc`):

| Option | Default | Description |
| ------ | ------- | ----------- |
| `CONFIG_XFRPC_TASK_STACK_SIZE` | 16384 | Stack of the xfrpc event loop task. |
| `CONFIG_XFRPC_TASK_PRIORITY` | 5 | Priority of that task. |
| `CONFIG_XFRPC_LOG_LEVEL` | 6 | syslog-style verbosity, `0`=emerg … `7`=debug. |

### Optional modules

These switches decide what is compiled at all: with a switch off, the sources
are not built and stubs (or, for wire v2, a constant `0` in
`port/wire_v2_off.c`) take their place, so the linker drops the subsystem
completely. All of them default to `n`.

| Option | Default | Description |
| ------ | ------- | ----------- |
| `CONFIG_XFRPC_ENABLE_TLS` | `n` | mbedtls TLS transport for the control connection. Selects `MBEDTLS_TLS_CLIENT`. |
| `CONFIG_XFRPC_TLS_CA_PEM` | `""` | PEM of the CA that signed the `frps` certificate. Empty = the peer is **not** verified, matching `frpc` without `caFile`. |
| `CONFIG_XFRPC_TLS_CERT_PEM` | `""` | Client certificate PEM (mTLS). |
| `CONFIG_XFRPC_TLS_KEY_PEM` | `""` | Client private key PEM (mTLS). |
| `CONFIG_XFRPC_TLS_HANDSHAKE_TIMEOUT` | 10 | Seconds before an unfinished handshake is aborted and retried. |
| `CONFIG_XFRPC_ENABLE_WIRE_V2` | `n` | `frp` wire protocol v2 (`frps` with `transport.protocol = "v2"`). |
| `CONFIG_XFRPC_ENABLE_COMPRESSION` | `n` | snappy payload compression for proxies created with `use_compression = 1`. |
| `CONFIG_XFRPC_ENABLE_HEALTH_CHECK` | `n` | Periodic local-service health checks. |

Measured effect on the example app binary (ESP32-S3, `esp32_http_tunnel`,
`idf.py build`):

| TLS | wire v2 | compression | health check | app size | free in 1 MB partition |
| --- | ------- | ----------- | ------------ | -------- | ---------------------- |
| `n` | `n` | `n` | `n` | 879,392 B (0xd6b20) | 16% |
| `n` | `y` | `y` | `y` | 897,904 B (0xdb370) | 14% |
| `y` | `n` | `n` | `n` | 956,864 B (0xe99c0) | 9% |
| `y` | `y` | `y` | `y` | 975,248 B (0xee190) | 7% |

The all-off build is the new default and the smallest; the all-on build is
still 7% inside a 1 MB app partition. Enabling TLS pulls in the mbedtls TLS
client, which is the bulk of the difference between the last two rows.

TCP proxy, AES encryption and `tcp_mux` are the protocol MVP and are always
compiled in; `tcp_mux`, `use_encryption` and `use_compression` can be disabled
per proxy at runtime through the API.

## TLS

`frps` enables TLS recognition on every connection when `transport.tls.force`
is set — and also when it is not, in which case the server just keeps plaintext
for non-TLS clients. So both of these work:

```c
/* verify the server certificate against a CA */
static const char ca_pem[] = "-----BEGIN CERTIFICATE-----\n...\n";
xfrpc_client_config_t cfg = {
    .server_addr = "frps.example.com",
    .tls_enable  = 1,
    .tls_ca_pem  = ca_pem,
};

/* no CA: encrypted, but the peer is not authenticated (frpc without caFile) */
xfrpc_client_config_t cfg2 = {
    .server_addr = "203.0.113.10",
    .tls_enable  = 1,
};
```

`tls_server_name` sets both the SNI extension and the name the certificate is
checked against; it defaults to `server_addr` and is skipped automatically when
the address is a literal IP.

Mutual TLS (`frps` with `transport.tls.certFile`/`keyFile`) uses
`tls_cert_pem` + `tls_key_pem`.

Two notes:

- **Certificate validity dates are not checked** on the ESP32: `ESP-IDF` ships
  mbedtls without `MBEDTLS_HAVE_TIME_DATE`, and the system clock is at 1970
  until SNTP syncs. If you need expiry checking, sync SNTP before calling
  `xfrpc_start()`. Chain, signature and hostname are checked either way.
- The PEM strings are compiled in (Kconfig) or copied from the caller (API) —
  no filesystem is required. `tls_trusted_ca_file` / `tls_cert_file` /
  `tls_key_file` are still honoured as fallbacks if a filesystem is mounted.

## Roadmap

1. UDP proxy (`proxy_udp.c`).
2. `stcp` / visitor / `xtcp` NAT hole punching.
3. HTTP/HTTPS proxy type with `custom_domains` / `subdomain`.
4. QUIC transport (needs ngtcp2).

## License and credits

- This component is licensed under **GPL-3.0-only**, inherited from upstream
  xfrpc. See [LICENSE](LICENSE).
- Upstream xfrpc: <https://github.com/liudf0716/xfrpc> — Copyright (c) Dengfeng Liu
  and contributors. The files under `components/xfrpc/src/` are his work, adapted
  for ESP-IDF where marked.
- frp protocol: <https://github.com/fatedier/frp>.
- Vendored third-party code: [snappy](https://github.com/google/snappy) and
  [uthash](https://github.com/troydhanson/uthash), both under their own licenses.