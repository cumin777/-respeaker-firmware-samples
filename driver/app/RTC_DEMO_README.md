# RTC Demo 使用说明

## 功能说明

这个例程演示如何：
1. 获取RTC设备实例
2. 读取当前RTC时间
3. 在主循环中每秒打印一次RTC时间到日志

---

## 文件说明

| 文件 | 说明 |
|------|------|
| `rtc_demo.c` | RTC演示主程序 |
| `prj.conf` | 已添加RTC配置 |

---

## 编译和运行

### 前提条件

1. Nordic nRF Connect SDK v3.2.1
2. nRF5340DK 开发板

### 设备树配置

**重要：** 需要在开发板的设备树（DTS）中添加RTC节点定义。

#### 选项1：使用设备树覆盖文件

在项目目录下创建或编辑 `boards/nrf5340dk_nrf5340_cpuapp.overlay` 文件：

```dts
/ {
    soc {
        nrf5340_cpuapp@50000000 {
            rtc: rtc@4000d000 {
                compatible = "nordic,nrf-rtc";
                reg = <0x4000d000 0x1000>;
                interrupts = <20 0>;
                status = "okay";
                clock-frequency = <32768>;
            };
        };
    };
};
```

#### 选项2：检查现有设备树

检查 nRF5340 的设备树是否已包含RTC节点：
```bash
cd /mnt/c/ncs/v3.2.1/nrf
find . -name "*.dts" -o -name "*.dtsi" | xargs grep -l "nrf-rtc"
```

如果已存在，则直接编译即可。

### 编译

```bash
cd /mnt/c/ncs/v3.2.1/respeaker_sample/driver/app
west build -b nrf5340dk_nrf5340_cpuapp -p always
```

### 烧录

```bash
cd /mnt/c/ncs/v3.2.1/respeaker_sample/driver/app
west flash
```

### 查看日志

#### 方式1：使用RTT Viewer
```bash
# 在nRF Connect for Desktop中打开RTT Viewer
```

#### 方式2：使用串口终端
```bash
# 确保串口已连接并配置正确的波特率
```

---

## 日志输出示例

程序启动后，串口/RTT终端会输出：

```
========================================
RTC Demo Started
========================================
RTC device ready: 4000d000
Initial RTC time: 2025-01-22 14:30:45
Printing RTC time every 1 second(s)...
========================================
<00:00:03> RTC time: 2025-01-22 14:30:45 (uptime: 3000 ms)
<00:00:04> RTC time: 2025-01-22 14:30:46 (uptime: 4000 ms)
<00:00:05> RTC time: 2025-01-22 14:30:47 (uptime: 5000 ms)
...
```

如果RTC时间尚未设置，会看到：
```
<00:00:03> RTC time not set
```

---

## 设置RTC时间

### 方法1：使用shell命令（如果有RTC shell支持）

在运行时通过串口设置：

```
uart:~$ rtc set 2025 1 22 14 35 0
uart:~$ rtc get
```

### 方法2：在代码中设置RTC时间

修改 `rtc_demo.c`，在 `main()` 函数开头添加：

```c
/* 设置RTC时间为当前日期时间 */
struct rtc_time time_to_set = {
    .tm_year = 125,    /* 2025年 (1900+125) */
    .tm_mon = 0,        /* 1月 (0=1月) */
    .tm_mday = 22,      /* 22日 */
    .tm_hour = 14,     /* 14时 */
    .tm_min = 30,      /* 30分 */
    .tm_sec = 0,       /* 0秒 */
    .tm_wday = 3,      /* 周三 (0=周日) */
    .tm_isdst = 0,      /* 无夏令时 */
    .tm_nsec = 0,       /* 无纳秒 */
};

ret = rtc_set_time(rtc_dev, &time_to_set);
if (ret != 0) {
    LOG_ERR("Failed to set RTC time: %d", ret);
} else {
    LOG_INF("RTC time set successfully");
}
```

### 方法3：使用外部NTP或其他时间源

从网络时间服务器获取时间后调用 `rtc_set_time()`。

---

## 配置选项说明

### prj.conf中的RTC配置

| 配置项 | 说明 |
|--------|------|
| `CONFIG_RTC=y` | 启用RTC驱动 |
| `CONFIG_RTC_ALARM` | 启用RTC闹钟功能（可选） |
| `CONFIG_RTC_UPDATE` | 启用RTC更新回调（可选） |
| `CONFIG_RTC_CALIBRATION` | 启用RTC校准功能（可选） |

---

## RTC API说明

### 核心API

| API | 功能 |
|-----|------|
| `rtc_get_time()` | 获取RTC当前时间 |
| `rtc_set_time()` | 设置RTC时间 |

### rtc_time 结构体

```c
struct rtc_time {
    int tm_sec;     /* 秒 [0, 59] */
    int tm_min;     /* 分 [0, 59] */
    int tm_hour;    /* 时 [0, 23] */
    int tm_mday;    /* 月中的日 [1, 31] */
    int tm_mon;     /* 月 [0, 11] (0=1月) */
    int tm_year;    /* 年份 - 1900 */
    int tm_wday;    /* 星期 [0, 6] (0=周日) */
    int tm_yday;    /* 一年中的第几天 [0, 365] */
    int tm_isdst;   /* 夏令时标志 */
    int tm_nsec;    /* 纳秒 [0, 999999999] */
};
```

---

## 功耗优化

当前例程每100ms唤醒一次，这对于演示是合理的。对于实际应用，可以调整：

```c
/* 降低唤醒频率到每秒一次 */
k_sleep(K_SECONDS(1));

/* 或者使用更长的睡眠间隔 */
k_sleep(K_SECONDS(10));
```

---

## 故障排查

### 问题1：RTC device not ready

**原因：** 设备树中没有RTC节点定义

**解决方法：** 添加设备树overlay文件（见上文）

### 问题2：RTC time not set

**原因：** RTC尚未初始化

**解决方法：** 使用 `rtc_set_time()` 设置初始时间

### 问题3：编译错误

```
undefined reference to 'rtc_get_time'
```

**原因：** `CONFIG_RTC` 未启用

**解决方法：** 确认 `prj.conf` 中有 `CONFIG_RTC=y`

---

## 扩展建议

1. **添加RTC闹钟功能**：启用 `CONFIG_RTC_ALARM` 配置项
2. **添加RTC更新回调**：启用 `CONFIG_RTC_UPDATE` 获取每秒更新事件
3. **添加RTC校准**：启用 `CONFIG_RTC_CALIBRATION` 精确调整RTC频率
4. **集成NTP客户端**：从网络自动同步时间
5. **保存时间到Flash**：实现时间持久化，断电后恢复

---

## 参考资料

- [Zephyr RTC文档](https://docs.zephyrproject.org/latest/hardware/peripherals/rtc.html)
- [nRF5340 Product Specification](https://infocenter.nordicsemi.com/index.jsp)
- [NCS v3.2.1 文档](https://developer.nordicsemi.com/nRF_Connect_SDK/doc/)
