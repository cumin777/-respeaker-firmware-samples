#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/wifi_utils.h>
#include <zephyr/net/dhcpv4_server.h>

#include <zephyr/sys/util.h>
#include <zephyr/sys/printk.h>

#include <net/wifi_ready.h>

LOG_MODULE_REGISTER(respeaker_wifi_softap, CONFIG_LOG_DEFAULT_LEVEL);

#define WIFI_AP_MGMT_EVENTS                                                                    \
	(NET_EVENT_WIFI_AP_ENABLE_RESULT | NET_EVENT_WIFI_AP_DISABLE_RESULT |                    \
	 NET_EVENT_WIFI_AP_STA_CONNECTED | NET_EVENT_WIFI_AP_STA_DISCONNECTED)

static struct net_mgmt_event_callback wifi_ap_mgmt_cb;
static K_SEM_DEFINE(wifi_ready_sem, 0, 1);
static bool wifi_ready;

static K_MUTEX_DEFINE(sta_list_lock);

struct sta_node {
	bool valid;
	struct wifi_ap_sta_info info;
};

static struct sta_node sta_list[CONFIG_RESPEAKER_WIFI_SOFTAP_MAX_STATIONS];

static struct net_if *wifi_iface;
static int64_t last_activity_ms;
static bool ap_running;

