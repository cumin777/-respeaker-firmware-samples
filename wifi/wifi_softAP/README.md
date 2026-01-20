# reSpeaker Wi-Fi SoftAP sample

Enables a Wi-Fi hotspot (SoftAP) on nRF7002DK (nRF5340 cpuapp), starts a DHCPv4 server, and logs stations connecting/disconnecting.

## Build

From NCS root:

- `west build -b nrf7002dk/nrf5340/cpuapp respeaker_sample/wifi -p`

If you previously built this app before enabling Wi‑Fi, do a pristine rebuild (important for sysbuild):
- Delete `respeaker_sample/wifi/build/` and rebuild, or use `-p always`.

Note: This app uses sysbuild. Wi‑Fi for nRF70 must be enabled via `sysbuild.conf` (`SB_CONFIG_WIFI_NRF70=y`).

## Configure

Key Kconfig options (menuconfig):
- `CONFIG_RESPEAKER_WIFI_SOFTAP_SSID`
- `CONFIG_RESPEAKER_WIFI_SOFTAP_PASSWORD`
- `CONFIG_RESPEAKER_WIFI_SOFTAP_REG_DOMAIN`
- `CONFIG_RESPEAKER_WIFI_SOFTAP_CHANNEL`
- `CONFIG_RESPEAKER_WIFI_SOFTAP_DHCPV4_POOL_START`

Low power behavior (optional):
- `CONFIG_RESPEAKER_WIFI_SOFTAP_AUTO_DISABLE_IDLE=y` (default)
- `CONFIG_RESPEAKER_WIFI_SOFTAP_IDLE_TIMEOUT_SEC`
- `CONFIG_RESPEAKER_WIFI_SOFTAP_IDLE_CHECK_PERIOD_SEC`

Default AP IP is set via `CONFIG_NET_CONFIG_MY_IPV4_ADDR` (see `prj.conf`).
