# xfrpc-esp32

[English](README.md) | [简体中文](README_zh.md)

把 [xfrpc](https://github.com/liudf0716/xfrpc)（[frp](https://github.com/fatedier/frp)
客户端的轻量 C 语言实现）移植到 ESP32 系列的 ESP-IDF 组件。处于 NAT 后的 ESP32 无需完整
Linux 用户态，即可把本地 TCP 服务发布到公网的 `frps` 服务器。

上游代码没有被改写：它以原样编译在组件内（`components/xfrpc/src/`），而 POSIX /
libevent / OpenSSL / json-c 这些依赖由一层薄的移植层（`components/xfrpc/port/`）用
ESP-IDF 自带的 mbedtls、cJSON 和 lwIP 实现。`src/` 中只有少量几行用 `// ESP32 port:`
标注，是必须替换掉的 Linux 专有调用。

## 功能支持

| 功能 | 状态 |
| ---- | ---- |
| TCP 代理 | 支持 |
| `tcp_mux`（控制连接多路复用） | 支持，默认开启 |
| `use_encryption`（AES-128-CFB 流加密） | 支持 |
| `use_compression`（snappy 压缩） | 支持（编译期可选） |
| 心跳 / 自动重连 | 支持 |
| 通过公共 API 编程配置 | 支持 |
| TLS 控制连接 | 支持（mbedtls，编译期可选），含 mTLS |
| UDP 代理 | 暂未支持（占位实现） |
| `stcp` / visitor / `xtcp`（P2P） | 暂未支持（占位实现） |
| HTTP/HTTPS 代理类型、自定义域名 | 暂未支持（占位实现） |
| `transport.wireProtocol = v2` | 支持（编译期可选） |
| QUIC 传输 | 暂未支持（无 ngtcp2） |
| kcp / websocket / wss | 不支持 |

TLS、wire protocol v2、snappy 和健康检查各自有一个 Kconfig 开关，且**默认全部关闭**——
默认构建即体积最小的构建，按 `frps` 的实际需要打开，只付出对应模块的体积
（见[配置项](#配置项)）。

`frps` 服务端也需要开启 `tcp_mux`，新版 `frps` 默认即开启。

## 目录结构

```
xfrpc-esp32/
├── components/
│   └── xfrpc/                 ESP-IDF 组件
│       ├── include/xfrpc.h    公共 API
│       ├── src/               上游 xfrpc 核心（版权见上游 LICENSE）
│       ├── port/              ESP32 移植层
│       │   ├── mini_event.c   libevent 最小替代实现
│       │   ├── mini_event_ssl.c  bufferevent 的 mbedtls 后端（TLS）
│       │   ├── tls.c          控制连接的 mbedtls TLS 实现（TLS）
│       │   ├── wire_v2_off.c  关闭 v2 时的 wire_protocol_is_v2() == 0
│       │   ├── openssl_compat.c  OpenSSL EVP/HMAC/MD5 -> mbedtls
│       │   ├── json_compat.c  json-c -> cJSON
│       │   ├── esp_platform.c uname()、致命错误处理、命令行/syslog 占位
│       │   ├── module_stubs.c 未编译模块的 weak 占位
│       │   └── xfrpc_api.c    xfrpc_start()/xfrpc_stop() 实现
│       ├── vendor/            snappy、uthash
│       ├── CMakeLists.txt
│       └── Kconfig
├── examples/
│   └── esp32_http_tunnel/     独立 ESP-IDF 工程
└── LICENSE                    GPL-3.0-only（继承自上游）
```

## 环境要求

- ESP-IDF **v5.5**（开发与验证所用版本；v5.1 及以上大概率可用）
- 有足够 RAM 运行 xfrpc 任务的目标芯片（默认任务栈 16 KB）
- 一个 ESP32 可访问的 `frps` 服务器，协议版本 0.71.x

示例默认目标为 `esp32s3`；换其他芯片时在示例目录执行
`idf.py set-target <target>` 即可。

## 快速开始

示例会连接 WiFi、在本地端口上提供一个简单的 HTTP 页面，并通过 `frps` 把该端口暴露出去。

```bash
cd examples/esp32_http_tunnel

idf.py set-target esp32s3
idf.py menuconfig     # xfrpc example configuration -> 填 WiFi SSID/密码、frps 地址
idf.py build flash monitor
```

然后在任意能访问该 `frps` 的机器上执行：

```bash
curl http://<frps-host>:6000/
```

能拿到页面，说明 `frps -> ESP32 -> 本地 HTTP 服务` 这条链路已经打通。更多细节见
[examples/esp32_http_tunnel/README.md](examples/esp32_http_tunnel/README.md)。

Windows 下请先在已导出 ESP-IDF 环境的终端里构建（运行 ESP-IDF 安装目录下的
`export.bat` / `export.ps1`，使 `idf.py` 进入 `PATH`）：

```bash
idf.py build
```

## 在自己的工程中使用本组件

把 `components/xfrpc/` 拷进你工程的 `components/` 目录，或在工程 `CMakeLists.txt` 中指向它：

```cmake
set(EXTRA_COMPONENT_DIRS "/path/to/xfrpc-esp32/components")
```

组件以 `XFRPC_DEBUG` 宏编译，日志走 `esp_log`，运行时详细程度由
`CONFIG_XFRPC_LOG_LEVEL` 控制。

## API 用法

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

`xfrpc_start()` 会复制所有字符串字段，因此调用方的缓冲区可以放在栈上。它会创建一个独立的
FreeRTOS 任务并立即返回：

| 返回值 | 含义 |
| ------ | ---- |
| `0`  | 任务已启动 |
| `1`  | 已在运行 |
| `-1` | 配置非法，或任务创建失败 |

其余接口：

| 函数 | 说明 |
| ---- | ---- |
| `xfrpc_stop(void)` | 请求优雅停止，任务异步退出；可在任意任务中调用。 |
| `xfrpc_is_connected(void)` | 控制连接已建立并登录成功时为 `true`。 |

状态变化通过 `xfrpc_start()` 传入的回调上报，回调运行在 xfrpc 事件任务上下文中：

| 状态 | 含义 |
| ---- | ---- |
| `XFRPC_STATE_CONNECTING` | 任务已启动，正在连接 `frps` |
| `XFRPC_STATE_CONNECTED` | TCP 连接已建立 |
| `XFRPC_STATE_LOGIN_OK` | `frps` 已接受登录 |
| `XFRPC_STATE_RECONNECTING` | 控制连接断开，正在重连 |
| `XFRPC_STATE_FATAL` | 不可恢复错误，任务已退出 |
| `XFRPC_STATE_STOPPED` | `xfrpc_stop()` 完成，任务已退出 |

## 配置项

组件选项（`idf.py menuconfig` → `xfrpc`）：

| 选项 | 默认值 | 说明 |
| ---- | ------ | ---- |
| `CONFIG_XFRPC_TASK_STACK_SIZE` | 16384 | xfrpc 事件循环任务栈大小。 |
| `CONFIG_XFRPC_TASK_PRIORITY` | 5 | 该任务优先级。 |
| `CONFIG_XFRPC_LOG_LEVEL` | 6 | syslog 风格日志级别，`0`=emerg … `7`=debug。 |

### 可选模块

这些开关决定源码是否参与编译：关闭时对应源码完全不编译，由占位实现（wire v2 是
`port/wire_v2_off.c` 里返回常量 0 的 `wire_protocol_is_v2()`）顶替，链接器会把整个子系统丢掉。
全部默认 `n`。

| 选项 | 默认值 | 说明 |
| ---- | ------ | ---- |
| `CONFIG_XFRPC_ENABLE_TLS` | `n` | 控制连接的 mbedtls TLS 传输，会 `select MBEDTLS_TLS_CLIENT`。 |
| `CONFIG_XFRPC_TLS_CA_PEM` | `""` | 签发 `frps` 证书的 CA（PEM）。留空则**不校验**对端证书，与 `frpc` 未配 `caFile` 时一致。 |
| `CONFIG_XFRPC_TLS_CERT_PEM` | `""` | 客户端证书 PEM（mTLS）。 |
| `CONFIG_XFRPC_TLS_KEY_PEM` | `""` | 客户端私钥 PEM（mTLS）。 |
| `CONFIG_XFRPC_TLS_HANDSHAKE_TIMEOUT` | 10 | 握手超过该秒数即中断并重连。 |
| `CONFIG_XFRPC_ENABLE_WIRE_V2` | `n` | `frp` wire protocol v2（`frps` 设 `transport.protocol = "v2"`）。 |
| `CONFIG_XFRPC_ENABLE_COMPRESSION` | `n` | 为 `use_compression = 1` 的代理编译 snappy 压缩。 |
| `CONFIG_XFRPC_ENABLE_HEALTH_CHECK` | `n` | 本地服务周期性健康检查。 |

在示例工程（ESP32-S3、`esp32_http_tunnel`、`idf.py build`）上实测的固件体积：

| TLS | wire v2 | 压缩 | 健康检查 | app 体积 | 1 MB 分区剩余 |
| --- | ------- | ---- | -------- | -------- | ------------- |
| `n` | `n` | `n` | `n` | 879,392 B (0xd6b20) | 16% |
| `n` | `y` | `y` | `y` | 897,904 B (0xdb370) | 14% |
| `y` | `n` | `n` | `n` | 956,864 B (0xe99c0) | 9% |
| `y` | `y` | `y` | `y` | 975,248 B (0xee190) | 7% |

全关档即新的默认配置，也是最小的一档；全开档在 1 MB app 分区内仍余 7%。
TLS 之所以是后两行之间主要差异，是因为它把一个 mbedtls TLS client 拉进固件。

TCP 代理、AES 加密和 `tcp_mux` 属于协议 MVP，始终参与编译；
`tcp_mux`、`use_encryption`、`use_compression` 可在运行时按代理通过 API 关闭。

## TLS

`frps` 在 `transport.tls.force` 打开时会对每条连接做 TLS 识别；即使不开，服务端同样会识别
TLS 连接、只是对非 TLS 客户端继续走明文。所以下面两种写法都能用：

```c
/* 用 CA 校验服务端证书 */
static const char ca_pem[] = "-----BEGIN CERTIFICATE-----\n...\n";
xfrpc_client_config_t cfg = {
    .server_addr = "frps.example.com",
    .tls_enable  = 1,
    .tls_ca_pem  = ca_pem,
};

/* 不配 CA：流量加密，但不认证对端（等同 frpc 未配 caFile） */
xfrpc_client_config_t cfg2 = {
    .server_addr = "203.0.113.10",
    .tls_enable  = 1,
};
```

`tls_server_name` 同时决定 SNI 扩展和证书校验用的名字，默认取 `server_addr`；当地址是字面
IP 时会自动跳过（mbedtls 的 `set_hostname` 只接受域名）。

双向 TLS（`frps` 配了 `transport.tls.certFile`/`keyFile`）用 `tls_cert_pem` +
`tls_key_pem`。

两点说明：

- **ESP32 上不校验证书有效期**：ESP-IDF 的 mbedtls 没开 `MBEDTLS_HAVE_TIME_DATE`，
  且系统时间在 SNTP 同步前是 1970。若需要校验证书有效期，请在调用 `xfrpc_start()` 之前完成
  SNTP 同步。证书链、签名和主机名的校验不受影响。
- PEM 字符串既可以编译进去（Kconfig），也可以由调用方传入（API），**不依赖文件系统**。
  挂了文件系统时，`tls_trusted_ca_file` / `tls_cert_file` / `tls_key_file` 仍可作为回退。

## 后续计划

1. UDP 代理（`proxy_udp.c`）。
2. `stcp` / visitor / `xtcp` NAT 打洞。
3. HTTP/HTTPS 代理类型与 `custom_domains` / `subdomain`。
4. QUIC 传输（需要 ngtcp2）。

## 许可证与致谢

- 本组件遵循上游 xfrpc 的 **GPL-3.0-only** 许可，见 [LICENSE](LICENSE)。
- 上游 xfrpc：<https://github.com/liudf0716/xfrpc>，版权归 Dengfeng Liu 及贡献者所有。
  `components/xfrpc/src/` 下的文件为其作品，标注处为适配 ESP-IDF 所做的修改。
- frp 协议：<https://github.com/fatedier/frp>。
- 内置第三方代码：[snappy](https://github.com/google/snappy) 与
  [uthash](https://github.com/troydhanson/uthash)，各自遵循其原始许可。