# Omi 固件低功耗方案解读（nRF + Zephyr / NCS）

本文基于仓库中的 Omi 固件源码进行“代码级”解读，目标是：
- 用尽量通俗的方式解释 Zephyr/nRF 上的低功耗是怎么做出来的。
- 说明 Omi 当前在 **蓝牙 / Wi‑Fi / 外设 / 电池测量** 上分别用了哪些省电手段。
- 给出你后续做功耗优化时的“抓手”（从哪里下手改、改什么最有效）。

> 分析范围：`omi/omi/omi/firmware`（NCS/Zephyr 应用 + 自定义板级 DTS + sysbuild）。

---

## 1. 先理解 Zephyr 里的“低功耗三层结构”（新手版）

在 Zephyr/nRF 上做低功耗，通常分 3 层：

1) **CPU 空闲省电（Idle）**
- 只要你的线程不忙等（不要 while(1) 空转），而是 `k_sleep()` / `k_msleep()`，内核就能在空闲时让 CPU 进入睡眠（WFI/WFE），这是“最基础”的省电。
- 但注意：CPU 睡了 ≠ 外设都睡了。外设/无线如果还在工作，电流仍然可能很大。

2) **设备级省电（Device PM）**
- 你需要让具体外设进入 suspend/low-power，比如 SPI Flash 进 deep power-down、SD 卡断电、Wi‑Fi 芯片下电等。
- Zephyr 提供 API：
  - `pm_device_action_run(dev, PM_DEVICE_ACTION_SUSPEND/RESUME)`：显式让某个设备进入 suspend/resume。
  - `pm_device_runtime_get/put`：运行时 PM（按需启用/停用设备），更“自动化”，但要正确配置并且驱动要支持。

3) **系统级极限省电（System Off / Deep sleep）**
- 当你希望设备“像关机一样”省电时，典型做法是 `sys_poweroff()` 进入 System Off。
- 唤醒通常靠 GPIO level/edge、NFC field detect 等硬件事件。

**Omi 的策略总结一句话：**
- **工作态**：尽量靠“按需开启无线/外设 + 不用就关（Device PM）”。
- **关机态**：用 `sys_poweroff()` 进入 System Off，并配置按键唤醒。

---

## 2. 入口配置：Omi 在 Kconfig / sysbuild 里打开了哪些“省电相关开关”

### 2.1 应用配置（omi.conf）
- 文件：`omi/omi/omi/firmware/omi/omi.conf`
- 关键点：
  - `CONFIG_PM_DEVICE=y`：启用设备级电源管理 API（允许 suspend/resume）。
  - `# CONFIG_PM_DEVICE_RUNTIME=y`：**运行时 PM 被注释掉**，说明当前更偏向“手动 suspend/resume”。
  - 蓝牙：`CONFIG_BT_*`、`CONFIG_BT_PERIPHERAL_PREF_*` 设定偏好的连接参数（影响功耗/吞吐）。
  - Wi‑Fi：`CONFIG_WIFI=y`、`CONFIG_WIFI_NRF70=y`、`CONFIG_NRF_WIFI_IF_AUTO_START=n`（接口不自动起，利于按需启用）。

### 2.2 sysbuild（多镜像/双核 + 外部 flash PM）
- 文件：`omi/omi/omi/firmware/omi/sysbuild.conf`
- 关键点：
  - `SB_CONFIG_WIFI_NRF70=y`：保证 Wi‑Fi 驱动会被 sysbuild 正确编进最终镜像。
  - `SB_CONFIG_PM_EXTERNAL_FLASH_MCUBOOT_SECONDARY=y`：配合 MCUboot 的外部 flash/secondary slot，避免外部 flash 在 PM 场景下功耗异常。

