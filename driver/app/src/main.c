#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>
#include <lvgl.h>
#include <errno.h>
#include <stdio.h>

#include "button.h"

/* Keep FPS stress test quiet (results are shown on-screen). */
LOG_MODULE_REGISTER(l7_e1_lvgl, LOG_LEVEL_ERR);

/*
 * NOTE:
 * This file originally implemented an FPS stress test.
 * Per request: keep the FPS test functions, but do not call them from main.
 */

#define OLED_W 88
#define OLED_H 48

/* Recording bars: fill the full 88px width. */
/* New effect: thin columns (2px) with constant spacing, pixel-scroll left.
 * Right side spawns as dots; after crossing the centerline, dots expand
 * into symmetric bars with per-column random height + brightness.
 */
#define REC_BAR_W 1
#define REC_BAR_W_MAX 3
#define REC_BAR_GAP 4
#define REC_BAR_PITCH (REC_BAR_W + REC_BAR_GAP)
#define REC_BAR_COUNT (((OLED_W + REC_BAR_PITCH - 1) / REC_BAR_PITCH) + 4)
#define REC_DOT_H 2
#define REC_MAX_HALF_H 18
#define REC_MIN_HALF_H 2
#define REC_GROW_RANGE_PX 14

/* Simulated recording volume range (0..100). */
#define REC_VOL_MIN 0
#define REC_VOL_MAX 100

/* INFO icons: 4 tiles in one centered row. */
#define INFO_ICON_SIZE 20
#define INFO_ICON_GAP 2
#define INFO_ROW_X0 1
#define INFO_ROW_Y ((OLED_H - INFO_ICON_SIZE) / 2)

/* 1bpp OLEDs often make 1px strokes look broken. */
#define UI_STROKE 2
/* Per-icon stroke used inside INFO tiles. */
#define INFO_STROKE 2
#if defined(CONFIG_L7_E1_LVGL_FPS_TEST)
#define BOX_W  10
#define BOX_H  10
#define ANIM_PERIOD_MS  5
#define STATS_PERIOD_MS 1000

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
#endif /* CONFIG_L7_E1_LVGL_FPS_TEST */

/* ----------------------- Recording pen UI (LVGL demo) ----------------------- */

enum ui_scene {
	UI_SCENE_BLACK = 0,
	UI_SCENE_INFO,
	UI_SCENE_STANDBY_MUTE,
	UI_SCENE_START_RECORDING,
	UI_SCENE_RECORDING_MUTE,
	UI_SCENE_TIMESTAMP,
};

struct ui_ctx {
	/* Root containers */
	lv_obj_t *root;
	lv_obj_t *info_cont;
	lv_obj_t *mute_cont;
	lv_obj_t *rec_cont;

	/* INFO page (icon-only) */
	lv_obj_t *bat_outline;
	lv_obj_t *bat_cap;
	lv_obj_t *bat_fill;
	lv_obj_t *bat_charge_a;
	lv_obj_t *bat_charge_b;

	lv_obj_t *wire_circle;
	lv_obj_t *wire_state_disconnected;
	lv_obj_t *wire_state_connected;
	lv_obj_t *wire_state_bt_tx;
	lv_obj_t *wire_state_wifi_tx;

	lv_obj_t *mode_icon;
	lv_obj_t *mode_normal;
	lv_obj_t *mode_enh;

	lv_obj_t *pending_icon;
	lv_obj_t *pending_stack_a;
	lv_obj_t *pending_stack_b;
	lv_obj_t *pending_arrow;

	/* Recording widgets */
	lv_obj_t *bars[REC_BAR_COUNT];
	lv_obj_t *rec_dot;
	lv_obj_t *rec_mute_label;
	uint8_t rec_volume; /* 0..100 simulated mic level */

	struct {
		uint8_t target_half_h;
		uint8_t target_opa;
	} bar_meta[REC_BAR_COUNT];
	int rec_scroll_px;

	/* Timers */
	lv_timer_t *tick_timer;
	lv_timer_t *bars_timer;
	lv_timer_t *timestamp_timer;

	/* State */
	enum ui_scene scene;
	uint32_t scene_start_ms;
	uint32_t battery_pct;
	bool charging;
	bool info_enh_mode;
	bool start_rec_switched;
};

static struct ui_ctx g_ui;

/* Monochrome OLEDs are commonly electrically inverted.
 * Keep the UI consistent by explicitly styling everything.
 * Using white as the foreground keeps content visible across panels.
 */
#define UI_FG lv_color_make(0xFF, 0xFF, 0xFF)
#define UI_BG lv_color_make(0x00, 0x00, 0x00)

static lv_obj_t *ui_rect(lv_obj_t *parent, int x, int y, int w, int h, lv_color_t c)
{
	lv_obj_t *r = lv_obj_create(parent);
	if (!r) {
		return NULL;
	}
	lv_obj_set_size(r, w, h);
	lv_obj_set_pos(r, x, y);
	lv_obj_set_style_radius(r, 0, 0);
	lv_obj_set_style_border_width(r, 0, 0);
	lv_obj_set_style_pad_all(r, 0, 0);
	lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(r, c, 0);
	lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
	return r;
}

