# ESP32 HTTP 隧道示例

[English](README.md) | [简体中文](README_zh.md)

用 `xfrpc` 组件把**运行在 ESP32 上**的 Web 服务通过 `frps` 发布到公网。这也是移植的端到端
冒烟测试：浏览器能打开页面，就说明 WiFi、局域网 TCP 客户端、frp 控制协议、登录、`tcp_mux`
分帧和代理数据通道都是通的。

```
浏览器 ──► frps:<remote_port> ──► ESP32 (xfrpc) ──► 127.0.0.1:<local_port>
                                                    (http_server.c)
```

## 示例流程

`main/main.c` 在启动时做三件事：

1. `app_wifi_connect()` —— 连接 menuconfig 中配置的 AP，并等待拿到 IP（超时 30 秒，重试 10 次）。
2. `http_server_start()` —— 启动一个基于 lwIP socket 的小 HTTP 服务，对任何请求都返回同一个
   静态页面，用来代替“你真正想暴露的那个服务”。
3. `xfrpc_start()` —— 连接 `frps`、用配置的 token 登录，并注册一条把
   `127.0.0.1:<local_port>` 映射到 `frps:<remote_port>` 的 TCP 代理。

组件的状态变化由 `on_xfrpc_state()` 回调打印（`connecting`、`connected`、`login OK`、
`reconnecting`、`stopped`、`fatal`）。

## 前置条件

- shell 中已导出 ESP-IDF v5.5（`idf.py` 在 `PATH` 中）；Windows 下需先运行 ESP-IDF
  的 `export.bat` / `export.ps1`。
- 一个可访问的 `frps` 服务器，且已开启 `tcp_mux`（新版默认开启），并允许你配置的 token。
- 所选 `remote_port` 在 `frps` 主机上未被占用，且在其 `allow_ports` 允许范围内（若配置了）。

## 配置

```bash
idf.py set-target esp32s3     # 或 RAM 足够的 esp32 / esp32c3 / ...
idf.py menuconfig
```

在 **xfrpc example configuration** 菜单下：

| 选项 | 默认值 | 说明 |
| ---- | ------ | ---- |
| `EXAMPLE_WIFI_SSID` | `myssid` | 要连接的 AP。 |
| `EXAMPLE_WIFI_PASSWORD` | `mypassword` | AP 密码。 |
| `EXAMPLE_FRPS_SERVER_ADDR` | `192.168.1.100` | `frps` 主机名或 IP。 |
| `EXAMPLE_FRPS_SERVER_PORT` | `7000` | `frps` 的 bindPort（不是 remote port）。 |
| `EXAMPLE_FRPS_AUTH_TOKEN` | `esp32-frpc-token` | 必须与 `frps.toml`/`frps.ini` 里的 `auth.token` 一致。 |
| `EXAMPLE_HTTP_SERVER_PORT` | `8080` | 片上 HTTP 服务的本地端口。 |
| `EXAMPLE_PROXY_REMOTE_PORT` | `6000` | 由 `frps` 暴露出去的端口。 |

组件级设置（任务栈、优先级、日志级别）在 **xfrpc** 菜单下，可选模块 **TLS**、
**wire protocol v2**、**snappy 压缩**、**健康检查** 也在这里，且默认全部为 `n`。
按所连 `frps` 的实际需要打开即可；各选项含义与实测体积见
[组件 README](../../README_zh.md#可选模块)。

要连接开了 `transport.tls.force = true` 的 `frps`，请打开
**xfrpc → TLS → Enable TLS for the frps control connection**，并把签发服务端证书的 CA
粘贴到 **Compiled-in CA certificate (PEM)**；也可以不编译进去，改为在 `xfrpc_start()`
时传 `.tls_enable = 1` 与 `.tls_ca_pem = ...`。CA 留空时连接同样是加密的，只是不认证
服务端身份（等同 `frpc` 未配 `caFile`）。

## 编译、烧录、验证

```bash
idf.py build flash monitor
```

预期日志：

```
I (1234) wifi: got ip:192.168.1.50
I (1240) httpd: HTTP server listening on port 8080
I (1250) app_main: xfrpc: connecting to frps
I (1450) app_main: xfrpc: connected
I (1600) app_main: xfrpc: login OK
```

接着**通过隧道**访问页面 —— 从任意能访问 `frps` 主机的机器上发起，而不是访问 ESP32 的局域网地址：

```bash
curl http://<frps-host>:6000/
```

成功时会返回：

```html
<h1>Hello from ESP32-S3 👋</h1>
```

同时 ESP32 串口会打印：

```
I (2100) httpd: connection from 127.0.0.1:52344
I (2110) httpd: request: GET /
```

源地址是 `127.0.0.1`，因为 `frps` 是通过 mux 流转发到本地 socket 的，这条路径上不会保留
客户端的真实地址。

## 常见问题

| 现象 | 可能原因 |
| ---- | -------- |
| `wifi: failed to connect to AP` | SSID/密码错误，或芯片只支持 2.4 GHz 而连的是 5 GHz 热点。 |
| 一直看不到 `xfrpc: login OK`，日志报登录失败 | token 与 `frps` 不一致，或服务端对该 `user` 有限制。 |
| 循环打印 `xfrpc: connection lost, reconnecting` | `frps` 不可达、端口/token 不对，或服务端拒绝 `tcp_mux = 1`（可在 `main.c` 中把 `.tcp_mux` 设为 0 以匹配关闭了 mux 的服务端）。 |
| 日志显示 `login OK`，但浏览器超时 | `remote_port` 被 `frps` 防火墙或 `allow_ports` 拦住；先在 `frps` 主机上本地 `curl` 验证。 |
| `curl` 能拿到页面，但第二个并发请求卡住 | 本示例的 HTTP 服务一次只处理一个连接；需要并发时请为每个连接单独建任务。 |

## 换成自己的服务

把 `http_server.c` 换成你的服务即可（也可以直接删掉它，让代理指向局域网内另一台主机 ——
`.local_ip` 不一定是 `127.0.0.1`），然后修改 `main/main.c` 中的代理列表：

```c
xfrpc_tcp_proxy_t proxies[] = {
    { .name = "web",  .local_ip = "127.0.0.1", .local_port = 80,
      .remote_port = 6000, .use_encryption = 1, .use_compression = 1 },
};
xfrpc_start(&cfg, proxies, 1, on_xfrpc_state, NULL);
```

同一个数组里可以注册多条代理，每条对应 `frpc.toml` 中的一个 `[[proxies]]` 块。
`use_encryption` / `use_compression` 会分别在流上叠加 AES-128-CFB 和 snappy，在 ESP32 上做大
流量传输时 CPU 开销比较明显，建议先实测再决定是否开启。`.use_compression = 1` 需要先打开
**xfrpc → Optional modules → snappy payload compression**，否则组件会打一条警告并按未压缩发送。