### 2.3 板级 DTS（硬件决定你能怎么省电）
- 文件：`omi/omi/omi/firmware/boards/omi/omi_nrf5340_cpuapp.dts`
- 和功耗相关的点：
  - `chosen { nordic,pm-ext-flash = &spi_flash; }`：告诉系统“外部 SPI Flash 是 PM 相关外设”。
  - `chosen { zephyr,wifi = &wlan0; }`：板级声明 Wi‑Fi。
  - `spi_flash` 节点带 `has-dpd`（deep power-down）属性：这意味着 flash 驱动有机会进入更低功耗状态。
  - `sdcard_en_pin`、`rfsw_en_pin`、`pdm_en_pin`、`lsm6dso_en_pin` 等：这些“使能脚”是做低功耗的关键抓手——不用外设时可以硬件断电/关模拟电路。

---

## 3. 蓝牙（BLE）的低功耗方案：能关就关 + 连接参数控制

### 3.1 BLE 什么时候耗电最大？
- **广播（Advertising）**：周期越短越耗电。
- **已连接（Connected）**：连接间隔越短、吞吐越高越耗电。
- **音频流**：如果你在跑音频实时推送，低功耗空间会明显变小，因为你需要频繁无线传输。

### 3.2 Omi 的实现方式

**(A) 传输层按需开启/关闭 BLE**
- 文件：`omi/omi/omi/firmware/omi/src/lib/core/transport.c`
- 关键行为：
  - `transport_start()`：
    - `bt_enable(NULL)` 开启蓝牙
    - `bt_le_adv_start(...)` 开始广播
    - 注册 GATT 服务（音频/设置/特性等）
  - `transport_off()`：
    - `bt_le_adv_stop()` 停止广播
    - `bt_disable()` 关闭蓝牙（这对省电非常直接有效）

**(B) RF 前端/天线开关控制**
- 同样在 `transport_start/off()` 中，通过 GPIO 控制 `rfsw_en`：
  - start 时拉高（打开射频开关）
  - off 时拉低（关闭射频路径）
- 这是“板级硬件 + 软件协同省电”的典型做法。

**(C) 连接参数在 Kconfig 中显式配置**
- 文件：`omi/omi/omi/firmware/omi/omi.conf`
- 例如：
  - `CONFIG_BT_PERIPHERAL_PREF_MIN_INT=6`（约 7.5ms）
  - `CONFIG_BT_PERIPHERAL_PREF_MAX_INT=24`（约 30ms）
- 这类参数是典型的“功耗 vs 体验”开关：
  - 间隔短：延迟低、吞吐高、功耗高
  - 间隔长：延迟高、吞吐低、功耗低

**(D) 控制器侧动态发射功率**
- 文件：`omi/omi/omi/firmware/boards/omi/omi_nrf5340_cpunet_defconfig`
- 看到：`CONFIG_BT_CTLR_TX_PWR_DYNAMIC_CONTROL=y`
- 这表示 BLE 控制器可以根据链路状况动态调 TX power（在近距离时降低发射功率，节省能量）。

---

## 4. Wi‑Fi 的低功耗方案：默认关、用时开、用完关（而不是一直开）

Wi‑Fi 通常是整机最耗电模块之一。对穿戴设备来说，最有效的策略往往是：
- **绝大多数时间 Wi‑Fi 关闭**
- 需要同步/上传时短暂开启，完成后立刻关闭

### 4.1 Omi 的 Wi‑Fi 状态机（按需启停）
- 文件：`omi/omi/omi/firmware/omi/src/wifi.c`
- 关键接口：
  - `wifi_turn_on()`：
    - `net_if_up(iface)` 把 Wi‑Fi 接口拉起
    - 设置内部状态 `WIFI_STATE_ON`，后续自动尝试 connect
  - `wifi_turn_off()`：
    - 进入 `WIFI_STATE_SHUTDOWN`，断开 TCP/断开 Wi‑Fi
    - `net_if_down(iface)` 把接口拉 down（很多驱动会在这里进入更低功耗/下电路径）

### 4.2 进入 system off 前对 Wi‑Fi 做“硬关”
- 文件：`omi/omi/omi/firmware/omi/src/lib/evt/systemoff.c`
- 关键行为：
  - 在 `sys_poweroff()` 前调用 `rpu_disable()`
