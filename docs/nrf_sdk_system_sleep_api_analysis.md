# nrf_sdk_system_sleep_api.md 解读（NCS/Zephyr System PM）

目标：给 `respeaker_sample/pm/` 的“周期切换系统级休眠状态”sample 提供 API/行为边界说明。

## 关键结论（最重要）

- `pm_state_force()` **不会立刻让 CPU 睡眠**：它只是为指定 CPU 设置“下一次进入 idle/suspend 时要使用的 pm_state”。
  - 因此需要让系统进入 idle（例如主线程 `k_sleep()`/`k_msleep()` 足够久），`pm_system_suspend()` 才会消费该 forced state。
- `CONFIG_PM` 依赖 `HAS_PM`（必须由 SoC Kconfig `select`，不能在 `prj.conf` 里直接设置）：如果 `HAS_PM` 不存在，则 `CONFIG_PM` 会被回退为 `n`，`pm_state_force()`/`pm_notifier` 这类 API 也不会可用。
- `sys_poweroff()` 是 **noreturn**：进入 System OFF/软关机后代码不会返回；通常需要外部事件/复位唤醒。
  - 所以如果要做“状态轮询切换”，对于不可返回的状态必须设计“跨重启 stage 保存”（NVS/retained RAM/GPREGRET 等）。
- `pm_policy_state_lock_get()/put()` 可用来临时禁止某些 state（比如调试时不想进深睡）。

## `enum pm_state` 含义（抽象层）

- `PM_STATE_ACTIVE`：正常运行。
- `PM_STATE_RUNTIME_IDLE`：运行态 idle（典型是 WFI + 外设保持）。
- `PM_STATE_SUSPEND_TO_IDLE`：系统 suspend-to-idle（仍靠 IRQ 唤醒，通常可返回）。
- `PM_STATE_STANDBY`：更深的待机（是否可返回/如何唤醒取决于 SoC 和 DTS power-states）。
- `PM_STATE_SUSPEND_TO_RAM / PM_STATE_SOFT_OFF`：更深层级（很多平台上会等同于关机/复位才能继续）。

## Notifier（进入/退出回调）

- `pm_notifier_register()` 允许注册进入/退出 state 的通知回调。
- 回调可能在 ISR 上下文触发；若要打日志，建议使用 `printk()` 或启用 immediate log。

## 和本项目 sample 的关系

- `respeaker_sample/pm/` 的实现选取了“能被定时器唤醒并继续执行”的状态做 60s 轮询。
- System OFF/soft-off 这类状态默认不纳入轮询（否则无法满足“1 分钟后自动切到下一个状态”的语义）。
