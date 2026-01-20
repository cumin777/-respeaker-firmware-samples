# PM cycle sample (nRF5340DK)

这个 sample 用于在 nRF5340DK 上演示“系统级休眠/关机”相关行为（每个状态运行一次，切换间隔 10 秒）：
- `ACTIVE_RUN`：正常运行（busy，不进入 sleep）
- `THREAD_SLEEP`：线程睡眠（`k_msleep()`，系统会进入 idle）
- `CPU_IDLE`：显式 idle（循环 `k_cpu_idle()`）
- `SYS_POWEROFF`：System OFF/关机（`sys_poweroff()`，不返回）

> 注意：nRF5340 的 System OFF/软关机类状态通常需要外部事件/复位唤醒，进入后不会返回；因此本 sample 在最后一个状态执行 `sys_poweroff()` 并结束。

## Build

说明：在 NCS v3.2.1 里，nRF5340DK（nRF53）无法启用 Zephyr System PM（`CONFIG_PM` 依赖 `HAS_PM`，且 SoC 未 `select HAS_PM`）。
因此本 sample 使用 `k_msleep()` / `k_cpu_idle()` / `sys_poweroff()` 来完成演示。

在 NCS v3.2.1：

```bash
west build -p always -b nrf5340dk_nrf5340_cpuapp .
```

烧录：

```bash
west flash
```

## 运行现象

串口日志会按 10 秒切换一次并且每个状态只执行一次：
- `ACTIVE_RUN(busy)` → `THREAD_SLEEP(k_msleep)` → `CPU_IDLE(k_cpu_idle loop)` → `SYS_POWEROFF(sys_poweroff)`