- 这属于“确保 Wi‑Fi 芯片真的停了”的保险操作（避免 Wi‑Fi 协处理器/芯片残留功耗）。

### 4.3 配置层面的低功耗点
- 文件：`omi/omi/omi/firmware/omi/omi.conf`
- 关键点：`CONFIG_NRF_WIFI_IF_AUTO_START=n`（不自动启动）

> 备注：测试工程中（`omi/omi/omi/firmware/test/omi.conf`）还启用了 `CONFIG_NRF_WIFI_LOW_POWER=y`，但在 `omi/omi.conf` 里没有看到同样设置。这意味着“正式固件”可能还没完全启用 Wi‑Fi 低功耗模式，或者依赖于按需 `net_if_down()` 来省电。

---

## 5. 外设/驱动是否启用了 PM 回调？Omi 的策略是：能 suspend 的就显式 suspend

你问的“驱动是否启用了 pm 回调”——从应用层代码能看到的最直接证据是：**它是否在调用 `pm_device_action_run()`，并且对关键外设做了 suspend/resume**。

### 5.1 外部 SPI Flash
- 文件：`omi/omi/omi/firmware/omi/src/spi_flash.c`
- 关键点：`flash_off()` 里直接 `pm_device_action_run(spi_flash_dev, PM_DEVICE_ACTION_SUSPEND)`
- 这通常会触发 SPI NOR 驱动进入 deep power-down（取决于驱动与 DTS 的 `has-dpd` 等能力）。

### 5.2 SD 卡（SPI-SD）
- 文件：`omi/omi/omi/firmware/omi/src/sd_card.c`
- 省电手段是“软硬结合”：
  1) GPIO 关断 `sdcard_en_pin`（硬件断电/关电源域）
  2) `pm_device_action_run(sd_dev/spi_dev, PM_DEVICE_ACTION_SUSPEND)`（让 SPI/SD 设备进入 suspend）
  3) 代码里还手动处理了 CS pin（注释写明：Zephyr suspend 没处理 CS，应用侧兜底）

### 5.3 传感器/麦克风/震动/LED 等
- 麦克风：`omi/omi/omi/firmware/omi/src/mic.c`
  - 通过 `dmic_trigger(START/STOP)` 控制采集，停止采集可以显著下降功耗（PDM 时钟停止）。
- IMU：板级 DTS 里有 `lsm6dso_en_pin`（可关 IMU 供电/使能），应用里也提供 `accel_off()` 拉低控制脚。
- LED：PWM 驱动本身不一定“自动省电”，但 Omi 会在关机/关机流程中 `led_off()`。

### 5.4 运行时 PM（pm_device_runtime_*）现状
- 代码里确实调用了 `pm_device_runtime_get/put`（例如按钮模块），但 `omi.conf` 把 `CONFIG_PM_DEVICE_RUNTIME` 注释掉了。
- 这意味着当前更像是“未来预留/可选”，实际省电主要靠显式 suspend/resume + system off。

---

## 6. 电池/充电低功耗：关键是“不要让分压电阻一直漏电”

Omi 的电池测量非常典型，也很值得你照抄：

### 6.1 只在测量时打开电压采样通路
- 文件：`omi/omi/omi/firmware/omi/src/battery.c`
- 关键点：
  - 通过 `bat_read_pin` 控制电压测量通路（多半是分压/使能 MOS）。
  - 测量时把 `bat_read_pin` 配成输出并拉到特定电平以“打开测量路径”。
  - 测量完成后立刻把 `bat_read_pin` 恢复为 `GPIO_INPUT`，从而避免持续漏电。

这点对低功耗非常关键：
- 很多设备的“电池分压”如果直接常连，会带来持续的 $~
$ µA 甚至更高的静态消耗（取决于分压阻值）。

