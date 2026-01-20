# nRF 官方 Wi‑Fi Fundamentals Lesson 1 — 解读说明

## 现状（透明说明）
我在当前环境里尝试抓取你给的 Nordic Academy 页面时，遇到 Cloudflare challenge（HTTP 403 / 连接重置），因此**无法基于原文逐段解读**。

如果你希望我严格按课程原文总结：
- 你可以把页面正文复制粘贴到聊天里，或截几张关键段落/图示；我就能按原文提炼要点并映射到 nRF7002/NCS 的实现。

## 先给你一个“通用版”Lesson 1 Wi‑Fi Fundamentals 框架（不依赖该页面原文）
通常 Wi‑Fi Fundamentals 第 1 课会覆盖这些概念（用于后续固件开发对齐术语）：
- **角色**：Station (STA) 与 Access Point (AP)
- **标识**：SSID（网络名），BSSID（AP 的 MAC/标识），可见网络/隐藏网络
- **信道与频段**：2.4GHz / 5GHz，信道重叠与干扰（影响吞吐与功耗）
- **连接流程**：扫描 → 认证(Authentication) → 关联(Association) → DHCP/获得 IP
- **安全**：Open / WPA2 / WPA3（企业网 802.1X 可能在后续课程）
- **质量指标**：RSSI/SNR、重传、吞吐、时延

## 映射到本项目（reSpeaker）最直接的落地点
- 需求书提到 **Wi‑Fi 用于高速导出录音**：
  - 需要明确是 “设备开 AP（手机直连）” 还是 “设备 STA 入网（同 LAN 传输）” 或两者兼容。
- 配网要求 “AP + BLE 辅助”：
  - 常见做法：手机用 BLE 写入 SSID/PSK → 设备切 STA 连接；
  - 或设备开 AP，手机连上后用 HTTP 配置（但需求里又强调 BLE 辅助）。

## 下一步我需要你提供的最小输入
- Nordic Academy 该页面的文本（粘贴 1~2 屏也可以），或截图。
- 你希望采用的拓扑：
  - A) 设备开 AP，手机加入后 HTTP 拉文件
  - B) 设备 STA 入网，用 HTTP/Socket 传
  - C) A+B 都支持
