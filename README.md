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
| `use_compression` (snappy) | Yes |
| Heartbeat / automatic reconnect | Yes |
| Programmatic configuration via public API | Yes |
| TLS control connection | Not yet (stub) |
| UDP proxy | Not yet (stub) |
| `stcp` / visitor / `xtcp` (P2P) | Not yet (stub) |
| HTTP/HTTPS proxy type, custom domains | Not yet (stub) |
| `transport.wireProtocol = v2` | Source present, not enabled |
| QUIC transport | Not yet (stub, no ngtcp2) |
| kcp / websocket / wss | No |

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

TCP proxy, AES encryption, snappy compression and `tcp_mux` are the protocol MVP and
are always compiled in; `tcp_mux`, `use_encryption` and `use_compression` can be
disabled per proxy at runtime through the API. Compile-time switches for the
optional modules (TLS, UDP proxy, visitor, `xtcp`, plugins) will be added together
with the corresponding ports.

## Roadmap

1. TLS control connection (`tls.c` over mbedtls).
2. UDP proxy (`proxy_udp.c`).
3. `stcp` / visitor / `xtcp` NAT hole punching.
4. HTTP/HTTPS proxy type with `custom_domains` / `subdomain`.
5. Kconfig switches to trim unused modules per build.
6. `transport.wireProtocol = v2` support.

## License and credits

- This component is licensed under **GPL-3.0-only**, inherited from upstream
  xfrpc. See [LICENSE](LICENSE).
- Upstream xfrpc: <https://github.com/liudf0716/xfrpc> — Copyright (c) Dengfeng Liu
  and contributors. The files under `components/xfrpc/src/` are his work, adapted
  for ESP-IDF where marked.
- frp protocol: <https://github.com/fatedier/frp>.
- Vendored third-party code: [snappy](https://github.com/google/snappy) and
  [uthash](https://github.com/troydhanson/uthash), both under their own licenses.