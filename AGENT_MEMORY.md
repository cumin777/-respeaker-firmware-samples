# reSpeaker Agent Memory（可持续更新）

> 目的：把“已解读的需求/关键决策/协议边界/未决问题”写入仓库，让后续任何一次 Copilot/Agent 会话都能从文件中恢复上下文，实现工程化的“持续记忆”。

## 已确认需求（摘要）
- 产品：可穿戴 AI 录音分析设备，偏 2B，可定制，可接入自定义 STT/LLM token。
- 核心硬件：nRF5340+nRF7002（备选 nRF54L15+nRF7002），2×PDM Mic，32G 存储，0.5" 88×48 OLED，震动马达，静音拨片。
- 录音：普通模式（双声道原始）/增强模式（降噪+增益 单声道）。采样率到 16k。
- 无线：BLE（配对、状态、低速传输、OTA）；Wi‑Fi（高速导出录音）。
- OTA：通过 BLE（App），且升级前必须待机+充电；要求安全校验与回滚。
- SDK：固件层 + App 层（Swift/Kotlin/Python）对外能力边界要稳定、可版本化。

## 关键决策（待填）
- Wi‑Fi 传输拓扑：AP-only / STA-only / dual
- 文件系统：FATFS vs LittleFS
- OTA 方案：MCUboot vs 自研
- 音频增强算法：选型/指标/资源预算

## 未决问题（需要产品/硬件/测试确认）
- “自带 j-link” 的量产实现方式
- 续航目标达成策略：无线默认关闭/按需开启、CPU DVFS、日志策略

## 约定的工作流
- 每次新增/澄清需求：更新本文件的“已确认需求/关键决策/未决问题”。
- 每次协议变更：补充到 `docs/requirements_interpretation.md` 并记录版本。

## Wi‑Fi（NCS/Zephyr）知识库索引
- AP/SoftAP 速查：`docs/nrf_wifi_ap_softap_notes.md`
- AP 关键 API（Zephyr）：`net_mgmt(NET_REQUEST_WIFI_AP_ENABLE/AP_DISABLE, ...)` + 事件 `NET_EVENT_WIFI_AP_*`
- SoftAP 配网库（Nordic）：`nrf/include/net/softap_wifi_provision.h`（init/start/reset + `SOFTAP_WIFI_PROVISION_EVT_*`）

## Omi 固件低功耗解读
- `docs/omi_firmware_low_power_analysis.md`

## Wi‑Fi Fundamentals：BLE 配网（L2 E3）
- `docs/wifi_fund_l2_e3_ble_wifi_provisioning_analysis.md`
- 核心链路：BLE Control Point 写入 protobuf `Request(SET_CONFIG)` → `wifi_prov_core` 触发 `NET_REQUEST_WIFI_CONNECT` → `NET_EVENT_WIFI_CONNECT_RESULT` 编码为 protobuf `Result` 通过 notify 回传
- 成功后凭据写入 `wifi_credentials`（本例程板级默认用 PSA backend），开机由 `NET_REQUEST_WIFI_CONNECT_STORED` 自动重连

## 系统级休眠/PM sample
- `../respeaker_sample/pm/`：nRF5340DK 系统级休眠轮询 sample（`pm_state_force()` + `pm_notifier`，每个状态保持 60s）
- API 解读：`docs/nrf_sdk_system_sleep_api_analysis.md`
