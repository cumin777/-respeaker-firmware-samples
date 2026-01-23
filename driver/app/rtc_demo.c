/*
 * RTC Demo - 获取RTC时间并每秒打印一次
 *
 * 功能：
 * 1. 获取RTC设备
 * 2. 主循环中每秒读取RTC时间
 * 3. 将时间打印到日志
 */

#include <zephyr/device.h>
#include <zephyr/drivers/rtc.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(rtc_demo, LOG_LEVEL_INF);

/* RTC设备实例（需要在设备树中定义） */
static const struct device *rtc_dev;

/**
 * @brief 格式化RTC时间为字符串
 *
 * @param timeptr RTC时间结构体指针
 * @param buf 输出缓冲区
 * @param len 缓冲区长度
 */
static void format_rtc_time(const struct rtc_time *timeptr, char *buf, size_t len)
{
    if (timeptr == NULL || buf == NULL || len == 0) {
        return;
    }

    /* 格式：YYYY-MM-DD HH:MM:SS */
    snprintk(buf, len, "%04d-%02d-%02d %02d:%02d:%02d",
              timeptr->tm_year + 1900,
              timeptr->tm_mon + 1,
              timeptr->tm_mday,
              timeptr->tm_hour,
              timeptr->tm_min,
              timeptr->tm_sec);
}

int main(void)
{
    struct rtc_time rtc_time;
    int ret;
    char time_str[32];
    uint32_t last_print_sec = 0;
    const uint32_t rtc_print_interval = 1000; /* 1秒 = 1000毫秒 */

    /* 获取RTC设备 */
    rtc_dev = DEVICE_DT_GET(DT_NODELABEL(rtc));
    if (!device_is_ready(rtc_dev)) {
        LOG_ERR("RTC device not ready");
        return 0;
    }

    LOG_INF("========================================");
    LOG_INF("RTC Demo Started");
    LOG_INF("========================================");
    LOG_INF("RTC device ready: %s", rtc_dev->name);

    /* 读取并打印初始RTC时间 */
    ret = rtc_get_time(rtc_dev, &rtc_time);
    if (ret == 0) {
        format_rtc_time(&rtc_time, time_str, sizeof(time_str));
        LOG_INF("Initial RTC time: %s", time_str);
    } else if (ret == -ENODATA) {
        LOG_WRN("RTC time has not been set yet");
        LOG_INF("Please set RTC time first using rtc_set_time()");
    } else {
        LOG_ERR("Failed to get RTC time: %d", ret);
        return 0;
    }

    LOG_INF("Printing RTC time every %u second(s)...",
             (unsigned)(rtc_print_interval / 1000));
    LOG_INF("========================================");

    /* 主循环：每秒打印一次RTC时间 */
    while (1) {
        uint32_t current_ms = k_uptime_get_32();

        /* 检查是否到达打印时间间隔 */
        if ((current_ms - last_print_sec) >= rtc_print_interval) {
            /* 读取RTC时间 */
            ret = rtc_get_time(rtc_dev, &rtc_time);
            if (ret == 0) {
                format_rtc_time(&rtc_time, time_str, sizeof(time_str));
                LOG_INF("RTC time: %s (uptime: %llu ms)",
                         time_str,
                         (unsigned long long)current_ms);
            } else if (ret == -ENODATA) {
                LOG_WRN("RTC time not set");
            } else {
                LOG_ERR("Failed to get RTC time: %d", ret);
            }

            last_print_sec = current_ms;
        }

        /* CPU休眠一小段时间，降低功耗 */
        k_sleep(K_MSEC(100));
    }

    return 0;
}
