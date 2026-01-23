/*
 * Simple UI Demo - 不使用LVGL的替代方案
 * 88x48 OLED 直接Framebuffer绘制
 *
 * RAM占用：约2.6KB（相比LVGL方案的36KB节省92%）
 *
 * 功能：
 * 1. 实现5个场景切换（INFO, STANDBY_MUTE, START_RECORDING, RECORDING_MUTE, TIMESTAMP）
 * 2. 简单的图标绘制
 * 3. 音量条动画
 * 4. 时间戳显示
 */

#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include "qr_32x32.h"

LOG_MODULE_REGISTER(simple_ui, LOG_LEVEL_INF);

#define OLED_WIDTH  88
#define OLED_HEIGHT 48
#define OLED_BUF_SIZE (OLED_WIDTH * OLED_HEIGHT / 8)

/*
 * 如果你的屏幕显示是“左右镜像/上下镜像”，可以在这里用软件方式修正。
 * 默认开启X方向镜像（常见于SSD1306/SH1106的段映射方向与期望相反）。
 * 如果你的屏幕本来就正常，把 UI_MIRROR_X 改成 0。
 */
#define UI_MIRROR_X 1
#define UI_MIRROR_Y 0

/* 场景枚举（按需求仅保留两页：INFO 与 RECORD） */
enum ui_scene {
    SCENE_INFO,
    SCENE_START_RECORDING,
    SCENE_QR,
};

/* 简单UI状态（不包含LVGL对象指针，节省大量RAM） */
struct simple_ui {
    enum ui_scene scene;
    bool muted;
    bool recording;
    char time_str[16];
    uint8_t volume;  /* 1-100 */
};

static const struct device *display;
static struct simple_ui g_ui = {
    .scene = SCENE_INFO,
    .muted = false,
    .recording = false,
    .volume = 50,
};

/* 帧缓冲区（仅528字节） */
static uint8_t frame_buf[OLED_BUF_SIZE];

/* ========================================
 * 像素操作辅助函数
 * ======================================== */

/**
 * @brief 设置单个像素
 */
static inline void map_xy(int *x, int *y)
{
    if (UI_MIRROR_X) {
        *x = (OLED_WIDTH - 1) - *x;
    }
    if (UI_MIRROR_Y) {
        *y = (OLED_HEIGHT - 1) - *y;
    }
}

static inline void set_pixel(uint8_t *buf, int x, int y)
{
    map_xy(&x, &y);
    if (x < 0 || x >= OLED_WIDTH || y < 0 || y >= OLED_HEIGHT)
        return;
    buf[(y / 8) * OLED_WIDTH + x] |= (1 << (y % 8));
}

/**
 * @brief 清除单个像素
 */
static inline void clear_pixel(uint8_t *buf, int x, int y)
{
    map_xy(&x, &y);
    if (x < 0 || x >= OLED_WIDTH || y < 0 || y >= OLED_HEIGHT)
        return;
    buf[(y / 8) * OLED_WIDTH + x] &= ~(1 << (y % 8));
}

/**
 * @brief 绘制矩形
 */
static void draw_rect(uint8_t *buf, int x, int y, int w, int h, bool fill)
{
    for (int i = x; i < x + w && i < OLED_WIDTH; i++) {
        for (int j = y; j < y + h && j < OLED_HEIGHT; j++) {
            if (fill || i == x || i == x + w - 1 || j == y || j == y + h - 1) {
                set_pixel(buf, i, j);
            }
        }
    }
}

/* ========================================
 * INFO页面图标（极简像素风）
 * ======================================== */

static void draw_battery_full_icon(uint8_t *buf, int x, int y)
{
    /* 14x10 battery outline + cap */
    draw_rect(buf, x, y + 1, 12, 8, false);
    draw_rect(buf, x + 12, y + 3, 2, 4, true);
    /* fill */
    draw_rect(buf, x + 2, y + 3, 8, 4, true);
}

static void draw_wifi_off_icon(uint8_t *buf, int x, int y)
{
    /* simple Wi-Fi arcs + slash */
    /* arcs */
    for (int i = 0; i < 7; i++) {
        set_pixel(buf, x + 6 - i, y + 6 - (i / 2));
        set_pixel(buf, x + 6 + i, y + 6 - (i / 2));
    }
    for (int i = 0; i < 5; i++) {
        set_pixel(buf, x + 6 - i, y + 8 - (i / 2));
        set_pixel(buf, x + 6 + i, y + 8 - (i / 2));
    }
    /* dot */
    set_pixel(buf, x + 6, y + 10);
    /* slash */
    for (int i = 0; i < 12; i++) {
        set_pixel(buf, x + 1 + i, y + 11 - i);
    }
}

