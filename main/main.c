#include <hal.h>
#include <esp_log.h>
#include <stddef.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "draw.h"
#include "txt.h"

extern const uint8_t ui_font_start[]   asm("_binary_CourierPrime_Bold_ttf_start");
extern const uint8_t ui_font_end[]     asm("_binary_CourierPrime_Bold_ttf_end");

static const char *TAG = "app";
static hal_fb_t fb;

static const char *touch_type_str(hal_touch_type_t type)
{
    switch (type) {
        case HAL_TOUCH_DOWN: return "DOWN";
        case HAL_TOUCH_MOVE: return "MOVE";
        case HAL_TOUCH_UP:   return "UP";
        default:             return "???";
    }
}

static void on_touch(const hal_touch_event_t *event, void *user_data)
{
    (void)user_data;
    ESP_LOGI(TAG, "touch id=%u type=%-4s x=%4u y=%4u size=%u",
             event->id,
             touch_type_str(event->type),
             event->x,
             event->y,
             event->size);

	if (event->type == HAL_TOUCH_DOWN) {
		draw_line(
			fb,
			event->x, event->y - event->size,
			event->x, event->y + event->size,
			0x00
		);
		draw_line(
			fb,
			event->x - event->size, event->y,
			event->x + event->size, event->y,
			0x00
		);
		hal_display_blit(HAL_BLIT_FAST);
	}
}

static void render_task(void* arg)
{
	txt_renderer_t* txt_renderer = txt_renderer_create();
	txt_font_t* font = txt_font_load(txt_renderer, ui_font_start, ui_font_end - ui_font_start, 30);
	txt_draw(txt_renderer, &(txt_draw_t){
		.output = fb,
		.x = fb.width / 2,
		.y = fb.height / 2,
		.style = TXT_STYLE_DEFAULT(font),
		.max_width  = 200,
		.max_height = TXT_SIZE_UNLIMITED,

		.text = "Hello world wrapped",
	});

    draw_line(fb, 0, 0, fb.width - 1, fb.height - 1, 0x00);

    hal_display_blit(HAL_BLIT_FULL);

	hal_input_set_callback(on_touch, NULL);

	vTaskDelete(NULL);
}

void app_main(void)
{
    hal_init();

	hal_display_clear();

    fb = hal_display_get_fb();
	ESP_LOGI(TAG, "Device resolution = %d x %d", fb.width, fb.height);

	TaskHandle_t handle;
    xTaskCreate(
        render_task,
        "render",
        32 * 1024,      // 32KB stack in internal RAM
        NULL,
        5,
        &handle
    );
}
