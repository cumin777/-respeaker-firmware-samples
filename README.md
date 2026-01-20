# -respeaker-firmware-samples

Respeaker 初始示例代码仓库（基于 Nordic nRF Connect SDK / Zephyr）。

## 目录

- `driver/`：驱动与自定义 Zephyr module 示例
- `wifi/`：Wi‑Fi 示例（含 SoftAP）
- `pm/`：低功耗/关机（System OFF）示例
- `dfu/`：DFU 示例（USB DFU）
- `bt/`：BLE 示例
- `docs/`：笔记与分析文档（“持久记忆”沉淀）

## 构建示例（命令行）

在 NCS 环境（已安装 west/Zephyr 工具链）下，例如：

- Wi‑Fi SoftAP：`west build -p -b <board> wifi/wifi_softAP`
- PM Demo：`west build -p -b nrf5340dk_nrf5340_cpuapp pm`

不同示例可能需要不同开发板/Shield（例如 Wi‑Fi 相关需要对应 Wi‑Fi 硬件与配置）。

## Git 说明

本仓库已通过 `.gitignore` 排除 Zephyr/NCS 构建产物（如 `build/`、`_sysbuild/`）。