static lv_obj_t *ui_tile(lv_obj_t *parent, int x, int y)
{
	/* Opaque black tile to ensure clean redraws on 1bpp / VTILED drivers. */
	lv_obj_t *t = lv_obj_create(parent);
	if (!t) {
		return NULL;
	}
	lv_obj_set_size(t, INFO_ICON_SIZE, INFO_ICON_SIZE);
	lv_obj_set_pos(t, x, y);
	lv_obj_set_style_radius(t, 0, 0);
	lv_obj_set_style_border_width(t, 0, 0);
	lv_obj_set_style_pad_all(t, 0, 0);
	lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(t, UI_BG, 0);
	lv_obj_clear_flag(t, LV_OBJ_FLAG_SCROLLABLE);
	return t;
}

static void ui_page_bg(lv_obj_t *cont)
{
	/* Force each scene container to fully overwrite previous content.
	 * This improves readability on 1bpp OLED drivers.
	 */
	lv_obj_set_style_bg_opa(cont, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(cont, UI_BG, 0);
	lv_obj_set_style_border_width(cont, 0, 0);
	lv_obj_set_style_pad_all(cont, 0, 0);
}

static void ui_label_fg(lv_obj_t *label)
{
	lv_obj_set_style_text_color(label, UI_FG, 0);
}

static void ui_icon_clear_wire_states(struct ui_ctx *ui)
{
	lv_obj_add_flag(ui->wire_state_disconnected, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(ui->wire_state_connected, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(ui->wire_state_bt_tx, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(ui->wire_state_wifi_tx, LV_OBJ_FLAG_HIDDEN);
}

static void ui_icon_set_wire_state(struct ui_ctx *ui, uint32_t state)
{
	ui_icon_clear_wire_states(ui);
	switch (state) {
	case 0:
		lv_obj_clear_flag(ui->wire_state_disconnected, LV_OBJ_FLAG_HIDDEN);
		break;
	case 1:
		lv_obj_clear_flag(ui->wire_state_connected, LV_OBJ_FLAG_HIDDEN);
		break;
	case 2:
		lv_obj_clear_flag(ui->wire_state_bt_tx, LV_OBJ_FLAG_HIDDEN);
		break;
	default:
		lv_obj_clear_flag(ui->wire_state_wifi_tx, LV_OBJ_FLAG_HIDDEN);
		break;
	}
}

static void ui_hide_all(struct ui_ctx *ui)
{
	lv_obj_add_flag(ui->info_cont, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(ui->mute_cont, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(ui->rec_cont, LV_OBJ_FLAG_HIDDEN);
}

static void ui_stop_timers(struct ui_ctx *ui);

static void ui_anim_black(struct ui_ctx *ui)
{
	ui_hide_all(ui);
	ui_stop_timers(ui);
	/* Ensure the screen stays black. */
	lv_obj_invalidate(lv_screen_active());
}

static void ui_stop_timers(struct ui_ctx *ui)
{
	if (ui->bars_timer) {
		lv_timer_del(ui->bars_timer);
		ui->bars_timer = NULL;
	}
	if (ui->timestamp_timer) {
		lv_timer_del(ui->timestamp_timer);
		ui->timestamp_timer = NULL;
	}
}

static uint32_t prng_u32(void)
{
	/* Tiny deterministic PRNG (no extra deps). */
	static uint32_t s = 0x12345678U;
	s ^= s << 13;
	s ^= s >> 17;
	s ^= s << 5;
	return s;
}

static void anim_set_bg_opa(void *obj, int32_t v)
{
	lv_obj_set_style_bg_opa((lv_obj_t *)obj, (lv_opa_t)v, 0);
}

static void rec_gen_bar(struct ui_ctx *ui, int idx)
{
	uint32_t r = prng_u32();
	uint32_t span = (uint32_t)(REC_MAX_HALF_H - REC_MIN_HALF_H + 1);
	ui->bar_meta[idx].target_half_h = (uint8_t)(REC_MIN_HALF_H + (r % span));
	/* NOTE: LV_COLOR_DEPTH_1 has no real brightness; keep for future use. */
	ui->bar_meta[idx].target_opa = (uint8_t)LV_OPA_COVER;
}

static void bars_timer_cb(lv_timer_t *t)
{
	LV_UNUSED(t);
	struct ui_ctx *ui = &g_ui;
	const int center_x = OLED_W / 2;
	const int mid_y = OLED_H / 2;

	/* Simulate incoming mic volume continuously. */
	ui->rec_volume = (uint8_t)(prng_u32() % (REC_VOL_MAX + 1));

	/* Pixel-scroll to the left. Wrap every REC_BAR_PITCH px and generate a new
	 * column at the right edge.
	 */
	ui->rec_scroll_px -= 1;
	if (ui->rec_scroll_px <= -REC_BAR_PITCH) {
		ui->rec_scroll_px += REC_BAR_PITCH;
		for (int i = 0; i < (REC_BAR_COUNT - 1); i++) {
			ui->bar_meta[i] = ui->bar_meta[i + 1];
		}
		rec_gen_bar(ui, REC_BAR_COUNT - 1);
	}

	for (int i = 0; i < REC_BAR_COUNT; i++) {
		lv_obj_t *o = ui->bars[i];
		int x = (i * REC_BAR_PITCH) + ui->rec_scroll_px;

		if ((x < -REC_BAR_W_MAX) || (x >= OLED_W)) {
			lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
			continue;
		}
		lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
		/* Monochrome: always draw fully opaque (avoid dithering/threshold issues). */
		lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);

		/* Width scales with volume on the left side only. Keep dots unchanged. */
		int w = REC_BAR_W;
		if (x < center_x) {
			w = REC_BAR_W + ((REC_BAR_W_MAX - REC_BAR_W) * (int)ui->rec_volume) / REC_VOL_MAX;
			if (w < REC_BAR_W) {
				w = REC_BAR_W;
			} else if (w > REC_BAR_W_MAX) {
				w = REC_BAR_W_MAX;
			}
		}
		/* Center the column inside the pitch cell so width changes don't cause jitter. */
		int inner_x = x + ((REC_BAR_PITCH - w) / 2);

		/* Dot on the right half; expand into symmetric bar after crossing center. */
		if (x >= center_x) {
			int h = REC_DOT_H;
			int y = mid_y - (h / 2);
			lv_obj_set_size(o, REC_BAR_W, h);
			lv_obj_set_pos(o, x, y);
			continue;
		}

		int dx = center_x - x;
		int k = (dx > REC_GROW_RANGE_PX) ? REC_GROW_RANGE_PX : dx;
		int target_half = (int)ui->bar_meta[i].target_half_h;
		int half = (target_half * k) / REC_GROW_RANGE_PX;
		/* Modulate height by current volume (0..100). */
		half = (half * (int)ui->rec_volume) / REC_VOL_MAX;
		if (half < 1) {
			half = 1;
		}
		int h = half * 2;
		int y = mid_y - half;

		lv_obj_set_size(o, w, h);
		lv_obj_set_pos(o, inner_x, y);
	}
}

static void dot_blink_once(lv_obj_t *dot)
{
	lv_anim_t a;
	lv_anim_init(&a);
	lv_anim_set_var(&a, dot);
	lv_anim_set_time(&a, 250);
	lv_anim_set_playback_time(&a, 250);
	lv_anim_set_repeat_count(&a, 1);
	lv_anim_set_values(&a, LV_OPA_30, LV_OPA_COVER);
	lv_anim_set_exec_cb(&a, anim_set_bg_opa);
	lv_anim_start(&a);
}

static void timestamp_timer_cb(lv_timer_t *t)
{
	LV_UNUSED(t);
	struct ui_ctx *ui = &g_ui;
	/* Timestamp action: blink the dot only (no text). */
	dot_blink_once(ui->rec_dot);
}

static void ui_anim_info_page(struct ui_ctx *ui)
{
	ui_hide_all(ui);
	ui_stop_timers(ui);
	lv_obj_clear_flag(ui->info_cont, LV_OBJ_FLAG_HIDDEN);

	/* Static status indicator page (no animations, no state cycling). */
	ui->battery_pct = 100;
	ui->charging = false;

	/* Battery fill (no percentage text) */
	/* New battery geometry uses a fixed 10px inner width. */
	uint32_t inner_w = 10U;
	uint32_t fill_w = (inner_w * ui->battery_pct) / 100U;
	if (fill_w > inner_w) {
		fill_w = inner_w;
	}
	lv_obj_set_width(ui->bat_fill, (int)fill_w);

	/* Charging indicator hidden on static page */
	lv_obj_add_flag(ui->bat_charge_a, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(ui->bat_charge_b, LV_OBJ_FLAG_HIDDEN);

	/* Wireless: disconnected */
	ui_icon_set_wire_state(ui, 0);

	/* Mode: normal/enhanced (toggled by double-press on INFO page) */
	if (ui->info_enh_mode) {
		lv_obj_add_flag(ui->mode_normal, LV_OBJ_FLAG_HIDDEN);
		lv_obj_clear_flag(ui->mode_enh, LV_OBJ_FLAG_HIDDEN);
	} else {
		lv_obj_clear_flag(ui->mode_normal, LV_OBJ_FLAG_HIDDEN);
		lv_obj_add_flag(ui->mode_enh, LV_OBJ_FLAG_HIDDEN);
	}

	/* Pending audio: none */
	lv_obj_add_flag(ui->pending_icon, LV_OBJ_FLAG_HIDDEN);
}

static void ui_anim_standby_mute(struct ui_ctx *ui)
{
	ui_hide_all(ui);
	ui_stop_timers(ui);
	lv_obj_clear_flag(ui->mute_cont, LV_OBJ_FLAG_HIDDEN);
}

static void ui_anim_start_recording(struct ui_ctx *ui)
{
	ui_hide_all(ui);
	ui_stop_timers(ui);
	lv_obj_clear_flag(ui->rec_cont, LV_OBJ_FLAG_HIDDEN);
	LOG_ERR("UI: START_RECORDING");

	/* Scrolling volume columns for the whole scene. */
	ui->rec_scroll_px = 0;
	ui->rec_volume = 0;
	for (int i = 0; i < REC_BAR_COUNT; i++) {
		rec_gen_bar(ui, i);
	}

	for (int i = 0; i < REC_BAR_COUNT; i++) {
		lv_obj_clear_flag(ui->bars[i], LV_OBJ_FLAG_HIDDEN);
		lv_obj_set_style_bg_color(ui->bars[i], UI_FG, 0);
		lv_obj_set_style_bg_opa(ui->bars[i], LV_OPA_COVER, 0);
	}
	lv_obj_add_flag(ui->rec_dot, LV_OBJ_FLAG_HIDDEN);
	if (ui->rec_mute_label) {
		lv_obj_add_flag(ui->rec_mute_label, LV_OBJ_FLAG_HIDDEN);
	}
	ui->start_rec_switched = false;

	ui->bars_timer = lv_timer_create(bars_timer_cb, 80, ui);
	if (!ui->bars_timer) {
		LOG_ERR("UI: bars_timer OOM");
		/* Fallback: show a visible marker so the user sees something. */
		lv_obj_clear_flag(ui->rec_dot, LV_OBJ_FLAG_HIDDEN);
		lv_obj_set_style_bg_color(ui->rec_dot, UI_FG, 0);
		lv_obj_set_style_bg_opa(ui->rec_dot, LV_OPA_COVER, 0);
		lv_obj_set_pos(ui->rec_dot, (OLED_W / 2) - 4, (OLED_H / 2) - 4);
		lv_obj_invalidate(ui->rec_cont);
		return;
	}

	/* Apply one immediate layout update so the first frame isn't stale. */
	bars_timer_cb(NULL);
	lv_obj_invalidate(ui->rec_cont);
}

static void ui_anim_recording_mute(struct ui_ctx *ui)
{
	ui_hide_all(ui);
	ui_stop_timers(ui);
	lv_obj_clear_flag(ui->rec_cont, LV_OBJ_FLAG_HIDDEN);

	/* Show dot (recording), and gradually "mute" it by lowering opacity. */
	for (int i = 0; i < REC_BAR_COUNT; i++) {
		lv_obj_add_flag(ui->bars[i], LV_OBJ_FLAG_HIDDEN);
	}
	lv_obj_clear_flag(ui->rec_dot, LV_OBJ_FLAG_HIDDEN);

	/* Keep it persistent and visible on mono OLEDs. */
	lv_obj_set_style_bg_color(ui->rec_dot, UI_FG, 0);
	lv_obj_set_style_bg_opa(ui->rec_dot, LV_OPA_COVER, 0);
	if (ui->rec_mute_label) {
		lv_obj_clear_flag(ui->rec_mute_label, LV_OBJ_FLAG_HIDDEN);
	}
}

static void ui_anim_timestamp(struct ui_ctx *ui)
{
	ui_hide_all(ui);
	ui_stop_timers(ui);
	lv_obj_clear_flag(ui->rec_cont, LV_OBJ_FLAG_HIDDEN);

	for (int i = 0; i < REC_BAR_COUNT; i++) {
		lv_obj_add_flag(ui->bars[i], LV_OBJ_FLAG_HIDDEN);
	}
	lv_obj_clear_flag(ui->rec_dot, LV_OBJ_FLAG_HIDDEN);
	lv_obj_set_style_bg_opa(ui->rec_dot, LV_OPA_COVER, 0);
	if (ui->rec_mute_label) {
		lv_obj_add_flag(ui->rec_mute_label, LV_OBJ_FLAG_HIDDEN);
	}

	/* Every second: blink dot only. */
	ui->timestamp_timer = lv_timer_create(timestamp_timer_cb, 1000, ui);
}

static void ui_scene_enter(struct ui_ctx *ui, enum ui_scene scene)
{
	ui->scene = scene;
	ui->scene_start_ms = k_uptime_get_32();

	switch (scene) {
	case UI_SCENE_BLACK:
		ui_anim_black(ui);
		break;
	case UI_SCENE_INFO:
		ui_anim_info_page(ui);
		break;
	case UI_SCENE_STANDBY_MUTE:
		ui_anim_standby_mute(ui);
		break;
	case UI_SCENE_START_RECORDING:
		ui_anim_start_recording(ui);
		break;
	case UI_SCENE_RECORDING_MUTE:
		ui_anim_recording_mute(ui);
		break;
	case UI_SCENE_TIMESTAMP:
		ui_anim_timestamp(ui);
		break;
	default:
		ui_anim_info_page(ui);
		break;
	}
}

static void ui_tick_cb(lv_timer_t *t)
{
	LV_UNUSED(t);
	struct ui_ctx *ui = &g_ui;

	/* Poll button events and drive UI. */
	button_event_t evt;
	while (button_get_event_no_wait(&evt) == 0) {
		switch (evt) {
		case BUTTON_EVENT_SHORT_PRESS:
			/*
			 * BLACK: short press -> INFO
			 * INFO: ignored
			 * RECORDING: ignored
			 */
			if (ui->scene == UI_SCENE_BLACK) {
				ui_scene_enter(ui, UI_SCENE_INFO);
			}
			break;
		case BUTTON_EVENT_DOUBLE_PRESS:
			/*
			 * BLACK: double press -> INFO
			 * INFO: toggle normal/enhanced and refresh UI
			 * RECORDING: ignored
			 */
			if (ui->scene == UI_SCENE_BLACK) {
				ui_scene_enter(ui, UI_SCENE_INFO);
			} else if (ui->scene == UI_SCENE_INFO) {
				ui->info_enh_mode = !ui->info_enh_mode;
				ui_anim_info_page(ui);
				lv_obj_invalidate(ui->info_cont);
			}
			break;
		case BUTTON_EVENT_LONG_PRESS:
			/*
			 * BLACK: long press -> RECORDING
			 * INFO: long press -> RECORDING
			 * RECORDING: long press -> INFO
			 */
			if (ui->scene == UI_SCENE_START_RECORDING) {
				ui_scene_enter(ui, UI_SCENE_INFO);
			} else if (ui->scene == UI_SCENE_BLACK || ui->scene == UI_SCENE_INFO) {
				ui_scene_enter(ui, UI_SCENE_START_RECORDING);
			}
			break;
		default:
			break;
		}
	}
}

static int ui_create_recording_demo(struct ui_ctx *ui)
{
	/* Force the global background to black. Some LVGL themes set the screen
	 * background to white; on a monochrome OLED that becomes a "white screen".
	 */
	lv_obj_t *scr = lv_screen_active();
	lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(scr, UI_BG, 0);
	lv_obj_set_style_border_width(scr, 0, 0);
	lv_obj_set_style_pad_all(scr, 0, 0);

	/* If we fail part-way through, delete the root to free any created children. */
	ui->root = NULL;
	ui->info_cont = NULL;
	ui->mute_cont = NULL;
	ui->rec_cont = NULL;
	ui->rec_mute_label = NULL;
	ui->rec_scroll_px = 0;

	ui->root = lv_obj_create(lv_screen_active());
	if (!ui->root) {
		return -ENOMEM;
	}
	lv_obj_set_size(ui->root, OLED_W, OLED_H);
	lv_obj_set_pos(ui->root, 0, 0);
	lv_obj_set_style_bg_opa(ui->root, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(ui->root, UI_BG, 0);
	lv_obj_set_style_border_width(ui->root, 0, 0);
	lv_obj_set_style_pad_all(ui->root, 0, 0);

	/* INFO container */
	ui->info_cont = lv_obj_create(ui->root);
	if (!ui->info_cont) {
		lv_obj_del(ui->root);
		ui->root = NULL;
		return -ENOMEM;
	}
	lv_obj_set_size(ui->info_cont, OLED_W, OLED_H);
	ui_page_bg(ui->info_cont);

	/* Battery icon (more standard): terminal + body outline + fill + charge overlay */
	const int info_y = INFO_ROW_Y;
	const int x_bat = INFO_ROW_X0;
	const int x_wire = INFO_ROW_X0 + (INFO_ICON_SIZE + INFO_ICON_GAP);
	const int x_mode = INFO_ROW_X0 + 2 * (INFO_ICON_SIZE + INFO_ICON_GAP);
	const int x_pending = INFO_ROW_X0 + 3 * (INFO_ICON_SIZE + INFO_ICON_GAP);

	ui->bat_outline = ui_tile(ui->info_cont, x_bat, info_y);
	if (!ui->bat_outline) {
		lv_obj_del(ui->root);
		ui->root = NULL;
		return -ENOMEM;
	}

	/* Terminal on top */
	ui->bat_cap = ui_rect(ui->bat_outline, 7, 3, 6, 3, UI_FG);
	if (!ui->bat_cap) {
		lv_obj_del(ui->root);
		ui->root = NULL;
		return -ENOMEM;
	}

	/* Body outline: 14x11 at (3,6), stroke=INFO_STROKE => inner width 10px */
	const int bat_x = 3;
	const int bat_y = 6;
	const int bat_w = 14;
	const int bat_h = 11;
	const int s = INFO_STROKE;
	if (!ui_rect(ui->bat_outline, bat_x, bat_y, bat_w, s, UI_FG) ||
	    !ui_rect(ui->bat_outline, bat_x, bat_y, s, bat_h, UI_FG) ||
	    !ui_rect(ui->bat_outline, bat_x, bat_y + bat_h - s, bat_w, s, UI_FG) ||
	    !ui_rect(ui->bat_outline, bat_x + bat_w - s, bat_y, s, bat_h, UI_FG)) {
		lv_obj_del(ui->root);
		ui->root = NULL;
		return -ENOMEM;
	}

	ui->bat_fill = ui_rect(ui->bat_outline, bat_x + s, bat_y + s, 10, bat_h - (2 * s), UI_FG);
	/* Charging marker: chunky lightning cut-out (drawn in BG so it appears inside fill) */
	ui->bat_charge_a = ui_rect(ui->bat_outline, bat_x + 6, bat_y + 2, 3, 3, UI_BG);
	ui->bat_charge_b = ui_rect(ui->bat_outline, bat_x + 5, bat_y + 5, 5, 3, UI_BG);
	if (!ui->bat_fill || !ui->bat_charge_a || !ui->bat_charge_b) {
		lv_obj_del(ui->root);
		ui->root = NULL;
		return -ENOMEM;
	}
	lv_obj_add_flag(ui->bat_charge_a, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(ui->bat_charge_b, LV_OBJ_FLAG_HIDDEN);

	/* Wireless icon: tile + 4 variants (chunky, rectangle-only, more recognizable) */
	ui->wire_circle = ui_tile(ui->info_cont, x_wire, info_y);
	if (!ui->wire_circle) {
		lv_obj_del(ui->root);
		ui->root = NULL;
		return -ENOMEM;
	}

	ui->wire_state_disconnected = lv_obj_create(ui->wire_circle);
	ui->wire_state_connected = lv_obj_create(ui->wire_circle);
	ui->wire_state_bt_tx = lv_obj_create(ui->wire_circle);
	ui->wire_state_wifi_tx = lv_obj_create(ui->wire_circle);
	if (!ui->wire_state_disconnected || !ui->wire_state_connected || !ui->wire_state_bt_tx || !ui->wire_state_wifi_tx) {
		lv_obj_del(ui->root);
		ui->root = NULL;
		return -ENOMEM;
	}
	/* Common init for state containers */
	{
		lv_obj_t *states[] = {
			ui->wire_state_disconnected,
			ui->wire_state_connected,
			ui->wire_state_bt_tx,
			ui->wire_state_wifi_tx,
		};
		for (size_t si = 0; si < ARRAY_SIZE(states); si++) {
			lv_obj_set_size(states[si], INFO_ICON_SIZE, INFO_ICON_SIZE);
			lv_obj_set_pos(states[si], 0, 0);
			lv_obj_set_style_bg_opa(states[si], LV_OPA_TRANSP, 0);
			lv_obj_set_style_border_width(states[si], 0, 0);
			lv_obj_set_style_pad_all(states[si], 0, 0);
			lv_obj_clear_flag(states[si], LV_OBJ_FLAG_SCROLLABLE);
		}
	}

	/* Disconnected: broken link + slash */
	{
		lv_obj_t *l = ui_rect(ui->wire_state_disconnected, 3, 8, 5, 5, UI_FG);
		lv_obj_t *r = ui_rect(ui->wire_state_disconnected, 12, 8, 5, 5, UI_FG);
		lv_obj_t *s1 = ui_rect(ui->wire_state_disconnected, 7, 6, 3, 3, UI_FG);
		lv_obj_t *s2 = ui_rect(ui->wire_state_disconnected, 9, 9, 3, 3, UI_FG);
		lv_obj_t *s3 = ui_rect(ui->wire_state_disconnected, 11, 12, 3, 3, UI_FG);
		if (!l || !r || !s1 || !s2 || !s3) {
			lv_obj_del(ui->root);
			ui->root = NULL;
			return -ENOMEM;
		}
	}

	/* Connected: chain link (two blocks + connector) */
	{
		lv_obj_t *l = ui_rect(ui->wire_state_connected, 3, 8, 5, 5, UI_FG);
		lv_obj_t *r = ui_rect(ui->wire_state_connected, 12, 8, 5, 5, UI_FG);
		lv_obj_t *c = ui_rect(ui->wire_state_connected, 7, 9, 6, 3, UI_FG);
		if (!l || !r || !c) {
			lv_obj_del(ui->root);
			ui->root = NULL;
			return -ENOMEM;
		}
	}

	/* BT TX: link + two "tx" columns */
	{
		lv_obj_t *l = ui_rect(ui->wire_state_bt_tx, 3, 8, 5, 5, UI_FG);
		lv_obj_t *r = ui_rect(ui->wire_state_bt_tx, 12, 8, 5, 5, UI_FG);
		lv_obj_t *c = ui_rect(ui->wire_state_bt_tx, 7, 9, 6, 3, UI_FG);
		lv_obj_t *t1 = ui_rect(ui->wire_state_bt_tx, 15, 3, 2, 5, UI_FG);
		lv_obj_t *t2 = ui_rect(ui->wire_state_bt_tx, 17, 2, 2, 7, UI_FG);
		if (!l || !r || !c || !t1 || !t2) {
			lv_obj_del(ui->root);
			ui->root = NULL;
			return -ENOMEM;
		}
	}

	/* WiFi TX: link + three rising bars */
	{
		lv_obj_t *l = ui_rect(ui->wire_state_wifi_tx, 3, 8, 5, 5, UI_FG);
		lv_obj_t *r = ui_rect(ui->wire_state_wifi_tx, 12, 8, 5, 5, UI_FG);
		lv_obj_t *c = ui_rect(ui->wire_state_wifi_tx, 7, 9, 6, 3, UI_FG);
		lv_obj_t *b1 = ui_rect(ui->wire_state_wifi_tx, 14, 4, 2, 4, UI_FG);
		lv_obj_t *b2 = ui_rect(ui->wire_state_wifi_tx, 16, 3, 2, 6, UI_FG);
		lv_obj_t *b3 = ui_rect(ui->wire_state_wifi_tx, 18, 2, 2, 8, UI_FG);
		if (!l || !r || !c || !b1 || !b2 || !b3) {
			lv_obj_del(ui->root);
			ui->root = NULL;
			return -ENOMEM;
		}
	}

	ui_icon_clear_wire_states(ui);
	lv_obj_clear_flag(ui->wire_state_disconnected, LV_OBJ_FLAG_HIDDEN);

	/* Mode icon: microphone (normal) vs microphone + gain bars (enhanced) */
	ui->mode_icon = ui_tile(ui->info_cont, x_mode, info_y);
	if (!ui->mode_icon) {
		lv_obj_del(ui->root);
		ui->root = NULL;
		return -ENOMEM;
	}

	ui->mode_normal = lv_obj_create(ui->mode_icon);
	if (!ui->mode_normal) {
		lv_obj_del(ui->root);
		ui->root = NULL;
		return -ENOMEM;
	}
	lv_obj_set_size(ui->mode_normal, INFO_ICON_SIZE, INFO_ICON_SIZE);
	lv_obj_set_pos(ui->mode_normal, 0, 0);
	lv_obj_set_style_bg_opa(ui->mode_normal, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_width(ui->mode_normal, 0, 0);
	lv_obj_set_style_pad_all(ui->mode_normal, 0, 0);
	lv_obj_clear_flag(ui->mode_normal, LV_OBJ_FLAG_SCROLLABLE);
	/* Mic: top cap + body + stem + base */
	if (!ui_rect(ui->mode_normal, 6, 3, 8, 2, UI_FG) ||
	    !ui_rect(ui->mode_normal, 7, 5, 6, 9, UI_FG) ||
	    !ui_rect(ui->mode_normal, 9, 14, 2, 3, UI_FG) ||
	    !ui_rect(ui->mode_normal, 6, 18, 8, 2, UI_FG)) {
		lv_obj_del(ui->root);
		ui->root = NULL;
		return -ENOMEM;
	}

	ui->mode_enh = lv_obj_create(ui->mode_icon);
	if (!ui->mode_enh) {
		lv_obj_del(ui->root);
		ui->root = NULL;
		return -ENOMEM;
	}
	lv_obj_set_size(ui->mode_enh, INFO_ICON_SIZE, INFO_ICON_SIZE);
	lv_obj_set_pos(ui->mode_enh, 0, 0);
	lv_obj_set_style_bg_opa(ui->mode_enh, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_width(ui->mode_enh, 0, 0);
	lv_obj_set_style_pad_all(ui->mode_enh, 0, 0);
	lv_obj_clear_flag(ui->mode_enh, LV_OBJ_FLAG_SCROLLABLE);
	/* Same mic + side gain bars */
	if (!ui_rect(ui->mode_enh, 6, 3, 8, 2, UI_FG) ||
	    !ui_rect(ui->mode_enh, 7, 5, 6, 9, UI_FG) ||
	    !ui_rect(ui->mode_enh, 9, 14, 2, 3, UI_FG) ||
	    !ui_rect(ui->mode_enh, 6, 18, 8, 2, UI_FG) ||
	    !ui_rect(ui->mode_enh, 3, 6, 2, 8, UI_FG) ||
	    !ui_rect(ui->mode_enh, 15, 6, 2, 8, UI_FG)) {
		lv_obj_del(ui->root);
		ui->root = NULL;
		return -ENOMEM;
	}
	lv_obj_add_flag(ui->mode_enh, LV_OBJ_FLAG_HIDDEN);

	/* Pending audio icon: trays + upload arrow */
	ui->pending_icon = ui_tile(ui->info_cont, x_pending, info_y);
	if (!ui->pending_icon) {
		lv_obj_del(ui->root);
		ui->root = NULL;
		return -ENOMEM;
	}

	ui->pending_stack_a = lv_obj_create(ui->pending_icon);
	ui->pending_stack_b = lv_obj_create(ui->pending_icon);
	ui->pending_arrow = lv_obj_create(ui->pending_icon);
	if (!ui->pending_stack_a || !ui->pending_stack_b || !ui->pending_arrow) {
		lv_obj_del(ui->root);
		ui->root = NULL;
		return -ENOMEM;
	}

	/* Trays */
	lv_obj_set_size(ui->pending_stack_b, 14, 3);
	lv_obj_set_pos(ui->pending_stack_b, 3, 13);
	lv_obj_set_style_bg_opa(ui->pending_stack_b, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(ui->pending_stack_b, UI_FG, 0);
	lv_obj_set_style_border_width(ui->pending_stack_b, 0, 0);

	lv_obj_set_size(ui->pending_stack_a, 14, 3);
	lv_obj_set_pos(ui->pending_stack_a, 3, 17);
	lv_obj_set_style_bg_opa(ui->pending_stack_a, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(ui->pending_stack_a, UI_FG, 0);
	lv_obj_set_style_border_width(ui->pending_stack_a, 0, 0);

	/* Arrow (centered) */
	lv_obj_set_size(ui->pending_arrow, 3, 9);
	lv_obj_set_pos(ui->pending_arrow, 9, 5);
	lv_obj_set_style_bg_opa(ui->pending_arrow, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(ui->pending_arrow, UI_FG, 0);
	lv_obj_set_style_border_width(ui->pending_arrow, 0, 0);
	{
		lv_obj_t *head = lv_obj_create(ui->pending_icon);
		if (!head) {
			lv_obj_del(ui->root);
			ui->root = NULL;
			return -ENOMEM;
		}
		lv_obj_set_size(head, 9, 3);
		lv_obj_set_pos(head, 6, 3);
		lv_obj_set_style_bg_opa(head, LV_OPA_COVER, 0);
		lv_obj_set_style_bg_color(head, UI_FG, 0);
		lv_obj_set_style_border_width(head, 0, 0);
		lv_obj_clear_flag(head, LV_OBJ_FLAG_SCROLLABLE);
	}

	lv_obj_clear_flag(ui->pending_stack_a, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(ui->pending_stack_b, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(ui->pending_arrow, LV_OBJ_FLAG_SCROLLABLE);

	/* MUTE container */
	ui->mute_cont = lv_obj_create(ui->root);
	if (!ui->mute_cont) {
		lv_obj_del(ui->root);
		ui->root = NULL;
		return -ENOMEM;
	}
	lv_obj_set_size(ui->mute_cont, OLED_W, OLED_H);
	ui_page_bg(ui->mute_cont);

	lv_obj_t *mute_icon = lv_label_create(ui->mute_cont);
	if (!mute_icon) {
		lv_obj_del(ui->root);
		ui->root = NULL;
		return -ENOMEM;
	}
	lv_label_set_text(mute_icon, "MUTE");
	lv_obj_set_width(mute_icon, OLED_W);
	lv_label_set_long_mode(mute_icon, LV_LABEL_LONG_CLIP);
	lv_obj_set_style_text_font(mute_icon, &lv_font_montserrat_14, 0);
	lv_obj_set_style_text_align(mute_icon, LV_TEXT_ALIGN_CENTER, 0);
	ui_label_fg(mute_icon);
	lv_obj_set_height(mute_icon, 16);
	lv_obj_align(mute_icon, LV_ALIGN_TOP_MID, 0, 6);

	lv_obj_t *mute_hint = lv_label_create(ui->mute_cont);
	if (!mute_hint) {
		lv_obj_del(ui->root);
		ui->root = NULL;
		return -ENOMEM;
	}
	lv_label_set_text(mute_hint, "STBY");
	lv_obj_set_width(mute_hint, OLED_W);
	lv_label_set_long_mode(mute_hint, LV_LABEL_LONG_CLIP);
	lv_obj_set_style_text_font(mute_hint, &lv_font_montserrat_12, 0);
	lv_obj_set_style_text_align(mute_hint, LV_TEXT_ALIGN_CENTER, 0);
	ui_label_fg(mute_hint);
	lv_obj_set_height(mute_hint, 14);
	lv_obj_align(mute_hint, LV_ALIGN_TOP_MID, 0, 28);

	/* RECORD container */
	ui->rec_cont = lv_obj_create(ui->root);
	if (!ui->rec_cont) {
		lv_obj_del(ui->root);
		ui->root = NULL;
		return -ENOMEM;
	}
	lv_obj_set_size(ui->rec_cont, OLED_W, OLED_H);
	ui_page_bg(ui->rec_cont);

	/* Volume bars: thin columns that scroll left; start as dots centered on midline */
	const int bar_w = REC_BAR_W;
	const int bar_pitch = REC_BAR_PITCH;
	const int mid_y = OLED_H / 2;
	for (int i = 0; i < REC_BAR_COUNT; i++) {
		ui->bars[i] = lv_obj_create(ui->rec_cont);
		if (!ui->bars[i]) {
			lv_obj_del(ui->root);
			ui->root = NULL;
			return -ENOMEM;
		}
		lv_obj_set_size(ui->bars[i], bar_w, REC_DOT_H);
		lv_obj_set_style_radius(ui->bars[i], 0, 0);
		lv_obj_set_style_border_width(ui->bars[i], 0, 0);
		lv_obj_set_style_bg_opa(ui->bars[i], LV_OPA_COVER, 0);
		lv_obj_set_style_bg_color(ui->bars[i], UI_FG, 0);
		lv_obj_set_pos(ui->bars[i], i * bar_pitch, mid_y - (REC_DOT_H / 2));
	}

	ui->rec_dot = lv_obj_create(ui->rec_cont);
	if (!ui->rec_dot) {
		lv_obj_del(ui->root);
		ui->root = NULL;
		return -ENOMEM;
	}
	lv_obj_set_size(ui->rec_dot, 8, 8);
	lv_obj_set_style_radius(ui->rec_dot, LV_RADIUS_CIRCLE, 0);
	lv_obj_set_style_border_width(ui->rec_dot, 0, 0);
	lv_obj_set_style_bg_opa(ui->rec_dot, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(ui->rec_dot, UI_FG, 0);
	lv_obj_set_pos(ui->rec_dot, (OLED_W / 2) - 4, (OLED_H / 2) - 4);

	ui->rec_mute_label = lv_label_create(ui->rec_cont);
	if (!ui->rec_mute_label) {
		lv_obj_del(ui->root);
		ui->root = NULL;
		return -ENOMEM;
	}
	lv_label_set_text(ui->rec_mute_label, "MUTE");
	lv_obj_set_width(ui->rec_mute_label, OLED_W);
	lv_label_set_long_mode(ui->rec_mute_label, LV_LABEL_LONG_CLIP);
	lv_obj_set_style_text_font(ui->rec_mute_label, &lv_font_montserrat_12, 0);
	lv_obj_set_style_text_align(ui->rec_mute_label, LV_TEXT_ALIGN_CENTER, 0);
	ui_label_fg(ui->rec_mute_label);
	lv_obj_set_height(ui->rec_mute_label, 16);
	lv_obj_align(ui->rec_mute_label, LV_ALIGN_TOP_MID, 0, 2);
	lv_obj_add_flag(ui->rec_mute_label, LV_OBJ_FLAG_HIDDEN);

	ui->battery_pct = 87;
	ui->charging = false;
	ui->info_enh_mode = false;
	ui->start_rec_switched = false;

	ui_hide_all(ui);
	return 0;
}

int main(void)
{
	const struct device *display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

	if (!device_is_ready(display_dev)) {
		LOG_ERR("Display device not ready");
		return 0;
	}

	/* Many 88x48 CH1115 modules are wired in inverted mode (pixel 0/1 swapped).
	 * In this project’s CH1115 driver:
	 * - PIXEL_FORMAT_MONO01 -> 0xA6 (normal display)
	 * - PIXEL_FORMAT_MONO10 -> 0xA7 (reverse display)
	 * Force MONO10 early so a black UI background is physically black.
	 */
	(void)display_set_pixel_format(display_dev, PIXEL_FORMAT_MONO10);

	LOG_INF("Starting LVGL app on CH1115 (88x48)");

	/*
	 * Keep the original FPS stress test code, but do not run it here.
	 * If you want to use it for benchmarking, call create_ui() instead.
	 */
	int ret = ui_create_recording_demo(&g_ui);
	if (ret != 0) {
		LOG_ERR("UI create failed: %d", ret);
		return 0;
	}

	ret = button_init();
	if (ret != 0) {
		LOG_ERR("Button init failed: %d", ret);
		return 0;
	}

	(void)display_blanking_off(display_dev);
	/* Max contrast is noticeably higher power on OLED. */
	(void)display_set_contrast(display_dev, 0x7F);
	/* Force one full clear + one lvgl handler to trigger a flush early */
	(void)display_clear(display_dev);

	lv_timer_handler();

	/* Default behavior: black screen, no animations. */
	ui_scene_enter(&g_ui, UI_SCENE_BLACK);
	lv_obj_invalidate(lv_screen_active());
	lv_timer_handler();
	g_ui.tick_timer = lv_timer_create(ui_tick_cb, 50, &g_ui);

	while (1) {
		/* Drive LVGL timers/animations. */
		lv_timer_handler();
		k_sleep(K_MSEC(5));
	}
}