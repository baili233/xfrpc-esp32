# ESP32 HTTP tunnel example

[English](README.md) | [简体中文](README_zh.md)

Publishes a web server running **on the ESP32 itself** to the internet through
`frps`, using the `xfrpc` component. This is the end-to-end smoke test for the port:
if the page loads in a browser, then WiFi, the LAN TCP client, the frp control
protocol, login, `tcp_mux` framing and the proxy data path all work.

```
browser ──► frps:<remote_port> ──► ESP32 (xfrpc) ──► 127.0.0.1:<local_port>
                                                      (http_server.c)
```

## What the example does

`main/main.c` runs three steps at boot:

1. `app_wifi_connect()` — joins the AP configured in menuconfig and waits for an IP
   address (30 s timeout, 10 retries).
2. `http_server_start()` — starts a small `lwIP` socket server that answers every
   request with the same static page. It stands in for "the service you actually
   want to expose".
3. `xfrpc_start()` — connects to `frps`, logs in with the configured token, and
   registers one TCP proxy from `127.0.0.1:<local_port>` to
   `frps:<remote_port>`.

State transitions from the component are printed by the `on_xfrpc_state()` callback
(`connecting`, `connected`, `login OK`, `reconnecting`, `stopped`, `fatal`).

## Prerequisites

- ESP-IDF v5.5 exported in the shell (`idf.py` on `PATH`) — on Windows, run the
  ESP-IDF `export.bat` / `export.ps1` first.
- A reachable `frps` server with `tcp_mux` enabled (the default in recent versions).
  The server must allow the token you configure.
- The `remote_port` you pick must be free on the `frps` host and allowed by its
  `allow_ports` setting, if any.

## Configure

```bash
idf.py set-target esp32s3     # or esp32, esp32c3, ... with enough RAM
idf.py menuconfig
```

Under **xfrpc example configuration**:

| Option | Default | Description |
| ------ | ------- | ----------- |
| `EXAMPLE_WIFI_SSID` | `myssid` | AP to join. |
| `EXAMPLE_WIFI_PASSWORD` | `mypassword` | AP password. |
| `EXAMPLE_FRPS_SERVER_ADDR` | `192.168.1.100` | `frps` host or IP. |
| `EXAMPLE_FRPS_SERVER_PORT` | `7000` | `frps` bind port (not the remote port). |
| `EXAMPLE_FRPS_AUTH_TOKEN` | `esp32-frpc-token` | Must match `auth.token` in `frps.toml`/`frps.ini`. |
| `EXAMPLE_FRPS_USER` | `""` | `[common]` `user`. Required by third-party providers (SakuraFrp, …): the wire-level proxy name becomes `{user}.{proxy_name}`. Empty for a self-hosted `frps`. |
| `EXAMPLE_PROXY_NAME` | `web` | Name of the proxy section (`[esp32]` in the provider panel). |
| `EXAMPLE_SNTP_SERVER` | `ntp.aliyun.com` | NTP server used to sync the clock before login (frp token auth signs a unix timestamp). |
| `EXAMPLE_HTTP_SERVER_PORT` | `8080` | Local port of the on-chip HTTP server. |
| `EXAMPLE_PROXY_REMOTE_PORT` | `6000` | Port exposed by `frps`. |

