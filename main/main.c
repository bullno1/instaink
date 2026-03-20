#include <hal.h>
#include <esp_log.h>
#include <stddef.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <clay.h>
#include "draw.h"
#include "txt.h"
#include "arena.h"

#define FRAME_ARENA_SIZE 2 * 1024

#define make_frame_copy(X) \
	make_frame_copy(&(X), sizeof((X)), _Alignof(typeof((X))))

typedef enum {
	UI_REFRESH,
	UI_TOUCH_EVENT,
} ui_msg_type_t;

typedef struct {
	ui_msg_type_t type;

	union {
		hal_touch_event_t touch;
	};
} ui_msg_t;

typedef void (*custom_element_render_fn_t)(void* ctx, const Clay_RenderCommand* cmd);

typedef struct {
	custom_element_render_fn_t render;
	void* ctx;
} custom_element_t;

typedef enum {
	TEXT_SIZING_WRAP,
	TEXT_SIZING_GROW,
} text_sizing_t;

typedef struct {
	txt_draw_t txt;
	text_sizing_t sizing;
} txt_config_t;

typedef struct {
	txt_config_t config;
	uint16_t y_offset;
} txt_render_ctx_t;

STATIC_ARENA(frame_arena_a, FRAME_ARENA_SIZE);
STATIC_ARENA(frame_arena_b, FRAME_ARENA_SIZE);
static arena_t* frame_arena = &frame_arena_a;

extern const uint8_t ui_font_start[]   asm("_binary_CourierPrime_Bold_ttf_start");
extern const uint8_t ui_font_end[]     asm("_binary_CourierPrime_Bold_ttf_end");

static const char *TAG = "app";
static void* clay_mem = NULL;
static hal_fb_t fb;
static QueueHandle_t ui_queue;
static txt_renderer_t* txt_renderer;
static txt_font_t* fnt_label;

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
	ui_msg_t msg = {
		.type = UI_TOUCH_EVENT,
		.touch = *event,
	};
	xQueueSend(ui_queue, &msg, 0);
}

static void handle_ui_msg(ui_msg_t* msg)
{
	if (msg->type == UI_TOUCH_EVENT) {
		Clay_SetPointerState(
			(Clay_Vector2){
				.x = msg->touch.x,
				.y = msg->touch.y,
			},
			msg->touch.type != HAL_TOUCH_UP
		);
	}
}

static inline uint8_t
clay_to_gray(Clay_Color color) {
    float luma = 0.2126f * color.r + 0.7152f * color.g + 0.0722f * color.b;
    /*luma = luma * color.a + 1.0f * (1.0f - color.a);*/
    return (uint8_t)(luma * 255.0f + 0.5f);
}

static custom_element_t*
make_custom_element(custom_element_render_fn_t fn, void* ctx) {
	custom_element_t* element = arena_memalign(frame_arena, sizeof(custom_element_t), _Alignof(custom_element_t));
	*element = (custom_element_t){
		.render = fn,
		.ctx = ctx,
	};
	return element;
}

static inline void*
(make_frame_copy)(void* data, size_t size, size_t alignment) {
	void* copy = arena_memalign(frame_arena, size, alignment);
	memcpy(copy, data, size);
	return copy;
}

static void
render_text(void* ctx, const Clay_RenderCommand* command) {
	txt_render_ctx_t* render_ctx = ctx;

	txt_draw_t draw_args = render_ctx->config.txt;
	draw_args.x = command->boundingBox.x;
	draw_args.y = command->boundingBox.y;
	draw_args.max_width = command->boundingBox.width;
	draw_args.max_height = command->boundingBox.height;
	draw_args.output = fb;
	txt_draw(txt_renderer, &draw_args);
}

static void
text(Clay_ElementId id, const txt_config_t* config) {
	CLAY(id, {
		.layout = {
			.sizing = {
				.width = config->sizing == TEXT_SIZING_WRAP
					? CLAY_SIZING_GROW(0)
					: CLAY_SIZING_FIT(0),
				.height = CLAY_SIZING_FIT(0),
			},
		},
	}) {
		Clay_ElementData size_data = Clay_GetElementData(id);

		if (size_data.found) {
			txt_draw_t draw_args = config->txt;
			draw_args.output.pixels = NULL;
			if (config->sizing == TEXT_SIZING_WRAP) {
				draw_args.max_width = size_data.boundingBox.width;
			}
			txt_draw_result_t draw_result = txt_draw(txt_renderer, &draw_args);

			txt_render_ctx_t ctx = {
				.config = *config,
				.y_offset = draw_result.y_offset,
			};

			CLAY(CLAY_ID_LOCAL("Content"), {
				.layout = {
					.sizing = {
						.width = CLAY_SIZING_FIXED(draw_result.width),
						.height = CLAY_SIZING_FIXED(draw_result.height),
					},
				},
				.custom.customData = make_custom_element(render_text, make_frame_copy(ctx)),
			}) {
			}
		}
	}
}

