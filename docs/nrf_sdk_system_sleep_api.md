# nRF SDK 系统级休眠 API 文档

## 概述

nRF SDK 基于 Zephyr RTOS 提供了丰富的系统级休眠电源管理 API，支持从简单的 CPU 空闲模式到复杂的深度睡眠状态。本文档整理了所有主要的休眠相关 API。

## 1. 内核休眠 API

### 基本休眠函数

#### 1.1 `k_sleep()`
```c
#include <zephyr/kernel.h>
__syscall int32_t k_sleep(k_timeout_t timeout);
```

- **功能**：使当前线程休眠指定时间
- **参数**：
  - `timeout`: 休眠时长，使用 `k_timeout_t` 类型
- **返回值**：实际休眠时间（单位：毫秒）
- **上下文**：线程上下文

#### 1.2 `k_msleep()`
```c
static inline int32_t k_msleep(int32_t ms)
```

- **功能**：毫秒级休眠（便捷函数）
- **参数**：
  - `ms`: 毫秒数
- **返回值**：0
- **示例**：
```c
k_msleep(100);  // 休眠 100ms
```

#### 1.3 `k_usleep()`
```c
__syscall int32_t k_usleep(int32_t us);
```

- **功能**：微秒级休眠
- **参数**：
  - `us`: 微秒数
- **返回值**：实际休眠时间（单位：微秒）
- **注意**：使用时需谨慎，因为实现精度可能较低

#### 1.4 `k_busy_wait()`
```c
static inline void k_busy_wait(uint32_t usec);
```

- **功能**：忙等待（不释放 CPU，无上下文切换）
- **参数**：
  - `usec`: 等待微秒数
- **返回值**：无
- **适用场景**：短时间等待，且需要精确计时
- **示例**：
```c
k_busy_wait(1000);  // 忙等待 1000 微秒
```

#### 1.5 `k_yield()`
```c
__syscall void k_yield(void);
```

- **功能**：主动让出 CPU 给其他就绪线程
- **参数**：无
- **返回值**：无
- **适用场景**：线程主动调度优化

### CPU 空闲模式

#### 1.6 `k_cpu_idle()`
```c
static inline void k_cpu_idle(void);
```

- **功能**：让 CPU 进入空闲状态，等待中断唤醒
- **行为**：
  - 禁用中断
  - 让 CPU 进入低功耗模式
  - 等待中断唤醒 CPU
- **适用场景**：系统空闲时降低功耗
- **注意**：必须在能被中断的上下文中调用

#### 1.7 `k_cpu_atomic_idle()`
```c
static inline void k_cpu_atomic_idle(void);
```

- **功能**：原子性的 CPU 空闲（在锁中断的上下文中）
- **行为**：
  - 保持中断禁用状态
  - 让 CPU 进入低功耗模式
  - 等待特定中断唤醒
- **适用场景**：在中断服务程序（ISR）中需要等待时

## 2. 系统级休眠 API

### 2.1 系统休眠状态

#### 系统休眠状态定义
```c
enum pm_state {
    PM_STATE_ACTIVE,                    // 活跃状态
    PM_STATE_RUNTIME_IDLE,             // 运行时空闲
    PM_STATE_SUSPEND_TO_IDLE,          // 挂起到空闲
    PM_STATE_STANDBY,                  // 待机
    PM_STATE_SUSPEND_TO_RAM,           // 挂起到内存
    PM_STATE_SUSPEND_TO_DISK,          // 挂起到磁盘
    PM_STATE_SOFT_OFF,                 // 软关机
};
```

#### 2.2 系统休眠函数

##### `pm_state_force()`
```c
#include <zephyr/pm/pm.h>
bool pm_state_force(uint8_t cpu, const struct pm_state_info *info);
```

- **功能**：强制系统使用指定的休眠状态
- **参数**：
  - `cpu`: CPU 索引
  - `info`: 休眠状态信息指针
- **返回值**：
  - `true`: 成功强制状态
  - `false`: 失败
- **注意**：只能在线程上下文中使用

##### `pm_policy_next_state()`
```c
const struct pm_state_info *pm_policy_next_state(uint8_t cpu, int32_t ticks);
```

- **功能**：获取下一个可用的休眠状态
- **参数**：
  - `cpu`: CPU 索引
  - `ticks`: 到下一个事件的时间（ticks）
- **返回值**：休眠状态信息指针

### 2.3 电源管理策略

