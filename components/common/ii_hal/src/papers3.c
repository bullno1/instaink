/*
 * HAL implementation — M5Stack PaperS3
 *
 * Framebuffer : Y8, 1 byte/pixel, 0=black 255=white, row-major
 * Display     : 540x960 portrait (ED047TC2 driven by epdiy)
 * Input       : GT911 via touch_processor (debounced, track-ID stable)
 */

#include <hal.h>

#include <papers3.h>
#include <papers3/bmi270.h>
#include <papers3/gt911.h>
#include <papers3/epd.h>
#include <papers3/touch_processor.h>

#include <epdiy.h>

#include "esp_heap_caps.h"
#include "esp_log.h"

#include <string.h>

static const char *TAG = "hal";

#define HAL_WIDTH  540
#define HAL_HEIGHT 960

/* ── Internal state ──────────────────────────────────────────────── */

static bool                s_init        = false;
static uint8_t            *s_y8_fb       = NULL;
static EpdiyHighlevelState s_epd_hl;
static touch_processor_t   s_touch_proc;
static hal_touch_cb_t      s_touch_cb    = NULL;
static void               *s_touch_ud    = NULL;

/* ── Y8 → epdiy 4bpp conversion ──────────────────────────────────── */
/*
 * epdiy framebuffer layout: 2 pixels per byte, 4 bits each
 *   high nibble = even column, low nibble = odd column
 *   nibble 0x0 = black, 0xF = white
 *
 * Y8 → nibble: value >> 4
 */
static void y8_to_epdiy(const uint8_t *src, uint8_t *dst)
{
    for (int py = 0; py < HAL_WIDTH; py++) {
        for (int px = 0; px < HAL_HEIGHT; px += 2) {
            uint8_t hi = src[(px + 1) * HAL_WIDTH + (HAL_WIDTH - 1 - py)] >> 4;
            uint8_t lo = src[px       * HAL_WIDTH + (HAL_WIDTH - 1 - py)] >> 4;

            dst[py * (HAL_HEIGHT / 2) + px / 2] = (hi << 4) | lo;
        }
    }
}

/* ── Touch bridge ────────────────────────────────────────────────── */

static void on_touch_event(const touch_event_t *event, void *user_data)
{
    if (s_touch_cb == NULL) return;

    hal_touch_event_t e = {
        .id   = event->id,
        .x    = event->x,
        .y    = event->y,
        .size = event->size > 0 ? event->size : 1,
    };

    switch (event->type) {
        case TOUCH_DOWN: e.type = HAL_TOUCH_DOWN; break;
        case TOUCH_MOVE: e.type = HAL_TOUCH_MOVE; break;
        case TOUCH_UP:   e.type = HAL_TOUCH_UP;   break;
        default:         return;
    }

    s_touch_cb(&e, s_touch_ud);
}

static void on_raw_touch(const gt911_state_t *touch, void *user_data)
{
    touch_processor_update(&s_touch_proc, touch);
}

/* ── HAL API ─────────────────────────────────────────────────────── */

void hal_init(void)
{
    if (s_init) return;

    /* Shared hardware — I2C bus, power rails */
    ESP_ERROR_CHECK(papers3_init());
    ESP_ERROR_CHECK(bmi270_init());

    /* Touch */
    touch_processor_init(&s_touch_proc, on_touch_event, NULL);
    ESP_ERROR_CHECK(gt911_init(on_raw_touch, NULL));

    /* Y8 framebuffer — PSRAM, starts white */
    s_y8_fb = heap_caps_malloc(HAL_WIDTH * HAL_HEIGHT, MALLOC_CAP_SPIRAM);
    if (s_y8_fb == NULL) {
        ESP_LOGE(TAG, "Y8 framebuffer allocation failed");
        return;
    }
    memset(s_y8_fb, 0xFF, HAL_WIDTH * HAL_HEIGHT);

    /* EPD */
    epd_init(&epd_board_papers3, &ED047TC2, EPD_LUT_64K);

    s_epd_hl = epd_hl_init(EPD_BUILTIN_WAVEFORM);
    if (epd_hl_get_framebuffer(&s_epd_hl) == NULL) {
        ESP_LOGE(TAG, "epdiy framebuffer allocation failed");
        return;
    }
    epd_hl_set_all_white(&s_epd_hl);

    s_init = true;
    ESP_LOGI(TAG, "init ok (%dx%d)", HAL_WIDTH, HAL_HEIGHT);
}

void hal_deinit(void)
{
    if (!s_init) return;

    epd_poweroff();
	epd_deinit();

    heap_caps_free(s_y8_fb);
    s_y8_fb = NULL;

    papers3_deinit();

    s_init = false;
}

hal_fb_t hal_display_get_fb(void)
{
    if (!s_init || s_y8_fb == NULL) {
        return (hal_fb_t){ .pixels = NULL, .width = 0, .height = 0 };
    }

    return (hal_fb_t){
        .pixels = s_y8_fb,
        .width  = HAL_WIDTH,
        .height = HAL_HEIGHT,
    };
}

void hal_display_blit(hal_blit_mode_t mode)
{
    if (!s_init) return;

    /* Convert Y8 → epdiy 4bpp */
    y8_to_epdiy(s_y8_fb, epd_hl_get_framebuffer(&s_epd_hl));

    /* Temperature for waveform timing */
    float temp = 25.0f;
    bmi270_get_temperature(&temp);

    enum EpdDrawMode draw_mode = (mode == HAL_BLIT_FULL) ? MODE_GC16 : MODE_GL16;

    epd_poweron();
    enum EpdDrawError err = epd_hl_update_screen(&s_epd_hl, draw_mode, (int)temp);
    epd_poweroff();

    if (err != EPD_DRAW_SUCCESS) {
        ESP_LOGE(TAG, "blit failed: %d", (int)err);
    }
}

void hal_display_clear(void)
{
    if (!s_init) return;

    epd_poweron();
    epd_clear();
    epd_poweroff();

    /* Keep Y8 framebuffer in sync */
    memset(s_y8_fb, 0xFF, HAL_WIDTH * HAL_HEIGHT);
}

void hal_input_set_callback(hal_touch_cb_t cb, void *user_data)
{
    s_touch_cb = cb;
    s_touch_ud = user_data;
}