#if IS_ENABLED(CONFIG_RESPEAKER_WIFI_SOFTAP_AUTO_DISABLE_IDLE)
static void idle_power_mgr_work_fn(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(idle_power_mgr_work, idle_power_mgr_work_fn);
#endif

static int sta_count_unlocked(void)
{
	int count = 0;

	for (int i = 0; i < ARRAY_SIZE(sta_list); i++) {
		if (sta_list[i].valid) {
			count++;
		}
	}

	return count;
}

static void note_activity(void)
{
	last_activity_ms = k_uptime_get();
}

static const char *mac_to_str(const uint8_t *mac, char *buf, size_t buf_len)
{
	if (!mac || !buf || buf_len < sizeof("xx:xx:xx:xx:xx:xx")) {
		return "(invalid)";
	}

	snprintk(buf, buf_len, "%02x:%02x:%02x:%02x:%02x:%02x",
		 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	return buf;
}

static void print_sta_list_unlocked(void)
{
	size_t id = 1;

	LOG_INF("AP stations:");
	LOG_INF("============");

	for (int i = 0; i < ARRAY_SIZE(sta_list); i++) {
		if (!sta_list[i].valid) {
			continue;
		}

		char mac_buf[sizeof("xx:xx:xx:xx:xx:xx")];
		LOG_INF("Station %zu MAC: %s", id++, mac_to_str(sta_list[i].info.mac, mac_buf, sizeof(mac_buf)));
	}

	if (id == 1) {
		LOG_INF("No stations connected");
	}
}

static void on_wifi_ready(bool ready)
{
	wifi_ready = ready;
	k_sem_give(&wifi_ready_sem);
}

static void handle_ap_enable_result(struct net_mgmt_event_callback *cb)
{
	const struct wifi_status *status = (const struct wifi_status *)cb->info;

	if (status && status->status) {
		LOG_ERR("AP enable failed (%d)", status->status);
		return;
	}

	LOG_INF("AP enable requested");
	ap_running = true;
	note_activity();
}

static void handle_ap_disable_result(struct net_mgmt_event_callback *cb)
{
	const struct wifi_status *status = (const struct wifi_status *)cb->info;

	if (status && status->status) {
		LOG_ERR("AP disable failed (%d)", status->status);
		return;
	}

	LOG_INF("AP disable requested");
	ap_running = false;
	note_activity();
}

static void handle_sta_connected(struct net_mgmt_event_callback *cb)
{
	const struct wifi_ap_sta_info *sta = (const struct wifi_ap_sta_info *)cb->info;
	char mac_buf[sizeof("xx:xx:xx:xx:xx:xx")];

	LOG_INF("Station connected: %s", mac_to_str(sta->mac, mac_buf, sizeof(mac_buf)));
	note_activity();

	k_mutex_lock(&sta_list_lock, K_FOREVER);
	for (int i = 0; i < ARRAY_SIZE(sta_list); i++) {
		if (!sta_list[i].valid) {
			sta_list[i].info = *sta;
			sta_list[i].valid = true;
			break;
		}
	}
	print_sta_list_unlocked();
	k_mutex_unlock(&sta_list_lock);
}

static void handle_sta_disconnected(struct net_mgmt_event_callback *cb)
{
	const struct wifi_ap_sta_info *sta = (const struct wifi_ap_sta_info *)cb->info;
	char mac_buf[sizeof("xx:xx:xx:xx:xx:xx")];

	LOG_INF("Station disconnected: %s", mac_to_str(sta->mac, mac_buf, sizeof(mac_buf)));
	note_activity();

	k_mutex_lock(&sta_list_lock, K_FOREVER);
	for (int i = 0; i < ARRAY_SIZE(sta_list); i++) {
		if (!sta_list[i].valid) {
			continue;
		}

		if (memcmp(sta_list[i].info.mac, sta->mac, WIFI_MAC_ADDR_LEN) == 0) {
			sta_list[i].valid = false;
			break;
		}
	}
	print_sta_list_unlocked();
	k_mutex_unlock(&sta_list_lock);
}

static void wifi_mgmt_event_handler(struct net_mgmt_event_callback *cb,
				   uint64_t mgmt_event, struct net_if *iface)
{
	ARG_UNUSED(iface);

	switch (mgmt_event) {
	case NET_EVENT_WIFI_AP_ENABLE_RESULT:
		handle_ap_enable_result(cb);
		break;
	case NET_EVENT_WIFI_AP_DISABLE_RESULT:
		handle_ap_disable_result(cb);
		break;
	case NET_EVENT_WIFI_AP_STA_CONNECTED:
		handle_sta_connected(cb);
		break;
	case NET_EVENT_WIFI_AP_STA_DISCONNECTED:
		handle_sta_disconnected(cb);
		break;
	default:
		break;
	}
}

static int set_reg_domain(struct net_if *iface)
{
	struct wifi_reg_domain regd = { 0 };

	regd.oper = WIFI_MGMT_SET;
	strncpy(regd.country_code, CONFIG_RESPEAKER_WIFI_SOFTAP_REG_DOMAIN, WIFI_COUNTRY_CODE_LEN);
	regd.country_code[WIFI_COUNTRY_CODE_LEN] = '\0';

	int ret = net_mgmt(NET_REQUEST_WIFI_REG_DOMAIN, iface, &regd, sizeof(regd));
	if (ret) {
		LOG_WRN("Failed to set regulatory domain (%d)", ret);
		return ret;
	}

	LOG_INF("Regulatory domain set to %s", CONFIG_RESPEAKER_WIFI_SOFTAP_REG_DOMAIN);
	return 0;
}

static int start_dhcp_server(struct net_if *iface)
{
	struct in_addr pool_start;

	if (net_addr_pton(AF_INET, CONFIG_RESPEAKER_WIFI_SOFTAP_DHCPV4_POOL_START,
			 &pool_start.s_addr)) {
		LOG_ERR("Invalid DHCP pool start: %s", CONFIG_RESPEAKER_WIFI_SOFTAP_DHCPV4_POOL_START);
		return -EINVAL;
	}

	int ret = net_dhcpv4_server_start(iface, &pool_start);
	if (ret == -EALREADY) {
		LOG_WRN("DHCPv4 server already running");
		return 0;
	}
	if (ret < 0) {
		LOG_ERR("DHCPv4 server start failed: %d", ret);
		return ret;
	}

	LOG_INF("DHCPv4 server started, pool from %s", CONFIG_RESPEAKER_WIFI_SOFTAP_DHCPV4_POOL_START);
	return 0;
}

static void stop_dhcp_server(struct net_if *iface)
{
	int ret = net_dhcpv4_server_stop(iface);
	if (ret == -EALREADY) {
		return;
	}
	if (ret < 0) {
		LOG_WRN("DHCPv4 server stop failed: %d", ret);
	}
}

static int disable_softap(struct net_if *iface)
{
	int ret = net_mgmt(NET_REQUEST_WIFI_AP_DISABLE, iface, NULL, 0);
	if (ret) {
		LOG_WRN("NET_REQUEST_WIFI_AP_DISABLE failed: %d", ret);
	} else {
		LOG_INF("AP disable requested (idle power save)");
	}

	stop_dhcp_server(iface);
	net_if_down(iface);
	ap_running = false;
	return ret;
}

#if IS_ENABLED(CONFIG_RESPEAKER_WIFI_SOFTAP_AUTO_DISABLE_IDLE)
static void idle_power_mgr_work_fn(struct k_work *work)
{
	ARG_UNUSED(work);

	if (!wifi_iface) {
		return;
	}

	if (!ap_running) {
		return;
	}

	int connected = 0;
	k_mutex_lock(&sta_list_lock, K_FOREVER);
	connected = sta_count_unlocked();
	k_mutex_unlock(&sta_list_lock);

	if (connected == 0) {
		int64_t idle_ms = k_uptime_get() - last_activity_ms;
		if (idle_ms >= (int64_t)CONFIG_RESPEAKER_WIFI_SOFTAP_IDLE_TIMEOUT_SEC * 1000) {
			LOG_INF("No stations for %d sec, disabling SoftAP to save power",
				CONFIG_RESPEAKER_WIFI_SOFTAP_IDLE_TIMEOUT_SEC);
			(void)disable_softap(wifi_iface);
			return;
		}
	}

	k_work_schedule(&idle_power_mgr_work, K_SECONDS(CONFIG_RESPEAKER_WIFI_SOFTAP_IDLE_CHECK_PERIOD_SEC));
}
#endif

static int configure_ap_ipv4(struct net_if *iface)
{
	struct in_addr addr = { 0 };
	struct in_addr netmask = { 0 };
	struct in_addr gw = { 0 };

	if (net_addr_pton(AF_INET, CONFIG_NET_CONFIG_MY_IPV4_ADDR, &addr)) {
		LOG_ERR("Invalid CONFIG_NET_CONFIG_MY_IPV4_ADDR: %s", CONFIG_NET_CONFIG_MY_IPV4_ADDR);
		return -EINVAL;
	}

	if (net_addr_pton(AF_INET, CONFIG_NET_CONFIG_MY_IPV4_NETMASK, &netmask)) {
		LOG_ERR("Invalid CONFIG_NET_CONFIG_MY_IPV4_NETMASK: %s", CONFIG_NET_CONFIG_MY_IPV4_NETMASK);
		return -EINVAL;
	}

	if (net_addr_pton(AF_INET, CONFIG_NET_CONFIG_MY_IPV4_GW, &gw)) {
		LOG_ERR("Invalid CONFIG_NET_CONFIG_MY_IPV4_GW: %s", CONFIG_NET_CONFIG_MY_IPV4_GW);
		return -EINVAL;
	}

	net_if_ipv4_set_gw(iface, &gw);

	if (net_if_ipv4_addr_add(iface, &addr, NET_ADDR_MANUAL, 0) == NULL) {
		LOG_WRN("IPv4 address add failed (may already be set)");
	}

	if (!net_if_ipv4_set_netmask_by_addr(iface, &addr, &netmask)) {
		LOG_WRN("Unable to set netmask for AP interface: %s", CONFIG_NET_CONFIG_MY_IPV4_NETMASK);
	}

	LOG_INF("AP IPv4 configured: %s", CONFIG_NET_CONFIG_MY_IPV4_ADDR);
	return 0;
}

static int enable_softap(struct net_if *iface)
{
	static struct wifi_connect_req_params ap = { 0 };

#ifdef CONFIG_RESPEAKER_WIFI_SOFTAP_BAND_5_GHZ
	ap.band = WIFI_FREQ_BAND_5_GHZ;
#else
	ap.band = WIFI_FREQ_BAND_2_4_GHZ;
#endif
	ap.channel = CONFIG_RESPEAKER_WIFI_SOFTAP_CHANNEL;

	ap.ssid = (const uint8_t *)CONFIG_RESPEAKER_WIFI_SOFTAP_SSID;
	ap.ssid_length = strlen((const char *)ap.ssid);
	if (ap.ssid_length > WIFI_SSID_MAX_LEN) {
		LOG_ERR("SSID too long (%u)", ap.ssid_length);
		return -EINVAL;
	}

	if (!wifi_utils_validate_chan(ap.band, ap.channel)) {
		LOG_ERR("Invalid channel %u in band %u", ap.channel, ap.band);
		return -EINVAL;
	}

	if (strlen(CONFIG_RESPEAKER_WIFI_SOFTAP_PASSWORD) == 0) {
		ap.security = WIFI_SECURITY_TYPE_NONE;
		ap.psk = NULL;
		ap.psk_length = 0;
		LOG_INF("Starting open SoftAP: %s", CONFIG_RESPEAKER_WIFI_SOFTAP_SSID);
	} else {
		ap.security = WIFI_SECURITY_TYPE_PSK;
		ap.psk = (const uint8_t *)CONFIG_RESPEAKER_WIFI_SOFTAP_PASSWORD;
		ap.psk_length = strlen((const char *)ap.psk);
		LOG_INF("Starting WPA2-PSK SoftAP: %s", CONFIG_RESPEAKER_WIFI_SOFTAP_SSID);
	}

	int ret = net_mgmt(NET_REQUEST_WIFI_AP_ENABLE, iface, &ap, sizeof(ap));
	if (ret) {
		LOG_ERR("NET_REQUEST_WIFI_AP_ENABLE failed: %d", ret);
		return ret;
	}

	return 0;
}

int main(void)
{
	int ret;

	LOG_INF("reSpeaker Wi-Fi SoftAP sample starting");

	struct net_if *iface = NULL;

	/* Some configs expose a dedicated SAP iface; otherwise fall back to first Wi-Fi iface. */
	for (int i = 0; i < 50; i++) {
		iface = net_if_get_wifi_sap();
		if (!iface) {
			iface = net_if_get_first_wifi();
		}
		if (iface) {
			break;
		}
		k_sleep(K_MSEC(100));
	}

	if (!iface) {
		LOG_ERR("No Wi-Fi interface found (net_if_get_wifi_sap/first_wifi returned NULL)");
		LOG_ERR("This usually means the image was built without the Wi-Fi driver or for the wrong board.");
		LOG_ERR("Check Kconfig: CONFIG_WIFI=%d CONFIG_WIFI_NRF70=%d CONFIG_NRF70_AP_MODE=%d",
			IS_ENABLED(CONFIG_WIFI), IS_ENABLED(CONFIG_WIFI_NRF70), IS_ENABLED(CONFIG_NRF70_AP_MODE));
		LOG_ERR("Fix: rebuild for nrf7002dk/nrf5340/cpuapp and ensure prj.conf enables Wi-Fi, then flash the new image.");
		return -ENODEV;
	}
	LOG_INF("WiFi interface found");

	wifi_ready_callback_t ready_cb = {
		.wifi_ready_cb = on_wifi_ready,
		.iface = iface,
	};

	ret = register_wifi_ready_callback(ready_cb, iface);
	if (ret) {
		LOG_WRN("register_wifi_ready_callback failed: %d", ret);
	}

	net_mgmt_init_event_callback(&wifi_ap_mgmt_cb, wifi_mgmt_event_handler, WIFI_AP_MGMT_EVENTS);
	net_mgmt_add_event_callback(&wifi_ap_mgmt_cb);

	/* Wait until Wi-Fi is reported ready (or timeout to continue anyway). */
	(void)k_sem_take(&wifi_ready_sem, K_SECONDS(10));
	LOG_INF("Wi-Fi ready: %s", wifi_ready ? "yes" : "no/timeout");

	(void)set_reg_domain(iface);
	(void)configure_ap_ipv4(iface);
	(void)start_dhcp_server(iface);

	ret = enable_softap(iface);
	if (ret) {
		LOG_ERR("SoftAP enable failed: %d", ret);
	}

	wifi_iface = iface;
	ap_running = (ret == 0);
	note_activity();

#if IS_ENABLED(CONFIG_RESPEAKER_WIFI_SOFTAP_AUTO_DISABLE_IDLE)
	k_work_schedule(&idle_power_mgr_work, K_SECONDS(CONFIG_RESPEAKER_WIFI_SOFTAP_IDLE_CHECK_PERIOD_SEC));
#endif

	while (1) {
		k_sleep(K_SECONDS(5));
	}
}