#### 休眠状态锁定/解锁
```c
// 获取状态锁（阻止该状态）
void pm_policy_state_lock_get(enum pm_state state, uint8_t substate_id);

// 释放状态锁（允许该状态）
void pm_policy_state_lock_put(enum pm_state state, uint8_t substate_id);

// 检查状态锁是否活跃
bool pm_policy_state_lock_is_active(enum pm_state state, uint8_t substate_id);
```

#### 设备电源约束
```c
// 获取设备电源锁
void pm_policy_device_power_lock_get(const struct device *dev);

// 释放设备电源锁
void pm_policy_device_power_lock_put(const struct device *dev);
```

## 3. 设备级电源管理

### 3.1 设备运行时电源管理

```c
#include <zephyr/pm/device_runtime.h>

// 启用设备运行时电源管理
int pm_device_runtime_enable(const struct device *dev);

// 禁用设备运行时电源管理
int pm_device_runtime_disable(const struct device *dev);

// 获取设备（增加使用计数）
int pm_device_runtime_get(const struct device *dev);

// 释放设备（减少使用计数）
int pm_device_runtime_put(const struct device *dev);
```

### 3.2 设备休眠/唤醒

```c
// 设备操作枚举
enum pm_device_action {
    PM_DEVICE_ACTION_SUSPEND,   // 挂起设备
    PM_DEVICE_ACTION_RESUME,    // 唤醒设备
    PM_DEVICE_ACTION_TURN_OFF,   // 关闭设备
    PM_DEVICE_ACTION_TURN_ON,    // 开启设备
};

// 设备电源管理回调
typedef int (*pm_device_action_cb_t)(const struct device *dev,
                                    enum pm_device_action action);
```

## 4. WiFi 电源管理

### 4.1 WiFi 功率节省模式

WiFi 模块支持多种功率节省（Power Save）模式，可显著降低空闲时的功耗。

#### WiFi PS 参数

```c
#include <zephyr/net/wifi_mgmt.h>

// WiFi 功率节省状态
enum wifi_ps_enabled {
    WIFI_PS_ENABLED,        // 功率节省已启用
    WIFI_PS_DISABLED,       // 功率节省已禁用
};

// WiFi 唤醒模式
enum wifi_ps_wakeup_mode {
    WIFI_PS_WAKEUP_MODE_ENABLED,   // 唤醒已启用
    WIFI_PS_WAKEUP_MODE_DISABLED,  // 唤醒已禁用
};

// WiFi PS 模式
enum wifi_ps_mode {
    WIFI_PS_MODE_LEGACY,           // 传统 802.11 功率节省
    WIFI_PS_MODE_WMM,               // WMM（Wi-Fi 多媒体）模式
    WIFI_PS_MODE_WLAN_UAPSD,         // WLAN U-APSD（动态功耗）模式
    WIFI_PS_MODE_WLAN_POWERSAVE,       // WLAN 省电（静态功耗）模式
};

// WiFi PS 参数
enum wifi_ps_param_type {
    WIFI_PS_PARAM_ENABLED,           // 启用/禁用 PS
    WIFI_PS_PARAM_WAKEUP_MODE,      // 唤醒模式
    WIFI_PS_PARAM_MODE,              // PS 模式
    WIFI_PS_PARAM_EXIT_STRATEGY,     // 退出策略
    WIFI_PS_PARAM_MULTICAST_FILTER, // 组播过滤器
};

struct wifi_ps_params {
    enum wifi_ps_enabled enabled;      // PS 状态
    enum wifi_ps_wakeup_mode wakeup_mode;  // 唤醒模式
    enum wifi_ps_mode mode;            // PS 模式
    enum wifi_ps_exit_strategy exit_strategy; // 退出策略
};

struct wifi_ps_config {
    struct wifi_ps_params ps_params;  // PS 参数
};
```

#### WiFi PS 管理命令

```c
// 设置 WiFi 功率节省
#define NET_REQUEST_WIFI_PS \
    (NET_MGMT_BASE | NET_MGMT_WIFI_CMD_PS)

// 获取 WiFi 功率节省配置
#define NET_REQUEST_WIFI_PS_CONFIG \
    (NET_MGMT_BASE | NET_MGMT_WIFI_CMD_PS_CONFIG)

// WiFi 驱动回调函数
typedef int (*wifi_set_power_save)(const struct device *dev, struct wifi_ps_params *params);
typedef int (*wifi_get_power_save_config)(const struct device *dev, struct wifi_ps_config *config);
```

### 4.2 WiFi 功率节省示例

