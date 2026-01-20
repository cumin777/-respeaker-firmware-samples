# nRF Connect SDK Wi‑Fi AP / SoftAP 速查（NCS v3.2.1）

本文目的：把“在 NCS/Zephyr 中如何开启 Wi‑Fi 热点（AP/SoftAP）”所需的 API、事件、关键结构体与参考样例做成可长期复用的索引。

## 1) API 入口：`net_mgmt` + `wifi_mgmt`

核心头文件：
- `zephyr/include/zephyr/net/net_mgmt.h`
- `zephyr/include/zephyr/net/wifi_mgmt.h`

AP 相关请求（`wifi_mgmt.h`）：
- `NET_REQUEST_WIFI_AP_ENABLE`：开启 AP
- `NET_REQUEST_WIFI_AP_DISABLE`：关闭 AP
- `NET_REQUEST_WIFI_AP_STA_DISCONNECT`：踢掉某个 STA
- `NET_REQUEST_WIFI_AP_CONFIG_PARAM`：配置 AP 参数（高级）
- `NET_REQUEST_WIFI_AP_RTS_THRESHOLD`：RTS threshold（高级）

AP 相关事件（`wifi_mgmt.h`）：
- `NET_EVENT_WIFI_AP_ENABLE_RESULT`：AP enable 结果
- `NET_EVENT_WIFI_AP_DISABLE_RESULT`：AP disable 结果
- `NET_EVENT_WIFI_AP_STA_CONNECTED`：有 STA 连入
- `NET_EVENT_WIFI_AP_STA_DISCONNECTED`：有 STA 断开

事件 payload 类型：
- `NET_EVENT_WIFI_AP_*_RESULT`：`struct wifi_status`（其 union 字段可用 `.status` 或 `.ap_status`）
- `NET_EVENT_WIFI_AP_STA_*`：`struct wifi_ap_sta_info`

## 2) 关键结构体：`struct wifi_connect_req_params`（STA/AP 共用）

在 `wifi_mgmt.h` 中，STA 连接与 AP enable 都复用 `struct wifi_connect_req_params`：
- SSID：`ssid` + `ssid_length`
- PSK：`psk` + `psk_length`（开放网络可不设置）
- 频段/信道：`band` + `channel`
- 安全：`security`（例如 `WIFI_SECURITY_TYPE_NONE` / `WIFI_SECURITY_TYPE_PSK`）
- 隐藏 SSID：`ignore_broadcast_ssid`

注意：同一个结构体里还包含 Enterprise(EAP) 相关字段，做基础 SoftAP demo 一般不用。

## 3) 最小 AP demo 流程（推荐按官方样例写法）

### 3.1 获取 Wi‑Fi 接口（net_if）
常见两种方式：
- 单接口场景：`net_if_get_first_wifi()`
- AP-STA 双接口场景：
  - AP：`net_if_get_wifi_sap()`
  - STA：`net_if_get_wifi_sta()`

### 3.2 注册 Wi‑Fi 管理事件回调
典型：
- `net_mgmt_init_event_callback(&cb, handler, EVENT_MASK)`
- `net_mgmt_add_event_callback(&cb)`

`EVENT_MASK` 至少包含：
- `NET_EVENT_WIFI_AP_ENABLE_RESULT`
- `NET_EVENT_WIFI_AP_STA_CONNECTED`
- `NET_EVENT_WIFI_AP_STA_DISCONNECTED`

### 3.3 填充 AP 参数并发起请求
- 填 `struct wifi_connect_req_params ap_params = {0};`
- 设置 `ssid/psk/security/band/channel` 等
- 可选：`wifi_utils_validate_chan(band, channel)` 校验信道（见官方 softap sample）
- 调用：
  - `net_mgmt(NET_REQUEST_WIFI_AP_ENABLE, iface, &ap_params, sizeof(ap_params))`

### 3.4 给 SoftAP 配 IP + DHCP（让手机真正“拿到地址”）
AP enable 只负责把 AP 开起来；要让手机获取 IP，通常还要：
- 给 AP 接口配置静态 IPv4（网关/掩码/IP）
- 启动 DHCPv4 server：`net_dhcpv4_server_start(iface, &pool_start)`

官方样例把 DHCPv4 server 作为 sample Kconfig 选项启用（见下方“参考样例”）。

## 4) SoftAP Provisioning（配网库，不同于“仅开热点”）

如果你的目标是“设备开一个 SoftAP + HTTPS 接口，让手机把目标路由器 SSID/密码发给设备，然后设备切换成 STA 去连路由器”，NCS 里有封装库：

头文件：`nrf/include/net/softap_wifi_provision.h`
- `softap_wifi_provision_init(handler)`
- `softap_wifi_provision_start()`（阻塞直到完成）
- `softap_wifi_provision_reset()`

事件：`SOFTAP_WIFI_PROVISION_EVT_*`（STARTED/CLIENT_CONNECTED/CREDENTIALS_RECEIVED/COMPLETED 等）。

## 5) `wifi_ready`（Nordic 库：等待 Wi‑Fi 可用）

NCS 里有一个轻量库用于在 Wi‑Fi 驱动/子系统“ready”时回调：

头文件：`nrf/include/net/wifi_ready.h`
- `register_wifi_ready_callback(...)`
- `unregister_wifi_ready_callback(...)`

官方 SoftAP sample 会先等 Wi‑Fi ready，再走 AP enable / DHCP 等流程。

## 6) 参考样例（强烈建议先跑通，再做定制）

### 5.1 仅 SoftAP
- `nrf/samples/wifi/softap/`
  - `src/main.c`：AP enable、站点连接事件、DHCP server
  - `prj.conf`：Wi‑Fi/Nrf70/AP mode/WPA supplicant 等关键 Kconfig
  - `Kconfig`：示例配置项（SSID/信道/密码/是否启用 DHCP server）

### 5.2 AP + STA 同时工作
- `zephyr/samples/net/wifi/apsta_mode/src/main.c`
  - 展示 `net_if_get_wifi_sap()` + `net_if_get_wifi_sta()` 的分离用法

### 5.3 shell 参考（可当“最小 AP enable”模板）
- `zephyr/subsys/net/l2/wifi/wifi_shell.c`
  - `cmd_wifi_ap_enable()` / `cmd_wifi_ap_disable()` 调用 `NET_REQUEST_WIFI_AP_ENABLE/AP_DISABLE`

## 7) “坑点”提示（基于源码观察）
- AP enable 的参数结构体是 `wifi_connect_req_params`（不是单独的 `wifi_ap_config_params`），写 demo 时别走偏。
- 事件回调里的 `cb->info` 必须按事件类型 cast 成正确结构体。
- AP 起来以后，手机能连上但拿不到 IP，大概率是 DHCP/静态 IP 没配置好（不是 AP enable 本身的问题）。