static void draw_recording_icon(uint8_t *buf, int x, int y)
{
    /* 10x10 ring + center dot */
    draw_rect(buf, x + 1, y + 1, 10, 10, false);
    draw_rect(buf, x + 4, y + 4, 4, 4, true);
}

static void draw_audio_tx_icon(uint8_t *buf, int x, int y)
{
    /* speaker */
    draw_rect(buf, x + 1, y + 4, 3, 4, true);
    set_pixel(buf, x + 4, y + 3);
    set_pixel(buf, x + 4, y + 8);
    set_pixel(buf, x + 5, y + 2);
    set_pixel(buf, x + 5, y + 9);
    /* arrow */
    for (int i = 0; i < 6; i++) {
        set_pixel(buf, x + 8 + i, y + 6);
    }
    set_pixel(buf, x + 12, y + 5);
    set_pixel(buf, x + 13, y + 4);
    set_pixel(buf, x + 12, y + 7);
    set_pixel(buf, x + 13, y + 8);
}

/* ========================================
 * QR code (scaled to fit OLED height)
 * ======================================== */

/* Quiet zone around the QR, in modules. Smaller saves space; too small hurts scanning.
 * On this tiny OLED, 1 module works well when modules are 2x2 pixels.
 */
#define QR_QUIET_MODULES 1

static inline bool qr_module_is_black(int mx, int my)
{
    if (mx < 0 || my < 0 || mx >= QR_MODULES || my >= QR_MODULES) {
        return false;
    }

    const int stride = QR_STRIDE_BYTES;
    const int byte_index = my * stride + (mx / 8);
    const int bit_index = 7 - (mx % 8);
    return ((qr_module_bits[byte_index] >> bit_index) & 0x1) != 0;
}

static void draw_qr_scaled_black_bg(uint8_t *buf)
{
    /* Black background for power saving (OLED off). */
    /* clear_screen() already did memset(buf, 0), so nothing to do here. */

    const int total_modules = QR_MODULES + 2 * QR_QUIET_MODULES;

    /* Integer scaling for crisp edges.
     * We also keep a 1px margin to avoid touching the bezel.
     */
    const int max_w = OLED_WIDTH - 2;
    const int max_h = OLED_HEIGHT - 2;

    int scale_w = max_w / total_modules;
    int scale_h = max_h / total_modules;
    int scale = (scale_w < scale_h) ? scale_w : scale_h;
    if (scale < 1) {
        scale = 1;
    }

    const int qr_px = total_modules * scale;
    const int x0 = (OLED_WIDTH - qr_px) / 2;
    const int y0 = (OLED_HEIGHT - qr_px) / 2;

    /* White QR window (quiet zone included), everything else stays black.
     * This keeps scan reliability while avoiding a full-screen white background.
     */
    draw_rect(buf, x0, y0, qr_px, qr_px, true);

    const int inner_x0 = x0 + QR_QUIET_MODULES * scale;
    const int inner_y0 = y0 + QR_QUIET_MODULES * scale;

    /* Normal polarity inside the white window: black modules = pixels OFF. */
    for (int my = 0; my < QR_MODULES; my++) {
        for (int mx = 0; mx < QR_MODULES; mx++) {
            if (!qr_module_is_black(mx, my)) {
                continue;
            }

            const int px0 = inner_x0 + mx * scale;
            const int py0 = inner_y0 + my * scale;
            for (int dy = 0; dy < scale; dy++) {
                for (int dx = 0; dx < scale; dx++) {
                    clear_pixel(buf, px0 + dx, py0 + dy);
                }
            }
        }
    }
}

/* ========================================
 * 录音页面：左滚动“点→对称柱”动画
 * ======================================== */

#define REC_PAGE_SWITCH_MS      10000
#define REC_SCROLL_TICK_MS      10

/* Demo: randomize volume every N ticks to make the effect obvious */
#define REC_VOLUME_RAND_TICKS   3

/* Animation tuning knobs */
#define REC_MAX_HALF_HEIGHT     20
#define REC_VOLUME_MIN          1
#define REC_VOLUME_MAX          100

/* Spawned bar base height model: random height + bounded random jitter */
#define REC_BASE_HALF_MIN       1
#define REC_HEIGHT_JITTER       3

/* Dot on the right side: 2x2 = 4 pixels */
#define REC_DOT_W               2
#define REC_DOT_H               2