```c
#include <zephyr/net/net_mgmt.h>

void wifi_power_save_example(void)
{
    struct net_if *iface = net_if_get_wifi();
    if (!iface) {
        return;
    }

    // 禁用 WiFi 功率节省（活跃模式）
    struct wifi_ps_params ps_params = {
        .enabled = WIFI_PS_DISABLED,
    };
    int ret = net_mgmt(NET_REQUEST_WIFI_PS, iface, &ps_params, sizeof(ps_params));
    if (ret) {
        LOG_ERR("Failed to disable WiFi PS: %d", ret);
        return;
    }

    LOG_INF("WiFi Power Save disabled (active mode)");

    // 启用功率节省
    ps_params.enabled = WIFI_PS_ENABLED;
    ps_params.mode = WIFI_PS_MODE_WLAN_POWERSAVE;
    ret = net_mgmt(NET_REQUEST_WIFI_PS, iface, &ps_params, sizeof(ps_params));
    if (ret) {
        LOG_ERR("Failed to enable WiFi PS: %d", ret);
        return;
    }

    LOG_INF("WiFi Power Save enabled (WLAN Power Save mode)");
}

// 动态切换功率节省
void wifi_dynamic_power_save(void)
{
    struct net_if *iface = net_if_get_wifi();

    // 有网络活动时禁用 PS
    if (has_network_activity()) {
        struct wifi_ps_params params = {
            .enabled = WIFI_PS_DISABLED,
        };
        net_mgmt(NET_REQUEST_WIFI_PS, iface, &params, sizeof(params));
    }
    // 空闲时启用 PS
    else {
        struct wifi_ps_params params = {
            .enabled = WIFI_PS_ENABLED,
            .mode = WIFI_PS_MODE_WLAN_POWERSAVE,
        };
        net_mgmt(NET_REQUEST_WIFI_PS, iface, &params, sizeof(params));
    }
}
}
```

### 4.3 配置选项

```conf
# WiFi 功率节省
CONFIG_WIFI_NM=y
CONFIG_WIFI_MGMT=y

# 网络事件管理器（支持 WiFi PS）
CONFIG_NET_CONNECTION_MANAGER=y
CONFIG_NET_CONNECTION_MANAGER_MONITOR=y
```

## 5. 系统关机 API

### 4.1 系统关机
```c
#include <zephyr/sys/poweroff.h>

// 关机（noreturn）
void sys_poweroff(void);
```

- **功能**：立即关闭系统
- **注意**：调用后不会返回
- **依赖**：需要启用 `CONFIG_POWEROFF` 配置

## 5. 时间和超时

### 5.1 超时常量
```c
// Zephyr 超时宏定义
#define K_NO_WAIT   (k_timeout_t) { .ticks = 0 }
#define K_FOREVER   (k_timeout_t) { .ticks = -1 }
#define K_SECONDS(x) (k_timeout_t) { .ticks = (x) * sys_clock_ticks_per_sec }
#define K_MSEC(x)    (k_timeout_t) { .ticks = (x) * sys_clock_ticks_per_sec / 1000 }
#define K_USEC(x)   (k_timeout_t) { .ticks = (x) * sys_clock_ticks_per_sec / 1000000 }
```

### 5.2 时间获取
```c
// 获取系统启动时间（毫秒）
uint64_t k_uptime_get(void);

// 获取系统启动时间（ticks）
int64_t k_uptime_ticks(void);
```

## 6. nRF5340 特定的电源管理

### 6.1 CPU 电源模式
nRF5340 支持多种 CPU 电源模式：

1. **活跃模式**：CPU 全速运行
2. **空闲模式**：CPU 停止，外设继续工作
3. **系统 OFF 模式**：最低功耗，需要外部复位唤醒

### 6.2 电源状态配置
```c
// 在设备树中配置电源状态
&cpu0 {
    power-states {
        idle: idle {
            compatible = "zephyr,power-state";
            power-state-name = "idle";
            min-residency-us = <100>;
            exit-latency-us = <10>;
            substates;
        };
        deep-sleep: deep-sleep {
            compatible = "zephyr,power-state";
            power-state-name = "standby";
            min-residency-us = <1000>;
            exit-latency-us = <50>;
            substates;
        };
    };
};
```

## 7. 最佳实践示例

### 7.1 简单的循环休眠
```c
#include <zephyr/kernel.h>
#include <zephyr/pm/pm.h>

void main(void) {
    while (1) {
        // 执行工作...

        // 休眠 100ms 降低功耗
        k_msleep(100);
    }
}
```

