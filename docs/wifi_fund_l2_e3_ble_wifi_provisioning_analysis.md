# Wi‑Fi Fundamentals L2 E3（Solution）BLE 配网 + Wi‑Fi 连接：代码级解读

> 目标：解读 `C:\ncs\v3.2.1\wifi-fund\l2\l2_e3_sol` 例程中“使用蓝牙配置 Wi‑Fi 凭据并完成 Wi‑Fi 连接”的完整链路。

---

## 0. 这个例程“到底做了什么”

一句话：
- 设备开机后通过 **BLE 广播一个 Wi‑Fi Provisioning Service**；手机连上后用 **GATT 写入（加密）protobuf 请求**，把 SSID/密码等信息发给设备；
- 设备侧 `wifi_prov_core` 把 protobuf 解码成 `wifi_connect_req_params`，直接发起 `NET_REQUEST_WIFI_CONNECT` 去连 AP；
- 连接结果/扫描结果会被重新编码成 protobuf，通过 **notify**（Data Out 特征）发回给手机；
- 连上后把凭据存进 `wifi_credentials`（本例程板级选择 PSA 后端），下次启动由应用调用 `NET_REQUEST_WIFI_CONNECT_STORED` 自动重连。

---

## 1. 关键模块与文件（从应用到库）

### 1.1 应用侧（你编写/可修改的部分）
- `wifi-fund/l2/l2_e3_sol/src/main.c`
  - 启动 BLE、配置广告包、跑一个工作队列周期性更新广告里的“配网状态/连接状态/RSSI”。
  - 启动 `wifi_prov_init()`（注意：它实际在 `wifi_prov_core` 里实现）。
  - 启动时调用 `NET_REQUEST_WIFI_CONNECT_STORED`：如果之前已经配过网，开机自动连。

### 1.2 BLE 传输层（GATT 服务定义）
- `nrf/subsys/bluetooth/services/wifi_prov/wifi_prov_ble.c`
  - 定义 128-bit UUID 的 Wi‑Fi Provisioning GATT Service。
  - **Control Point** 特征：手机写入请求（`BT_GATT_PERM_WRITE_ENCRYPT`）。
  - **Data Out** 特征：设备 notify 发送扫描/连接状态结果。
  - `wifi_prov_send_rsp()` 用 **indicate** 返回 Response。
  - `wifi_prov_send_result()` 用 **notify** 推送 Result。

### 1.3 配网核心（protobuf 解码、Wi‑Fi connect、凭据存储）
- `nrf/subsys/net/lib/wifi_prov_core/wifi_prov_handler.c`
  - `wifi_prov_recv_req()`：解码 protobuf `Request`，按 op_code 分发。
  - `prov_set_config_handler()`：把 SSID/密码等转成 `wifi_connect_req_params`，直接 `NET_REQUEST_WIFI_CONNECT`。
  - `handle_wifi_scan_result()`：把 `NET_EVENT_WIFI_SCAN_RESULT` 编码成 protobuf `Result` 发回手机。
  - `handle_wifi_connect_result()`：把 `NET_EVENT_WIFI_CONNECT_RESULT` 编码成 protobuf `Result` 发回手机，并在成功时写入 `wifi_credentials`。
  - `wifi_prov_state_get()`：判断是否已配网（flash 的 `wifi_credentials` 或 RAM 的 `config_in_ram`）。

---

## 2. 数据链（Data Plane）：端到端数据怎么流

这里把“手机看到的字节”到“Wi‑Fi 子系统用到的结构体”完整串起来：

### 2.1 广播数据：设备 → 手机（无连接）

应用侧 `main.c` 广播内容分两块：

1) Advertising packet（`ad[]`）
- Flags
- 128-bit Service UUID：`BT_UUID_PROV_VAL`
- Device Name：形如 `PVxxxxxxxx`
  - `PV` 固定前缀
  - `xxxxxx` 来自网卡 MAC 的后 3 字节（hex）

2) Scan Response（`sd[]`）
- Service Data 128：`prov_svc_data[]`

`prov_svc_data[]` 的布局（来自 `main.c` 的索引宏）：
- `[0..15]`：Service UUID（`BT_UUID_PROV_VAL`）
- `[16]`：`PROV_SVC_VER`（服务版本，当前为 `0x02`）
- `[17]`：flags
  - bit0：Provisioned（`wifi_prov_state_get()`）
  - bit1：Wi‑Fi Connected（通过 `NET_REQUEST_WIFI_IFACE_STATUS` 判断状态 >= `WIFI_STATE_ASSOCIATED`）
- `[19]`：RSSI（Wi‑Fi 已关联时填 `status.rssi`，否则 `INT8_MIN`）