static void render_task(void* arg)
{
	hal_input_set_callback(on_touch, NULL);

	txt_renderer = txt_renderer_create();
	fnt_label = txt_font_load(txt_renderer, ui_font_start, ui_font_end - ui_font_start, 30);

	while (true) {
		// Flip arena
		frame_arena = frame_arena == &frame_arena_a ? &frame_arena_b : &frame_arena_a;
		arena_reset(frame_arena);

		// Render
		Clay_BeginLayout();
		{
			CLAY(CLAY_ID("Main"), {
				.layout = {
					.sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
					.layoutDirection = CLAY_TOP_TO_BOTTOM,
				},
			}) {
				CLAY(CLAY_ID("TopBar"), {
					.layout = {
						.padding = CLAY_PADDING_ALL(6),
						.sizing = {
							.width = CLAY_SIZING_GROW(0),
							.height = CLAY_SIZING_FIT(0),
						},
						.childAlignment.x = CLAY_ALIGN_X_CENTER,
					},
					.border = { .width = CLAY_BORDER_ALL(4) },
				}) {
					CLAY(CLAY_ID("Title"), {
						.layout = {
							.sizing = {
								.width = CLAY_SIZING_FIXED(200),
								.height = CLAY_SIZING_FIXED(200)
							},
						},
						.backgroundColor = (Clay_Color){ .a = 1.f },
					}) {
					}
				}
				CLAY(CLAY_ID("Body"), {
					.layout = {
						.padding = CLAY_PADDING_ALL(6),
						.sizing = {
							.width = CLAY_SIZING_GROW(0),
							.height = CLAY_SIZING_GROW(0),
						},
						.childAlignment = {
							.x = CLAY_ALIGN_X_CENTER,
							.y = CLAY_ALIGN_Y_CENTER,
						},
					},
				}) {
					CLAY(CLAY_ID("Wrapper"), {
						.layout = {
							.padding = CLAY_PADDING_ALL(6),
							.sizing = {
								.width = CLAY_SIZING_PERCENT(0.5f),
								.height = CLAY_SIZING_GROW(0),
							},
							.childAlignment = {
								.x = CLAY_ALIGN_X_CENTER,
								.y = CLAY_ALIGN_Y_CENTER,
							},
						},
					}) {
						text(CLAY_ID("Text"), &(txt_config_t){
							.sizing = TEXT_SIZING_WRAP,
							.txt = {
								.text = "Hello world this text is long",
								.max_width = TXT_SIZE_UNLIMITED,
								.max_height = TXT_SIZE_UNLIMITED,
								.style = TXT_STYLE_DEFAULT(fnt_label),
							},
						});
					}
				}
			}
		}
		Clay_RenderCommandArray commands = Clay_EndLayout();

		draw_clear(fb, 0xFF);
		for (int32_t i = 0; i < commands.length; ++i) {
			const Clay_RenderCommand* cmd = &commands.internalArray[i];

			switch (cmd->commandType) {
				case CLAY_RENDER_COMMAND_TYPE_RECTANGLE: {
					draw_rect_filled(
						fb,
						cmd->boundingBox.x, cmd->boundingBox.y,
						cmd->boundingBox.width, cmd->boundingBox.height,
						clay_to_gray(cmd->renderData.border.color)
					);
				} break;
				case CLAY_RENDER_COMMAND_TYPE_BORDER: {
					draw_rect(
						fb,
						cmd->boundingBox.x, cmd->boundingBox.y,
						cmd->boundingBox.width, cmd->boundingBox.height,
						cmd->renderData.border.width.top,
						clay_to_gray(cmd->renderData.border.color)
					);
				} break;
				case CLAY_RENDER_COMMAND_TYPE_CUSTOM: {
					custom_element_t* element = cmd->renderData.custom.customData;
					element->render(element->ctx, cmd);
				} break;
				default:
					break;
			}
		}

		hal_display_blit(HAL_BLIT_FAST);

		// Drain the event queue
		{
			ui_msg_t msg;

			// Wait for first message indefinitely
			xQueueReceive(ui_queue, &msg, portMAX_DELAY);
			handle_ui_msg(&msg);

			// Pull all messages with debouncing
			while (xQueueReceive(ui_queue, &msg, portTICK_PERIOD_MS / 2) == pdPASS) {
				handle_ui_msg(&msg);
			}
		}
	}

	vTaskDelete(NULL);
}

static void on_clay_error(Clay_ErrorData errorText)
{
	ESP_LOGE("clay", "%.*s", errorText.errorText.length, errorText.errorText.chars);
}

void app_main(void)
{
    hal_init();
	hal_display_clear();

    fb = hal_display_get_fb();
	ESP_LOGI(TAG, "Device resolution = %d x %d", fb.width, fb.height);

	if (clay_mem == NULL) {
		Clay_SetMaxElementCount(512);
		Clay_SetMaxMeasureTextCacheWordCount(4);

		size_t clay_mem_size = Clay_MinMemorySize();
		clay_mem = malloc(clay_mem_size);
		Clay_Initialize(
			Clay_CreateArenaWithCapacityAndMemory(clay_mem_size, clay_mem),
			(Clay_Dimensions){
				.width = fb.width,
				.height = fb.height,
			},
			(Clay_ErrorHandler){ .errorHandlerFunction = on_clay_error }
		);
	}

	ui_queue = xQueueCreate(16, sizeof(ui_msg_t));

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
