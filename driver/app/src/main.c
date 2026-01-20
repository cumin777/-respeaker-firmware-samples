#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>
#include <lvgl.h>
#include <errno.h>
#include <stdio.h>

/* Keep FPS stress test quiet (results are shown on-screen). */
LOG_MODULE_REGISTER(l7_e1_lvgl, LOG_LEVEL_ERR);

static lv_obj_t *fps_label;
static lv_obj_t *hint_label;
static lv_obj_t *box;

static uint32_t count;
static int16_t box_x;
static int8_t box_dx = 2;

static uint32_t refr_cnt;
static uint32_t render_cnt;
static uint32_t flush_wait_cnt;
static uint32_t window_start_ms;

static uint32_t fps_refr;
static uint32_t fps_render;
static uint32_t fps_flush_wait;

#define OLED_W 88
#define OLED_H 48
#define BOX_W  10
#define BOX_H  10
#define ANIM_PERIOD_MS  5
#define STATS_PERIOD_MS 1000

#define ACTIVE_MS  10000
#define SLEEP_MS   20000

static void fps_update_ui(void)
{
	char buf[20];

	/* FPS (frame/s): count LVGL refresh cycles completed. */
	snprintf(buf, sizeof(buf), "%u", (unsigned)fps_refr);
	lv_label_set_text(fps_label, buf);
}

static void stats_timer_cb(lv_timer_t *t)
{
	LV_UNUSED(t);

	uint32_t now = k_uptime_get_32();
	uint32_t elapsed = now - window_start_ms;
	if (window_start_ms == 0U || elapsed == 0U) {
		window_start_ms = now;
		refr_cnt = 0U;
		render_cnt = 0U;
		flush_wait_cnt = 0U;
		return;
	}

	/* Use a 1s-ish window; tolerate drift. */
	if (elapsed >= 900U) {
		fps_refr = (refr_cnt * 1000U) / elapsed;
		fps_render = (render_cnt * 1000U) / elapsed;
		fps_flush_wait = (flush_wait_cnt * 1000U) / elapsed;

		refr_cnt = 0U;
		render_cnt = 0U;
		flush_wait_cnt = 0U;
		window_start_ms = now;
	}

	/* Update once per second to avoid the FPS label becoming the bottleneck. */
	fps_update_ui();

	if (hint_label) {
		char hint_buf[32];
		/* On-screen breakdown: render fps + flush-complete fps (helps explain gaps). */
		snprintf(hint_buf, sizeof(hint_buf), "rn%u fl%u",
			(unsigned)fps_render,
			(unsigned)fps_flush_wait);
		lv_label_set_text(hint_label, hint_buf);
	}
}

static void display_event_cb(lv_event_t *e)
{
	lv_event_code_t code = lv_event_get_code(e);

	/* NOTE:
	 * - LV_EVENT_REFR_READY: closest to a "frame" in LVGL terms
	 * - LV_EVENT_RENDER_READY: render finished (before flush)
	 * - LV_EVENT_FLUSH_WAIT_FINISH: flush finished including wait callback
	 */
	switch (code) {
	case LV_EVENT_REFR_READY:
		refr_cnt++;
		break;
	case LV_EVENT_RENDER_READY:
		render_cnt++;
		break;
	case LV_EVENT_FLUSH_WAIT_FINISH:
		flush_wait_cnt++;
		break;
	default:
		break;
	}
}

static void anim_timer_cb(lv_timer_t *t)
{
	LV_UNUSED(t);

	box_x = (int16_t)(box_x + box_dx);
	int16_t max_x = (int16_t)(OLED_W - BOX_W);

	if (box_x <= 0) {
		box_x = 0;
		box_dx = 2;
	} else if (box_x >= max_x) {
		box_x = max_x;
		box_dx = -2;
	}

	lv_obj_set_x(box, box_x);
}