这块数据的意义：
- 手机无需连接就能判断：设备是否已经配网、Wi‑Fi 是否已连上、信号强弱。

### 2.2 配网请求：手机 → 设备（BLE GATT + protobuf）

GATT 服务（`wifi_prov_ble.c`）暴露 3 个特征：
- `BT_UUID_PROV_INFO`：read，返回 Info protobuf
- `BT_UUID_PROV_CONTROL_POINT`：write + indicate（加密写入）
- `BT_UUID_PROV_DATA_OUT`：notify（推送结果）

手机把 protobuf `Request` 写到 Control Point：
- `wifi_prov_ble.c::write_prov_control_point()` 收到字节流 → `wifi_prov_recv_req()`
- `wifi_prov_handler.c::wifi_prov_recv_req()` 用 nanopb 解码 `Request_fields`
- 根据 `req.op_code` 调用对应 handler

支持的 op_code（本实现里限定为 5 个）：
- `GET_STATUS`
- `START_SCAN`
- `STOP_SCAN`
- `SET_CONFIG`
- `FORGET_CONFIG`

**最关键的是 `SET_CONFIG`：**
- `prov_set_config_handler()` 把 protobuf 中的 `ssid/bssid/passphrase/auth/band/volatileMemory` 映射到：
  - `struct wifi_credentials_personal config`（用于后续存储）
  - `struct wifi_connect_req_params cnx_params`（用于立刻发起连接）
- 然后调用：
  - `net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &cnx_params, sizeof(cnx_params))`

也就是说：
- **连 Wi‑Fi 的动作不是应用 main.c 做的，是 `wifi_prov_core` 收到 SET_CONFIG 后直接做的。**

### 2.3 扫描结果/连接结果：设备 → 手机（notify 的 Result protobuf）

- 手机发 `START_SCAN` 后：
  - 触发 `NET_REQUEST_WIFI_SCAN`
  - 扫描过程中每个 `NET_EVENT_WIFI_SCAN_RESULT` 事件 → `handle_wifi_scan_result()`
  - 逐条编码成 protobuf `Result(scan_record=...)` → `wifi_prov_send_result()` → notify 到 Data Out

- 手机发 `SET_CONFIG` 后：
  - 触发 `NET_REQUEST_WIFI_CONNECT`
  - 连接结束 `NET_EVENT_WIFI_CONNECT_RESULT` → `handle_wifi_connect_result()`
  - 若失败：发送 `Result(state=CONNECTION_FAILED)`
  - 若成功：依次发送
    - `AUTHENTICATION`
    - `ASSOCIATION`
    - `OBTAINING_IP`
    - `CONNECTED`

### 2.4 凭据落地：设备侧存储在哪里

成功连接后：
- 默认（`volatileMemory` 未设置或为 false）：
  - `wifi_credentials_set_personal_struct(&config_buf)`
  - 具体后端由 Kconfig 决定：本例程板级配置选择 `CONFIG_WIFI_CREDENTIALS_BACKEND_PSA=y`

- 若 `volatileMemory=true`：
  - 不写 flash，写进 RAM 的 `config_in_ram`

---

## 3. 控制链（Control Plane）：谁驱动谁、状态如何推进

把线程/回调/事件按“时间顺序”串起来：

### 3.1 上电启动链（Boot → 广播 → 尝试自动连）

1) `main()` 初始化 DK LEDs
2) `k_sleep(1s)`：给 Wi‑Fi 驱动一点初始化时间（很常见的工程性处理）
3) 注册网络事件回调（`NET_EVENT_L4_CONNECTED/DISCONNECTED`）
   - 连接成功点亮 LED1
4) 注册蓝牙配对回调（pairing complete/failed 等）
5) `bt_enable()` 打开 BLE
6) `wifi_prov_init()`：注册 `NET_EVENT_WIFI_SCAN_RESULT` 和 `NET_EVENT_WIFI_CONNECT_RESULT` 事件回调（供配网核心使用）
7) 构造设备名 `PVxxxxxx` 并 `bt_set_name()`
8) 开始 advertising
   - 如果 `wifi_prov_state_get()==false`：FAST advertising
   - 如果已配网：SLOW advertising
9) 启动一个专用 work queue：周期性更新广播的 service data（包括配网状态/连接状态/RSSI）
10) 调用 `net_mgmt(NET_REQUEST_WIFI_CONNECT_STORED, ...)`
    - 这是“开机自动连”的关键

### 3.2 配网控制链（手机连接后的交互）

1) 手机连接 BLE
   - `connected()` 回调触发：取消定时更新广告（因为连接后通常不再广播）