### 7.2 事件驱动的电源管理
```c
#include <zephyr/kernel.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/device.h>

void main(void) {
    // 启用设备运行时电源管理
    pm_device_runtime_enable(DEVICE_DT_GET(DT_NODELABEL(sensor)));

    while (1) {
        // 激活设备
        pm_device_runtime_get(DEVICE_DT_GET(DT_NODELABEL(sensor)));

        // 执行传感器读取...

        // 释放设备
        pm_device_runtime_put(DEVICE_DT_GET(DT_NODELABEL(sensor)));

        // 等待下一个事件
        k_msleep(1000);
    }
}
```

### 7.3 深度睡眠示例
```c
#include <zephyr/pm/pm.h>
#include <zephyr/kernel.h>

void enter_deep_sleep(void) {
    // 设置电源状态锁
    pm_policy_state_lock_get(PM_STATE_SUSPEND_TO_RAM, 0);

    // 等待系统进入休眠
    k_sleep(K_FOREVER);

    // 永远不会执行到这里（需要外部唤醒）
}

// 唤醒回调
void wakeup_handler(void) {
    // 释放电源状态锁
    pm_policy_state_lock_put(PM_STATE_SUSPEND_TO_RAM, 0);

    // 处理唤醒后的工作...
}
```

### 7.4 低功耗传感器读取
```c
#include <zephyr/kernel.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/device.h>

#define SAMPLE_INTERVAL_MS 5000

void sensor_loop(void) {
    const struct device *sensor = DEVICE_DT_GET(DT_NODELABEL(temp_sensor));

    if (!device_is_ready(sensor)) {
        return;
    }

    while (1) {
        // 获取设备电源
        pm_device_runtime_get(sensor);

        // 读取传感器数据
        float temp = read_temperature(sensor);

        // 处理数据...

        // 释放设备电源
        pm_device_runtime_put(sensor);

        // 休眠到下次采样
        k_msleep(SAMPLE_INTERVAL_MS);
    }
}
```

## 8. 注意事项

### 8.1 使用约束
1. **`k_sleep()` 系列**：只能在线程上下文中使用，不能在中断服务程序中调用
2. **`k_cpu_idle()`**：调用时必须确保有中断可以唤醒 CPU
3. **设备电源管理**：设备获取/释放必须配对使用，避免计数错误

### 8.2 性能考虑
1. **短等待**：使用 `k_busy_wait()` 精确计时
2. **中等等待**：使用 `k_msleep()` 省电
3. **长等待**：使用设备运行时电源管理

### 8.3 调试技巧
1. 启用电源管理日志：`CONFIG_LOG=y`, `CONFIG_LOG_DEFAULT_LEVEL=3`
2. 使用 shell 命令查看设备状态：
   ```
   # 查看所有设备电源状态
   device list

   # 挂起特定设备
   pm suspend device_name

   # 唤醒特定设备
   pm resume device_name
   ```

### 8.4 功耗优化
1. **减少唤醒频率**：适当延长休眠间隔
2. **批量处理**：集中处理多个任务减少上下文切换
3. **关闭不必要的外设**：使用设备运行时电源管理
4. **使用低功耗外设**：选择支持低功耗模式的传感器和通信接口

## 9. 配置选项

要在项目中使用系统级休眠，需要在 prj.conf 中配置：

```conf
# 启用电源管理
CONFIG_PM=y

# 启用设备运行时电源管理
CONFIG_PM_DEVICE_RUNTIME=y

# 启用设备电源管理 shell
CONFIG_PM_DEVICE_SHELL=y

# 启用系统电源管理
CONFIG_PM_SYSTEM=y

# 设置 CPU 空闲电源管理
CONFIG_CPU_PM_RUNTIME=y

# 设置日志级别
CONFIG_LOG=y
CONFIG_LOG_DEFAULT_LEVEL=3
```

## 10. 相关资源

- Zephyr 电源管理文档：`zephyr/doc/services/pm/`
- nRF SDK 电源管理示例：`nrf/samples/subsys/pm/`
- 设备电源管理 API：`zephyr/include/zephyr/pm/device_runtime.h`
- 系统电源管理 API：`zephyr/include/zephyr/pm/pm.h`

---

## 总结

nRF SDK/Zephyr 提供了完整的电源管理 API 体系，从简单的线程休眠到复杂的设备电源管理。正确使用这些 API 可以显著降低系统功耗，延长电池寿命。

**关键原则**：
1. 选择合适的休眠函数
2. 正确管理设备电源状态
3. 合理使用电源状态锁定
4. 注意调用上下文约束