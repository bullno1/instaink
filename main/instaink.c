/*
 * PaperS3 Triangle Demo
 *
 * Renders a filled triangle to the ED047TC2 960x540 e-ink display
 * using epdiy v2 and the custom M5Stack PaperS3 board definition.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "epdiy.h"
#include <papers3.h>
#include <papers3/bmi270.h>
#include <papers3/epd.h>
#include <papers3/gt911.h>
#include <papers3/touch_processor.h>

static const char *TAG = "triangle_demo";

/* Logical dimensions after 90° rotation */
#define EPD_WIDTH  540
#define EPD_HEIGHT 960

/* Triangle vertices — apex at top centre, base at bottom (△) */
#define APEX_X   (EPD_WIDTH  / 2)   /* 270 — top centre   */
#define APEX_Y   80
#define BASE_LX  60
#define BASE_LY  (EPD_HEIGHT - 80)  /* 880 — bottom left  */
#define BASE_RX  (EPD_WIDTH  - 60)  /* 480 — bottom right */
#define BASE_RY  (EPD_HEIGHT - 80)  /* 880                */

static EpdiyHighlevelState hl;
static touch_processor_t touch_processor;

static void on_raw_touch(const gt911_state_t *state, void *user_data)
{
	touch_processor_update(user_data, state);
}

static void on_touch_event(const touch_event_t *event, void *user_data)
{
    switch (event->type) {
        case TOUCH_DOWN:
            ESP_LOGI(TAG, "down (%d)  x=%d y=%d", event->id, event->x, event->y);
            break;
        case TOUCH_UP:
            ESP_LOGI(TAG, "up (%d)   x=%d y=%d", event->id, event->x, event->y);
            break;
        case TOUCH_MOVE:
            ESP_LOGI(TAG, "move (%d)  x=%d y=%d", event->id, event->x, event->y);
            break;
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "PaperS3 triangle demo starting");

    /* ── 1. Init: board + display + LUT in one call ──────────────── */
	papers3_init();
	bmi270_init();
	touch_processor_init(&touch_processor, on_touch_event, NULL);
	gt911_init(on_raw_touch, &touch_processor);
    epd_init(&epd_board_papers3, &ED047TC2, EPD_LUT_64K);
	epd_set_rotation(EPD_ROT_INVERTED_PORTRAIT);

    /* ── 2. Allocate framebuffer in PSRAM ────────────────────────── */
    hl = epd_hl_init(EPD_BUILTIN_WAVEFORM);

    uint8_t *fb = epd_hl_get_framebuffer(&hl);
    if (fb == NULL) {
        ESP_LOGE(TAG, "Framebuffer allocation failed — is PSRAM enabled?");
        return;
    }

    /* ── 3. Power on and clear the panel ─────────────────────────── */
    epd_poweron();
    epd_clear();
    epd_poweroff();

    /* ── 4. Draw triangle into the framebuffer ───────────────────── */
    epd_hl_set_all_white(&hl);

    epd_fill_triangle(
        APEX_X,  APEX_Y,   /* top centre  */
        BASE_LX, BASE_LY,  /* bottom left */
        BASE_RX, BASE_RY,  /* bottom right */
        0x00,              /* black */
        fb
    );

    /* ── 5. Push framebuffer to the panel ────────────────────────── */
	float temp = 25.f;
	bmi270_get_temperature(&temp);
    ESP_LOGI(TAG, "Temperature = %f", temp);
    epd_poweron();

    enum EpdDrawError err = epd_hl_update_screen(&hl, MODE_GL16, temp);
    if (err != EPD_DRAW_SUCCESS) {
        ESP_LOGE(TAG, "Screen update failed: %d", (int)err);
    } else {
        ESP_LOGI(TAG, "Triangle drawn successfully");
    }

    epd_poweroff();

    ESP_LOGI(TAG, "Done — image retained on display");
}