2) 手机在 Control Point 写入 protobuf Request
   - `wifi_prov_ble.c::write_prov_control_point()` → `wifi_prov_recv_req()`
3) `wifi_prov_core` 分发请求
   - `GET_STATUS`：读当前是否已配网 + 当前 Wi‑Fi 状态 + IP
   - `START_SCAN`：先断开 Wi‑Fi，再发起 scan
   - `SET_CONFIG`：保存配置到 `config_buf`，立刻 `NET_REQUEST_WIFI_CONNECT`
   - `FORGET_CONFIG`：disconnect + 清空 `wifi_credentials` + 清空 `config_in_ram`
4) Wi‑Fi 连接结果通过 net_mgmt 事件回调进入 `wifi_prov_core`
   - encode Result → notify 给手机
   - 成功时把 `config_buf` 写入 `wifi_credentials`（或 RAM）
5) 手机断开 BLE
   - `disconnected()` 回调：
     - 重新调度 `update_adv_param_work`（1s 后 stop/start adv，切换 FAST/SLOW）
     - 重新调度 `update_adv_data_work`（立即更新 adv data）

---

## 4. 代码思路总结（为什么这么设计）

这个例程的“分层”非常清晰：

- **应用（main.c）只做两件事**
  1) 把设备变成一个可被发现、可被手机配网的 BLE 外设（广播/名称/配对）
  2) 做“开机自动连”（`CONNECT_STORED`）以及用 net_mgmt 事件反映网络状态

- **配网协议与 Wi‑Fi 行为由库接管**
  - BLE 侧：`wifi_prov_ble.c` 只负责把“字节流”交给 `wifi_prov_core`
  - core 侧：`wifi_prov_handler.c` 负责 protobuf 协议、扫描/连接调用、结果上报、凭据存储

这样的好处：
- 你换 transport（比如把 BLE 换成 SoftAP）时，尽量复用同一套“配网核心”。
- 应用代码很薄，更多逻辑可复用、易测试。

---

## 5. 关键配置点（为什么它能跑起来）

- `wifi-fund/l2/l2_e3_sol/prj.conf`
  - `CONFIG_BT_WIFI_PROV=y` + `CONFIG_WIFI_PROV_CORE=y`：启用 BLE 配网服务 + 配网核心
  - `CONFIG_NANOPB=y`：protobuf 编解码依赖
  - `CONFIG_WIFI_CREDENTIALS=y`：凭据存储/读取接口

- `wifi-fund/l2/l2_e3_sol/sysbuild.conf`
  - `SB_CONFIG_WIFI_NRF70=y`：确保 sysbuild 把 nRF70 Wi‑Fi 驱动编进最终镜像（nRF7002 场景必备）

- `wifi-fund/l2/l2_e3_sol/boards/*_ns.conf`
  - `CONFIG_WIFI_CREDENTIALS_BACKEND_PSA=y`
  - `CONFIG_WIFI_CREDENTIALS_BACKEND_SETTINGS=n`

---

## 6. 你后续改造时最值得动的“抓手”

- 如果你希望“配完就立即连”的策略可控：
  - 修改 `wifi_prov_core` 的 `prov_set_config_handler()`（或做一层包装）让它只存不连，改成由应用在某个时机触发连接。

- 如果你希望更省电：
  - 配网完成后可主动停止 BLE 广播（或把广播间隔调更慢）。

- 如果你希望更安全：
  - 现在是 `WRITE_ENCRYPT`（需要加密链路），但 `CONFIG_BT_BONDABLE=n` 意味着不绑定；
  - 可以改成 bondable + 白名单，或实现带 passkey/数值比较的配对策略。

---

## 7. 快速索引（从“现象”到“代码入口”）

- “手机写入 SSID/密码在哪里收？”
  - `nrf/subsys/bluetooth/services/wifi_prov/wifi_prov_ble.c::write_prov_control_point()`

- “protobuf 怎么解、op_code 怎么分发？”
  - `nrf/subsys/net/lib/wifi_prov_core/wifi_prov_handler.c::wifi_prov_recv_req()`
  - `...::prov_request_handler()`

- “收到凭据后怎么发起 Wi‑Fi 连接？”
  - `...::prov_set_config_handler()` → `NET_REQUEST_WIFI_CONNECT`

- “连接成功后凭据写到哪里？”
  - `...::handle_wifi_connect_result()` → `wifi_credentials_set_personal_struct()`

- “开机自动连接之前存过的网络？”
  - `wifi-fund/l2/l2_e3_sol/src/main.c::main()` → `NET_REQUEST_WIFI_CONNECT_STORED`