static void create_ui(void)
{
	/* Top: FPS value (large, centered). */
	lv_obj_t *fps_prefix = lv_label_create(lv_screen_active());
	lv_label_set_text(fps_prefix, "FPS");
	lv_obj_align(fps_prefix, LV_ALIGN_TOP_LEFT, 0, 0);

	fps_label = lv_label_create(lv_screen_active());
	lv_label_set_text(fps_label, "0");
	lv_obj_set_style_text_font(fps_label, &lv_font_montserrat_14, 0);
	lv_obj_align(fps_label, LV_ALIGN_TOP_MID, 0, 0);

	/* Middle: short mode hint. */
	hint_label = lv_label_create(lv_screen_active());
	lv_label_set_text(hint_label, "r0 f0");
	lv_obj_align(hint_label, LV_ALIGN_CENTER, 0, 0);

	/* A moving solid box to continuously invalidate areas (stress flush). */
	box = lv_obj_create(lv_screen_active());
	lv_obj_set_size(box, BOX_W, BOX_H);
	lv_obj_set_style_radius(box, 0, 0);
	lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(box, 0, 0);
	lv_obj_set_pos(box, 0, 18);
	box_x = 0;

	/* Timers drive the stress animation and the on-screen stats. */
	(void)lv_timer_create(anim_timer_cb, ANIM_PERIOD_MS, NULL);
	(void)lv_timer_create(stats_timer_cb, STATS_PERIOD_MS, NULL);
}

int main(void)
{
	const struct device *display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	uint32_t phase_start_ms;
	bool active = true;

	if (!device_is_ready(display_dev)) {
		LOG_ERR("Display device not ready");
		return 0;
	}

	LOG_INF("Starting LVGL test on CH1115 (88x48)");

	/* Attach display-level events for FPS measurement. */
	lv_display_t *disp = lv_display_get_default();
	if (disp) {
		lv_display_add_event_cb(disp, display_event_cb, LV_EVENT_REFR_READY, NULL);
		lv_display_add_event_cb(disp, display_event_cb, LV_EVENT_RENDER_READY, NULL);
		lv_display_add_event_cb(disp, display_event_cb, LV_EVENT_FLUSH_WAIT_FINISH, NULL);
		window_start_ms = k_uptime_get_32();
	}

	create_ui();

	(void)display_blanking_off(display_dev);
	(void)display_set_contrast(display_dev, 0xFF);
	/* Force one full clear + one lvgl handler to trigger a flush early */
	(void)display_clear(display_dev);

	lv_timer_handler();
	phase_start_ms = k_uptime_get_32();

	while (1) {
		uint32_t now = k_uptime_get_32();

		if (active) {
			/* Run UI + animation */
			lv_timer_handler();
			count++;

			if ((now - phase_start_ms) >= ACTIVE_MS) {
				/* Enter display sleep */
				int pm_ret = pm_device_action_run(display_dev, PM_DEVICE_ACTION_SUSPEND);
				if (pm_ret == -ENOSYS || pm_ret == -ENOTSUP) {
					(void)display_blanking_on(display_dev);
				}
				active = false;
				phase_start_ms = now;
			}

			k_sleep(K_MSEC(1));
		} else {
			/* During sleep, don't call lv_timer_handler() to avoid any flush attempts. */
			if ((now - phase_start_ms) >= SLEEP_MS) {
				int pm_ret = pm_device_action_run(display_dev, PM_DEVICE_ACTION_RESUME);
				if (pm_ret == -ENOSYS || pm_ret == -ENOTSUP) {
					(void)display_blanking_off(display_dev);
				}
				/* Ensure panel is on and redraw after resume */
				(void)display_blanking_off(display_dev);
				(void)display_set_contrast(display_dev, 0xFF);
				(void)display_clear(display_dev);
				lv_timer_handler();

				active = true;
				phase_start_ms = now;
			}

			/* Let the system idle for low power */
			k_sleep(K_MSEC(50));
		}
	}
}