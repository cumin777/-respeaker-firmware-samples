#define DT_DRV_COMPAT respeaker_gpio_button_gesture

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "input/button_gesture.h"

LOG_MODULE_REGISTER(respeaker_button_gesture, CONFIG_LOG_DEFAULT_LEVEL);

struct bg_cfg {
	struct gpio_dt_spec gpio;
	uint32_t debounce_ms;
	uint32_t tap_threshold_ms;
	uint32_t double_click_ms;
	uint32_t long_press_ms;
};

struct bg_data {
	const struct device *dev;
	const struct bg_cfg *cfg;

	struct gpio_callback gpio_cb;
	struct k_work_delayable edge_work;
	struct k_work_delayable long_work;
	struct k_work_delayable tap_finalize_work;

	bool pressed;
	bool armed;
	bool long_sent;
	bool tap_pending;
	uint32_t last_edge_ms;
	uint32_t press_start_ms;
	uint32_t first_tap_release_ms;

	button_gesture_cb_t cb[BUTTON_GESTURE_ACTION_COUNT];
	void *cb_user_data[BUTTON_GESTURE_ACTION_COUNT];
};

static bool gpio_pressed(const struct gpio_dt_spec *spec, bool fallback)
{
	/* gpio_pin_get_dt() returns the *logical* level and already takes
	 * GPIO_ACTIVE_LOW into account *if the pin was configured with that flag*.
	 * Since we use gpio_pin_configure_dt(), the dt_flags (incl. ACTIVE_LOW)
	 * are applied at configuration time.
	 */
	int v = gpio_pin_get_dt(spec);
	if (v < 0) {
		return fallback;
	}

	return (v != 0);
}

static void emit(const struct device *dev, enum button_gesture_action action)
{
	struct bg_data *data = dev->data;
	button_gesture_cb_t cb = data->cb[action];
	if (cb) {
		cb(dev, action, data->cb_user_data[action]);
	}
}

static void long_work_handler(struct k_work *work)
{
	struct k_work_delayable *dw = k_work_delayable_from_work(work);
	struct bg_data *data = CONTAINER_OF(dw, struct bg_data, long_work);

	if (!data->armed) {
		return;
	}
	if (data->pressed && !data->long_sent) {
		data->long_sent = true;
		data->tap_pending = false;
		(void)k_work_cancel_delayable(&data->tap_finalize_work);
		emit(data->dev, BUTTON_GESTURE_LONG);
	}
}

static void tap_finalize_handler(struct k_work *work)
{
	struct k_work_delayable *dw = k_work_delayable_from_work(work);
	struct bg_data *data = CONTAINER_OF(dw, struct bg_data, tap_finalize_work);

	if (!data->armed) {
		return;
	}
	if (data->tap_pending) {
		data->tap_pending = false;
		emit(data->dev, BUTTON_GESTURE_SINGLE);
	}
}

static void handle_press_edge(struct bg_data *data, uint32_t now)
{
	data->pressed = true;
	data->press_start_ms = now;
	data->long_sent = false;
	(void)k_work_reschedule(&data->long_work, K_MSEC(data->cfg->long_press_ms));
}

static void handle_release_edge(struct bg_data *data, uint32_t now)
{
	data->pressed = false;
	(void)k_work_cancel_delayable(&data->long_work);

	if (!data->armed) {
		/* If we booted with the button pressed, arm after the first release. */
		data->armed = true;
		data->tap_pending = false;
		data->long_sent = false;
		(void)k_work_cancel_delayable(&data->tap_finalize_work);
		return;
	}

	if (data->long_sent) {
		return;
	}

	uint32_t held_ms = now - data->press_start_ms;
	if (held_ms > data->cfg->tap_threshold_ms) {
		data->tap_pending = false;
		(void)k_work_cancel_delayable(&data->tap_finalize_work);
		emit(data->dev, BUTTON_GESTURE_SINGLE);
		return;
	}

	if (!data->tap_pending) {
		data->tap_pending = true;
		data->first_tap_release_ms = now;
		(void)k_work_reschedule(&data->tap_finalize_work, K_MSEC(data->cfg->double_click_ms));
		return;
	}

	if ((now - data->first_tap_release_ms) <= data->cfg->double_click_ms) {
		data->tap_pending = false;
		(void)k_work_cancel_delayable(&data->tap_finalize_work);
		emit(data->dev, BUTTON_GESTURE_DOUBLE);
	} else {
		emit(data->dev, BUTTON_GESTURE_SINGLE);
		data->tap_pending = true;
		data->first_tap_release_ms = now;
		(void)k_work_reschedule(&data->tap_finalize_work, K_MSEC(data->cfg->double_click_ms));
	}
}