/* Bar thickness (in columns): derived from current volume, applied to ALL on-screen bars */
#define REC_BAR_THICK_MIN       1
#define REC_BAR_THICK_MAX       3

/* Deterministic spacing between generated columns ("柱间隔").
 * NOTE: by request, volume does NOT affect this spacing.
 */
#define REC_SPAWN_GAP            3

struct rec_column {
    uint8_t target_half; /* 0..REC_MAX_HALF_HEIGHT */
};

static struct rec_column rec_cols[OLED_WIDTH];
static uint32_t rec_prng = 0x1234u;
static uint8_t rec_gap_countdown;

static uint32_t prng_next(void)
{
    /* xorshift32 */
    uint32_t x = rec_prng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rec_prng = x ? x : 0x1234u;
    return rec_prng;
}

static void rec_anim_init_once(void)
{
    static bool inited;
    if (inited) {
        return;
    }
    inited = true;
    for (int i = 0; i < OLED_WIDTH; i++) {
        rec_cols[i].target_half = 0;
    }
    rec_gap_countdown = 0;
}

static void rec_anim_step(void)
{
    /* shift left */
    memmove(&rec_cols[0], &rec_cols[1], sizeof(rec_cols) - sizeof(rec_cols[0]));

    /* By request: spawn gap is constant; volume affects rendering, not generation. */
    if (rec_gap_countdown > 0) {
        rec_cols[OLED_WIDTH - 1].target_half = 0;
        rec_gap_countdown--;
        return;
    }

    uint32_t r = prng_next();
    uint8_t base_half = (uint8_t)(REC_BASE_HALF_MIN + (r % (REC_MAX_HALF_HEIGHT - REC_BASE_HALF_MIN + 1u)));
    int jitter = (int)((r >> 8) % (2u * REC_HEIGHT_JITTER + 1u)) - (int)REC_HEIGHT_JITTER;
    int h = (int)base_half + jitter;
    if (h < 1) {
        h = 1;
    } else if (h > REC_MAX_HALF_HEIGHT) {
        h = REC_MAX_HALF_HEIGHT;
    }

    rec_cols[OLED_WIDTH - 1].target_half = (uint8_t)h;
    rec_gap_countdown = REC_SPAWN_GAP;
}

static void draw_dot_2x2(uint8_t *buf, int x, int y)
{
    for (int dx = 0; dx < REC_DOT_W; dx++) {
        for (int dy = 0; dy < REC_DOT_H; dy++) {
            set_pixel(buf, x - dx, y + dy);
        }
    }
}

static void draw_rec_animation(uint8_t *buf, uint32_t phase)
{
    (void)phase;
    const int x_mid = OLED_WIDTH / 2;
    const int y_mid = OLED_HEIGHT / 2;

    uint8_t vol = g_ui.volume;
    if (vol < REC_VOLUME_MIN) {
        vol = REC_VOLUME_MIN;
    } else if (vol > REC_VOLUME_MAX) {
        vol = REC_VOLUME_MAX;
    }

    int bar_thick = (int)REC_BAR_THICK_MIN;
    if (REC_BAR_THICK_MAX > REC_BAR_THICK_MIN) {
        bar_thick = (int)REC_BAR_THICK_MIN +
                    (int)(((uint32_t)(vol - 1) * (REC_BAR_THICK_MAX - REC_BAR_THICK_MIN)) / (REC_VOLUME_MAX - 1));
    }
    if (bar_thick < REC_BAR_THICK_MIN) {
        bar_thick = REC_BAR_THICK_MIN;
    } else if (bar_thick > REC_BAR_THICK_MAX) {
        bar_thick = REC_BAR_THICK_MAX;
    }

    for (int x = 0; x < OLED_WIDTH; x++) {
        const struct rec_column *c = &rec_cols[x];
        if (c->target_half == 0) {
            continue;
        }

        if (x > x_mid) {
            /* dot mode (right half): fixed 2x2 dot, no vertical jitter */
            int dot_y = y_mid - 1;
            if (dot_y < 0) {
                dot_y = 0;
            }
            draw_dot_2x2(buf, x, dot_y);
        } else {
            /* bar mode (left half): height+width scale with current volume */
            int cur_half = (int)(((uint32_t)c->target_half * (uint32_t)vol + (REC_VOLUME_MAX - 1u)) / REC_VOLUME_MAX);
            if (cur_half > REC_MAX_HALF_HEIGHT) {
                cur_half = REC_MAX_HALF_HEIGHT;
            }
            if (cur_half < 1) {
                cur_half = 1;
            }

            for (int dx = 0; dx < bar_thick; dx++) {
                int xb = x - dx;
                if (xb < 0) {
                    continue;
                }
                for (int dy = 0; dy <= cur_half; dy++) {
                    set_pixel(buf, xb, y_mid - dy);
                    set_pixel(buf, xb, y_mid + dy);
                }
            }
        }
    }
}

