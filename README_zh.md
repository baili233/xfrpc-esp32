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
| `use_compression`（snappy 压缩） | 支持 |
| 心跳 / 自动重连 | 支持 |
| 通过公共 API 编程配置 | 支持 |
| TLS 控制连接 | 暂未支持（占位实现） |
| UDP 代理 | 暂未支持（占位实现） |
| `stcp` / visitor / `xtcp`（P2P） | 暂未支持（占位实现） |
| HTTP/HTTPS 代理类型、自定义域名 | 暂未支持（占位实现） |
| `transport.wireProtocol = v2` | 源码已在，尚未启用 |
| QUIC 传输 | 暂未支持（无 ngtcp2） |
| kcp / websocket / wss | 不支持 |

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

TCP 代理、AES 加密、snappy 压缩和 `tcp_mux` 属于协议 MVP，始终参与编译；
`tcp_mux`、`use_encryption`、`use_compression` 可在运行时按代理通过 API 关闭。
可选模块（TLS、UDP 代理、visitor、`xtcp`、插件）的编译期开关会随对应移植一起加入。

## 后续计划

1. TLS 控制连接（`tls.c` 基于 mbedtls）。
2. UDP 代理（`proxy_udp.c`）。
3. `stcp` / visitor / `xtcp` NAT 打洞。
4. HTTP/HTTPS 代理类型与 `custom_domains` / `subdomain`。
5. 用 Kconfig 按需裁剪未使用的模块。
6. 支持 `transport.wireProtocol = v2`。

## 许可证与致谢

- 本组件遵循上游 xfrpc 的 **GPL-3.0-only** 许可，见 [LICENSE](LICENSE)。
- 上游 xfrpc：<https://github.com/liudf0716/xfrpc>，版权归 Dengfeng Liu 及贡献者所有。
  `components/xfrpc/src/` 下的文件为其作品，标注处为适配 ESP-IDF 所做的修改。
- frp 协议：<https://github.com/fatedier/frp>。
- 内置第三方代码：[snappy](https://github.com/google/snappy) 与
  [uthash](https://github.com/troydhanson/uthash)，各自遵循其原始许可。