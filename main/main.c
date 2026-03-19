#include <hal.h>
#include <esp_log.h>
#include <stddef.h>
#include "draw.h"

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

void app_main(void)
{
    hal_init();
	hal_input_set_callback(on_touch, NULL);

	hal_display_clear();

    fb = hal_display_get_fb();
	ESP_LOGI(TAG, "Device resolution = %d x %d", fb.width, fb.height);

    draw_line(fb, 0, 0, fb.width - 1, fb.height - 1, 0x00);

    hal_display_blit(HAL_BLIT_FAST);
}
