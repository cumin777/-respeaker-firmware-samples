#pragma once

#include <zephyr/device.h>

#ifdef __cplusplus
extern "C" {
#endif

enum button_gesture_action {
	BUTTON_GESTURE_SINGLE = 0,
	BUTTON_GESTURE_DOUBLE = 1,
	BUTTON_GESTURE_LONG = 2,
	BUTTON_GESTURE_ACTION_COUNT,
};

typedef void (*button_gesture_cb_t)(const struct device *dev,
				   enum button_gesture_action action,
				   void *user_data);

/** Register a callback for one specific gesture action. */
int button_gesture_register_callback(const struct device *dev,
				     enum button_gesture_action action,
				     button_gesture_cb_t cb,
				     void *user_data);

#ifdef __cplusplus
}
#endif