/* ========================================
 * 字符绘制（简化5x7字体）
 * ======================================== */

/**
 * @brief 简单的5x7字体数据
 * 仅支持数字、冒号和部分字母
 */
static const struct {
    char c;
    uint8_t data[5];
} simple_font[] = {
    {'0', {0x3E, 0x51, 0x49, 0x45, 0x3E}},
    {'1', {0x00, 0x42, 0x7F, 0x40, 0x00}},
    {'2', {0x42, 0x61, 0x51, 0x49, 0x46}},
    {'3', {0x21, 0x41, 0x45, 0x4B, 0x31}},
    {'4', {0x18, 0x14, 0x12, 0x7F, 0x10}},
    {'5', {0x27, 0x45, 0x45, 0x45, 0x39}},
    {'6', {0x3C, 0x4A, 0x49, 0x49, 0x30}},
    {'7', {0x01, 0x71, 0x09, 0x05, 0x03}},
    {'8', {0x36, 0x49, 0x49, 0x49, 0x36}},
    {'9', {0x06, 0x49, 0x49, 0x29, 0x1E}},
    {':', {0x00, 0x36, 0x00, 0x36, 0x00}},
    {'A', {0x7E, 0x09, 0x09, 0x09, 0x7E}},
    {'D', {0x3E, 0x41, 0x41, 0x41, 0x3E}},
    {'E', {0x7F, 0x49, 0x49, 0x49, 0x41}},
    {'I', {0x41, 0x41, 0x7F, 0x41, 0x41}},
    {'K', {0x41, 0x49, 0x49, 0x31, 0x01}},
    {'M', {0x7F, 0x01, 0x01, 0x01, 0x7F}},
    {'N', {0x7F, 0x04, 0x08, 0x10, 0x7F}},
    {'O', {0x3E, 0x41, 0x41, 0x41, 0x3E}},
    {'R', {0x7F, 0x09, 0x19, 0x29, 0x46}},
    {'T', {0x01, 0x01, 0x7F, 0x01, 0x01}},
    {'U', {0x3E, 0x40, 0x40, 0x40, 0x3E}},
    {'W', {0x3F, 0x40, 0x38, 0x40, 0x3F}},
    {' ', {0x00, 0x00, 0x00, 0x00, 0x00}},
};

/**
 * @brief 绘制单个字符
 */
static void draw_char(uint8_t *buf, char c, int x, int y)
{
    for (size_t i = 0; i < ARRAY_SIZE(simple_font); i++) {
        if (simple_font[i].c == c) {
            for (int col = 0; col < 5; col++) {
                for (int row = 0; row < 7; row++) {
                    if (simple_font[i].data[col] & (1 << (6 - row))) {
                        set_pixel(buf, x + col, y + row);
                    }
                }
            }
            return;
        }
    }
}

/**
 * @brief 绘制字符串
 */
static void draw_string(uint8_t *buf, const char *str, int x, int y)
{
    int pos = x;
    while (*str && pos < OLED_WIDTH - 5) {
        draw_char(buf, *str, pos, y);
        pos += 6;  /* 字符宽度(5) + 间距(1) */
        str++;
    }
}

/* ========================================
 * 场景渲染
 * ======================================== */

/**
 * @brief 清屏
 */
static void clear_screen(uint8_t *buf)
{
    memset(buf, 0, OLED_BUF_SIZE);
}

/**
 * @brief 渲染当前场景到帧缓冲区
 */
static void render_scene(uint8_t *buf)
{
    clear_screen(buf);

    switch (g_ui.scene) {
        case SCENE_INFO:
            /* INFO场景：四个状态图标居中显示 */
        {
            const int icon_w = 14;
            const int gap = 6;
            const int total_w = 4 * icon_w + 3 * gap;
            const int x0 = (OLED_WIDTH - total_w) / 2;
            const int y0 = 16;

            draw_string(buf, "INFO", 34, 4);

            draw_battery_full_icon(buf, x0 + 0 * (icon_w + gap), y0);
            draw_wifi_off_icon(buf,     x0 + 1 * (icon_w + gap), y0);
            draw_recording_icon(buf,    x0 + 2 * (icon_w + gap), y0);
            draw_audio_tx_icon(buf,     x0 + 3 * (icon_w + gap), y0);
        }
            break;

        case SCENE_START_RECORDING:
            /* RECORD场景：标题 + 左滚动对称柱动画 */
            rec_anim_init_once();
            draw_string(buf, "REC", 36, 4);
            draw_rec_animation(buf, 0);
            break;

        case SCENE_QR:
        {
            /* QR场景：尽可能放大（受48px高度限制），黑底 + 局部白底(quiet zone) */
            draw_qr_scaled_black_bg(buf);
        }
            break;
    }
}