Component-level settings (task stack, priority, log level) are under **xfrpc**,
together with the optional modules — **TLS**, **wire protocol v2**, **snappy
compression** and **health check** all default to `n`. Enable only what the
`frps` you are connecting to needs; see the
[component README](../../README.md#optional-modules) for the options and the
measured size of each combination.

To connect to an `frps` with `transport.tls.force = true`, turn on
**xfrpc → TLS → Enable TLS for the frps control connection**, and paste the CA
that signed the server certificate into
**Compiled-in CA certificate (PEM)** — or call `xfrpc_start()` with
`.tls_enable = 1` and `.tls_ca_pem = ...` instead. Leaving the CA empty still
gives you an encrypted connection, it just does not authenticate the server
(the same thing `frpc` does without `caFile`).

## Third-party frp providers (SakuraFrp, …)

Panel-run `frps` instances work the same as a self-hosted server, with two
provider specifics to set:

- **`user`** — the provider account name (SakuraFrp: the `user` line of the
  downloaded `frpc.ini`). With `user` set, the frp protocol sends the proxy as
  `{user}.{proxy_name}` and the provider maps it to the tunnel created in the
  panel — the component applies this prefix and its removal automatically, so
  `EXAMPLE_PROXY_NAME` stays the tunnel name (`esp32` in the example below).
- **Clock sync** — token auth signs a unix timestamp, and providers reject
  stale ones. The ESP32 has no battery-backed clock, so the example syncs via
  SNTP (`EXAMPLE_SNTP_SERVER`) before logging in.

Mapping a SakuraFrp tunnel to the example options:

```ini
[common]
user = s-000q2qbede2ef3          # -> EXAMPLE_FRPS_USER
token = <access key>             # -> EXAMPLE_FRPS_AUTH_TOKEN
server_addr = test.u33794.nyat.app
server_port = 8088               # -> EXAMPLE_FRPS_SERVER_ADDR / _PORT

[esp32]                          # section name -> EXAMPLE_PROXY_NAME
type = tcp
local_port = 8080                # -> EXAMPLE_HTTP_SERVER_PORT
remote_port = 61698              # -> EXAMPLE_PROXY_REMOTE_PORT
```

## Build, flash, verify

```bash
idf.py build flash monitor
```

Expected log:

```
I (1234) wifi: got ip:192.168.1.50
I (1240) httpd: HTTP server listening on port 8080
I (1250) app_main: xfrpc: connecting to frps
I (1450) app_main: xfrpc: connected
I (1600) app_main: xfrpc: login OK
```

Now request the page **through the tunnel** — from any machine that can reach the
`frps` host, not from the ESP32's LAN address:

```bash
curl http://<frps-host>:6000/
```

A successful run returns:

```html
<h1>Hello from ESP32-S3 👋</h1>
```

and the ESP32 monitor prints:

```
I (2100) httpd: connection from 127.0.0.1:52344
I (2110) httpd: request: GET /
```

The source address is `127.0.0.1` because `frps` forwards the connection through
the mux stream into the local socket — the client's real address is not preserved
in this path.

## Troubleshooting

| Symptom | Likely cause |
| ------- | ------------ |
| `wifi: failed to connect to AP` | Wrong SSID/password, or 2.4 GHz-only hardware and a 5 GHz SSID. |
| `xfrpc: login OK` never appears, log shows a login error | Token mismatch with `frps`, or a `user` restriction on the server. |
| Provider rejects login, provider log mentions timestamp/auth | Clock not synced: check the `SNTP sync failed` warning and `EXAMPLE_SNTP_SERVER` reachability. |
| Provider panel shows the proxy as offline / "proxy not found" | `user` set on the server side but not here (or vice versa) — the provider matches tunnels by `{user}.{proxy_name}`. |
| `xfrpc: connection lost, reconnecting` in a loop | `frps` unreachable, port/token wrong, or the server rejected `tcp_mux = 1`. Set `.tcp_mux = 0` in `main.c` to match a server with mux disabled. |
| Browser times out but the log says `login OK` | The remote port is blocked by the `frps` firewall or `allow_ports`; test with `curl` on the `frps` host itself first. |
| `curl` returns the page, but a second concurrent request stalls | One shared stack per proxy in this example: the HTTP server handles one connection at a time. Add a per-connection task if you need concurrency. |

## Adapting it to your own service

Replace `http_server.c` with your service (or drop it and point the proxy at another
host on the LAN — `.local_ip` does not have to be `127.0.0.1`), and change the proxy
list in `main/main.c`:

```c
xfrpc_tcp_proxy_t proxies[] = {
    { .name = "web",  .local_ip = "127.0.0.1", .local_port = 80,
      .remote_port = 6000, .use_encryption = 1, .use_compression = 1 },
};
xfrpc_start(&cfg, proxies, 1, on_xfrpc_state, NULL);
```

Several proxies can be registered in the same array; each entry is one `frpc.toml`
`[[proxies]]` block. `use_encryption` / `use_compression` add AES-128-CFB and snappy
on the stream, at a CPU cost that is noticeable on an ESP32 at high throughput —
measure before enabling them for bulk transfer. `.use_compression = 1` needs
**xfrpc → Optional modules → snappy payload compression** enabled, otherwise the
component logs a warning and sends the payload uncompressed.