### 6.2 电量上报频率
- 文件：`omi/omi/omi/firmware/omi/src/lib/core/transport.c`
- 采用 `K_WORK_DELAYABLE_DEFINE(battery_work, ...)` 每 10 秒上报一次（`BATTERY_REFRESH_INTERVAL=10000`）。
- 这会周期性唤醒系统、开 ADC、采样多次（这里采样很多次用于稳定/滤波），因此如果你要“超低功耗待机”，可能需要把刷新周期改长（比如 60s/300s）或改成事件驱动。

### 6.3 低电量保护（避免深度放电）
- 同样在 `transport.c` 的电池任务里：低于阈值会调用 `turnoff_all()` 进入关机流程。
- 这属于“电池保护型低功耗”：不是省那一点点电，而是避免电池过放导致损坏。

---

## 7. 关机（System Off）流程：Omi 真正的“极限省电模式”

- 文件：`omi/omi/omi/firmware/omi/src/lib/core/button.c`
- `turnoff_all()` 做了一串“先关耗电大户，再进 system off”的操作：
  - `transport_off()`（关 BLE、RF 开关拉低）
  - `mic_off()`（停 PDM/DMIC）
  - `accel_off()`（拉低使能脚）
  - SD/Flash/WDT 等依次关闭
  - `wifi_turn_off()`（可选）
  - 配置 `usr_btn` 为 level interrupt 作为唤醒源
  - 最后 `sys_poweroff()` 进入 System Off

这套流程的本质就是：
- **先把所有外设/无线都停掉**（否则 system off 之前的瞬态功耗也会很大，而且可能导致某些外设状态不一致）
- 再进入 System Off（此时整机功耗通常可以做到 µA 级，具体取决于板子电路）

---

## 8. 你可以怎么用这份解读来继续优化（建议路线）

如果你是萌新，建议按“收益最大→最小”来做：

1) **优先做“关无线”策略**
- BLE：不需要连接就拉长广播间隔；完全不需要就 `bt_disable()`。
- Wi‑Fi：坚持“用完就 net_if_down()”，把 Wi‑Fi 的活跃时间压缩到最短。

2) **把外设做成真正“按需供电”**
- SD 卡 / SPI Flash：现在已经做得不错（有 suspend + 使能脚）。
- IMU / 麦克风：如果硬件有 EN pin，就让 idle 时拉低，只有录音/检测时拉高。

3) **再考虑运行时 PM（CONFIG_PM_DEVICE_RUNTIME）**
- 这是进阶方向：需要确保每个设备驱动都支持 runtime PM，并且你的使用方式是“get/put 成对”。

4) **最后再抠细节：日志、串口、采样频率、连接参数**
- `CONFIG_CONSOLE/CONFIG_LOG` 都会影响功耗（尤其是 UART 输出）。
- 电池刷新周期、IMU ODR、BLE conn interval 都是很有效的旋钮。

---

## 9. 快速索引（你要找代码就看这里）

- 配置入口：`omi/omi/omi/firmware/omi/omi.conf`
- sysbuild（Wi‑Fi + MCUboot + 外部 flash PM）：`omi/omi/omi/firmware/omi/sysbuild.conf`
- 板级 DTS（Wi‑Fi/flash/SD 使能脚）：`omi/omi/omi/firmware/boards/omi/omi_nrf5340_cpuapp.dts`
- BLE 传输启停 + RF 开关：`omi/omi/omi/firmware/omi/src/lib/core/transport.c`
- Wi‑Fi 状态机与启停：`omi/omi/omi/firmware/omi/src/wifi.c`
- 关机流程（System Off）：`omi/omi/omi/firmware/omi/src/lib/core/button.c`
- SPI Flash suspend：`omi/omi/omi/firmware/omi/src/spi_flash.c`
- SD 卡断电 + suspend：`omi/omi/omi/firmware/omi/src/sd_card.c`
- 电池测量（bat_read_pin 省电）：`omi/omi/omi/firmware/omi/src/battery.c`
