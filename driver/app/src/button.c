#include "button.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/devicetree.h>

#include "input/button_gesture.h"

LOG_MODULE_REGISTER(app_button, CONFIG_LOG_DEFAULT_LEVEL);

K_MSGQ_DEFINE(button_evt_q, sizeof(button_event_t), 8, 4);

static const struct device *btn_gesture_dev;

static void button_emit(button_event_t evt)
{
	(void)k_msgq_put(&button_evt_q, &evt, K_NO_WAIT);
}

static void gesture_cb(const struct device *dev, enum button_gesture_action action, void *user_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(user_data);

	switch (action) {
	case BUTTON_GESTURE_SINGLE:
		button_emit(BUTTON_EVENT_SHORT_PRESS);
		break;
	case BUTTON_GESTURE_DOUBLE:
		button_emit(BUTTON_EVENT_DOUBLE_PRESS);
		break;
	case BUTTON_GESTURE_LONG:
		button_emit(BUTTON_EVENT_LONG_PRESS);
		break;
	default:
		break;
	}
}

int button_init(void)
{
	/* Bind to the first enabled gesture device instance.
	 * The concrete GPIO/polarity is provided by devicetree.
	 */
#if DT_HAS_COMPAT_STATUS_OKAY(respeaker_gpio_button_gesture)
	btn_gesture_dev = DEVICE_DT_GET_ANY(respeaker_gpio_button_gesture);
#else
	btn_gesture_dev = NULL;
#endif

	if (!btn_gesture_dev) {
		LOG_ERR("No gesture device in devicetree (compatible respeaker,gpio-button-gesture)");
		return -ENODEV;
	}
	if (!device_is_ready(btn_gesture_dev)) {
		LOG_ERR("Gesture device not ready");
		return -ENODEV;
	}

	k_msgq_purge(&button_evt_q);
	int ret = 0;
	ret |= button_gesture_register_callback(btn_gesture_dev, BUTTON_GESTURE_SINGLE, gesture_cb, NULL);
	ret |= button_gesture_register_callback(btn_gesture_dev, BUTTON_GESTURE_DOUBLE, gesture_cb, NULL);
	ret |= button_gesture_register_callback(btn_gesture_dev, BUTTON_GESTURE_LONG, gesture_cb, NULL);
	if (ret != 0) {
		LOG_ERR("Failed to register gesture callbacks: %d", ret);
		return ret;
	}

	LOG_INF("Button wrapper bound to gesture device");
	return 0;
}

int button_get_event(button_event_t *evt, k_timeout_t timeout)
{
	if (!evt) {
		return -EINVAL;
	}

	return k_msgq_get(&button_evt_q, evt, timeout);
}