static void edge_work_handler(struct k_work *work)
{
	struct k_work_delayable *dw = k_work_delayable_from_work(work);
	struct bg_data *data = CONTAINER_OF(dw, struct bg_data, edge_work);

	bool now_pressed = gpio_pressed(&data->cfg->gpio, data->pressed);
	if (now_pressed == data->pressed) {
		return;
	}

	uint32_t now = k_uptime_get_32();
	if ((now - data->last_edge_ms) < data->cfg->debounce_ms) {
		return;
	}
	data->last_edge_ms = now;

	if (now_pressed) {
		if (!data->armed) {
			/* Ignore press edges until we are armed. */
			data->pressed = true;
			return;
		}
		handle_press_edge(data, now);
	} else {
		handle_release_edge(data, now);
	}
}

static void gpio_isr(const struct device *port, struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(port);
	ARG_UNUSED(pins);
	struct bg_data *data = CONTAINER_OF(cb, struct bg_data, gpio_cb);
	(void)k_work_reschedule(&data->edge_work, K_MSEC(data->cfg->debounce_ms));
}

int button_gesture_register_callback(const struct device *dev,
				     enum button_gesture_action action,
				     button_gesture_cb_t cb,
				     void *user_data)
{
	if (!dev) {
		return -EINVAL;
	}
	if (action >= BUTTON_GESTURE_ACTION_COUNT) {
		return -EINVAL;
	}

	struct bg_data *data = dev->data;
	data->cb[action] = cb;
	data->cb_user_data[action] = user_data;
	return 0;
}

static int bg_init(const struct device *dev)
{
	struct bg_data *data = dev->data;
	const struct bg_cfg *cfg = dev->config;

	data->dev = dev;
	data->cfg = cfg;

	if (!gpio_is_ready_dt(&cfg->gpio)) {
		LOG_ERR("GPIO not ready");
		return -ENODEV;
	}

	int ret = gpio_pin_configure_dt(&cfg->gpio, GPIO_INPUT);
	if (ret < 0) {
		LOG_ERR("gpio_pin_configure_dt failed: %d", ret);
		return ret;
	}

	ret = gpio_pin_interrupt_configure_dt(&cfg->gpio, GPIO_INT_EDGE_BOTH);
	if (ret < 0) {
		LOG_ERR("gpio_pin_interrupt_configure_dt failed: %d", ret);
		return ret;
	}

	gpio_init_callback(&data->gpio_cb, gpio_isr, BIT(cfg->gpio.pin));
	ret = gpio_add_callback(cfg->gpio.port, &data->gpio_cb);
	if (ret < 0) {
		LOG_ERR("gpio_add_callback failed: %d", ret);
		return ret;
	}

	k_work_init_delayable(&data->edge_work, edge_work_handler);
	k_work_init_delayable(&data->long_work, long_work_handler);
	k_work_init_delayable(&data->tap_finalize_work, tap_finalize_handler);

	data->pressed = gpio_pressed(&cfg->gpio, false);
	data->armed = !data->pressed;
	data->long_sent = false;
	data->tap_pending = false;
	data->last_edge_ms = k_uptime_get_32();
	data->press_start_ms = data->last_edge_ms;
	data->first_tap_release_ms = 0U;

	for (int i = 0; i < BUTTON_GESTURE_ACTION_COUNT; i++) {
		data->cb[i] = NULL;
		data->cb_user_data[i] = NULL;
	}

	if (data->pressed) {
		LOG_WRN("Button pressed at boot; arming after release");
	}

	LOG_INF("GPIO button gesture ready (debounce=%ums tap=%ums dbl=%ums long=%ums)",
		(unsigned)cfg->debounce_ms,
		(unsigned)cfg->tap_threshold_ms,
		(unsigned)cfg->double_click_ms,
		(unsigned)cfg->long_press_ms);
	return 0;
}

#define BG_CFG(inst)                                                                                \
	{                                                                                              \
		.gpio = GPIO_DT_SPEC_INST_GET(inst, gpios),                                              \
		.debounce_ms = DT_INST_PROP_OR(inst, debounce_ms, 20),                                  \
		.tap_threshold_ms = DT_INST_PROP_OR(inst, tap_threshold_ms, 300),                        \
		.double_click_ms = DT_INST_PROP_OR(inst, double_click_ms, 600),                         \
		.long_press_ms = DT_INST_PROP_OR(inst, long_press_ms, 1000),                            \
	}

#define BG_DEFINE(inst)                                                                             \
	static struct bg_data bg_data_##inst;                                                         \
	static const struct bg_cfg bg_cfg_##inst = BG_CFG(inst);                                      \
	DEVICE_DT_INST_DEFINE(inst, bg_init, NULL, &bg_data_##inst, &bg_cfg_##inst, POST_KERNEL,      \
			     CONFIG_KERNEL_INIT_PRIORITY_DEVICE, NULL);

DT_INST_FOREACH_STATUS_OKAY(BG_DEFINE)
