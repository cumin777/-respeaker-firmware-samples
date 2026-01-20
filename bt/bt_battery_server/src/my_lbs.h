/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef BT_LBS_H_
#define BT_LBS_H_

/**@file
 * @defgroup bt_lbs LED Button Service API
 * @{
 * @brief API for the LED Button Service (LBS).
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <zephyr/types.h>

#define BT_UUID_LBS_VAL \
	BT_UUID_128_ENCODE(0x00001523, 0x1212, 0xefde, 0x1523, 0x785feabcd123)
#define BT_UUID_LBS_BUTTON_VAL \
	BT_UUID_128_ENCODE(0x00001524, 0x1212, 0xefde, 0x1523, 0x785feabcd123)
#define BT_UUID_LBS_LED_VAL \
  BT_UUID_128_ENCODE(0x00001525, 0x1212, 0xefde, 0x1523, 0x785feabcd123)

#define BT_UUID_LBS BT_UUID_DECLARE_128(BT_UUID_LBS_VAL)
#define BT_UUID_LBS_BUTTON BT_UUID_DECLARE_128(BT_UUID_LBS_BUTTON_VAL)
#define BT_UUID_LBS_LED BT_UUID_DECLARE_128(BT_UUID_LBS_LED_VAL)

#define BT_UUID_BAS_VAL \
	BT_UUID_128_ENCODE (0x0000180F, 0x0000, 0x1000, 0x8000, 0x00805F9B34FB)
#define BT_UUID_BAS_BATTERY_LEVEL_VAL \
    BT_UUID_128_ENCODE(0x00002A19, 0x0000, 0x1000, 0x8000, 0x00805F9B34FB)

#define BT_UUID_BAS BT_UUID_DECLARE_128(BT_UUID_BAS_VAL)
#define BT_UUID_BAS_BATTERY_LEVEL BT_UUID_DECLARE_128(BT_UUID_BAS_BATTERY_LEVEL_VAL)

/** @brief Callback type for when an LED state change is received. */
typedef void (*led_cb_t)(const bool led_state);

/** @brief Callback type for when the button state is pulled. */
typedef bool (*button_cb_t)(void);

/** @brief Callback struct used by the LBS Service. */
struct my_lbs_cb {
	/** LED state change callback. */
	led_cb_t led_cb;
	/** Button read callback. */
	button_cb_t button_cb;
};

struct my_bas_cb {
	button_cb_t button_cb;
};

/** @brief Initialize the LBS Service.
 *
 * This function registers application callback functions with the My LBS
 * Service
 *
 * @param[in] callbacks Struct containing pointers to callback functions
 *			used by the service. This pointer can be NULL
 *			if no callback functions are defined.
 *
 *
 * @retval 0 If the operation was successful.
 *           Otherwise, a (negative) error code is returned.
 */
int my_lbs_init(struct my_lbs_cb *callbacks);
int my_bas_init(struct my_bas_cb *callbacks);

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* BT_LBS_H_ */