/* ========================================
 * 公开API函数
 * ======================================== */

/**
 * @brief 切换UI场景
 * @param s 目标场景
 */
void ui_set_scene(enum ui_scene s)
{
    g_ui.scene = s;

    render_scene(frame_buf);

    struct display_buffer_descriptor desc = {
        .buf_size = OLED_BUF_SIZE,
        .width = OLED_WIDTH,
        .height = OLED_HEIGHT,
        .pitch = OLED_WIDTH,
    };

    display_write(display, 0, 0, &desc, frame_buf);
    LOG_DBG("Scene set to: %d", s);
}

/**
 * @brief 更新音量（影响录音动画的高度/密度/柱宽）
 * @param level 音量等级 (1-100)
 */
void ui_update_volume(int level)
{
    if (level < REC_VOLUME_MIN) level = REC_VOLUME_MIN;
    if (level > REC_VOLUME_MAX) level = REC_VOLUME_MAX;
    g_ui.volume = (uint8_t)level;

    if (g_ui.scene == SCENE_START_RECORDING) {
        ui_set_scene(g_ui.scene);
    }
}

/**
 * @brief 更新时间戳
 * @param time_str 时间字符串（格式："HH:MM:SS"）
 */
void ui_update_timestamp(const char *time_str)
{
    if (!time_str) return;

    strncpy(g_ui.time_str, time_str, sizeof(g_ui.time_str) - 1);
    g_ui.time_str[sizeof(g_ui.time_str) - 1] = '\0';
}

/**
 * @brief 设置静音状态
 * @param muted 是否静音
 */
void ui_set_muted(bool muted)
{
    g_ui.muted = muted;
    LOG_DBG("Mute state: %d", muted);
}

/**
 * @brief 设置录音状态
 * @param recording 是否录音
 */
void ui_set_recording(bool recording)
{
    g_ui.recording = recording;
    LOG_DBG("Recording state: %d", recording);
}

/* ========================================
 * 主程序
 * ======================================== */

int main(void)
{
    /* 获取显示设备 */
    display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
    if (!device_is_ready(display)) {
        LOG_ERR("Display device not ready");
        return 0;
    }

    LOG_INF("========================================");
    LOG_INF("Simple UI Demo Started (No LVGL)");
    LOG_INF("========================================");
    LOG_INF("Display resolution: %dx%d", OLED_WIDTH, OLED_HEIGHT);
    LOG_INF("Frame buffer size: %u bytes", (unsigned)OLED_BUF_SIZE);

    /* 初始场景 */
    ui_set_scene(SCENE_INFO);

    enum ui_scene scenes[] = {
        //SCENE_INFO,
        SCENE_START_RECORDING,
        SCENE_QR,
    };
    int scene_idx = 0;
    uint32_t last_switch_ms = k_uptime_get_32();
    uint32_t rec_tick = 0;

    while (1) {
        k_sleep(K_MSEC(REC_SCROLL_TICK_MS));

        /* 每10秒切换 INFO <-> RECORD */
        uint32_t now = k_uptime_get_32();
        if ((now - last_switch_ms) >= REC_PAGE_SWITCH_MS) {
            last_switch_ms = now;
            scene_idx = (scene_idx + 1) % ARRAY_SIZE(scenes);
            ui_set_scene(scenes[scene_idx]);
            LOG_INF("Scene switched to: %d", scenes[scene_idx]);
        }

        /* RECORD 页面持续刷新动画 */
        if (g_ui.scene == SCENE_START_RECORDING) {
            rec_tick++;
            if ((rec_tick % REC_VOLUME_RAND_TICKS) == 0u) {
                /* 1..100 random volume */
                g_ui.volume = (uint8_t)(REC_VOLUME_MIN + (prng_next() % (REC_VOLUME_MAX - REC_VOLUME_MIN + 1)));
            }
            rec_anim_step();
            ui_set_scene(SCENE_START_RECORDING);
        }
    }

    return 0;
}
