#include <hal.h>

#include "sokol/sokol_log.h"
#include "sokol/sokol_app.h"
#include "sokol/sokol_gfx.h"
#include "sokol/util/sokol_gl.h"
#include "sokol/sokol_glue.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

/* ── PaperS3 EPD geometry ────────────────────────────────────────────────── */
#define EPD_W   540u
#define EPD_H   980u

/* ==========================================================================
 * Command queue
 * ========================================================================== */

typedef enum {
    CMD_NONE  = 0,
    CMD_BLIT,       /* push g.fb to the screen              */
    CMD_CLEAR,      /* hardware clear → white               */
    CMD_QUIT,       /* tear down sokol; exit sokol thread   */
} cmd_t;

/* ==========================================================================
 * Global HAL state
 * ========================================================================== */

static struct {
	uint8_t fb[EPD_W * EPD_H];
	uint8_t rgba[EPD_W * EPD_H * 4];

	QueueHandle_t cmd_q;

	SemaphoreHandle_t touch_cb_lock;
	hal_touch_cb_t touch_cb;
	void* touch_ud;

	sg_image        tex;
	sg_sampler      smp;
	sg_view         tex_view;

	bool btn_down;
	bool active;
} g;

/* ==========================================================================
 * Internal helpers  (called only from the sokol thread)
 * ========================================================================== */

/*
 * Convert g.fb (Y8) → g.rgba (RGBA8) and upload to the streaming texture.
 */
static void fb_upload(void)
{
    const uint32_t npix = EPD_W * EPD_H;
    for (uint32_t i = 0; i < npix; i++) {
        const uint8_t y    = g.fb[i];
        g.rgba[i * 4 + 0]  = y;
        g.rgba[i * 4 + 1]  = y;
        g.rgba[i * 4 + 2]  = y;
        g.rgba[i * 4 + 3]  = 0xFFu;
    }
    sg_update_image(g.tex, &(sg_image_data){
        .mip_levels[0] = {
            .ptr  = g.rgba,
            .size = npix * 4u,
        }
    });
}

static void fb_draw(void)
{
    /* Accumulate draw command into sokol_gl's internal buffer */
    sgl_defaults();
    sgl_enable_texture();
    sgl_texture(g.tex_view, g.smp);

    sgl_begin_quads();
        sgl_t2f(0.0f, 0.0f);  sgl_v2f(-1.0f,  1.0f); /* top-left     */
        sgl_t2f(1.0f, 0.0f);  sgl_v2f( 1.0f,  1.0f); /* top-right    */
        sgl_t2f(1.0f, 1.0f);  sgl_v2f( 1.0f, -1.0f); /* bottom-right */
        sgl_t2f(0.0f, 1.0f);  sgl_v2f(-1.0f, -1.0f); /* bottom-left  */
    sgl_end();

    /* Open a render pass, flush the sokol_gl command, commit */
    sg_begin_pass(&(sg_pass){
        .action = {
            .colors[0] = {
                .load_action = SG_LOADACTION_CLEAR,
                .clear_value = { 1.0f, 1.0f, 1.0f, 1.0f }, /* white bg */
            }
        },
        .swapchain = sglue_swapchain(),
    });
    sgl_draw();
    sg_end_pass();
    sg_commit();
}

static void cb_init(void)
{
    sg_setup(&(sg_desc){
        .environment = sglue_environment(),
        .logger.func = slog_func,
    });
    sgl_setup(&(sgl_desc_t){
        .logger.func = slog_func,
    });

    /* Initialise framebuffer and RGBA scratch buffer to white */
    memset(g.fb,   0xFF, sizeof(g.fb));
    memset(g.rgba, 0xFF, sizeof(g.rgba));

    g.tex = sg_make_image(&(sg_image_desc){
        .width        = (int)EPD_W,
        .height       = (int)EPD_H,
        .pixel_format = SG_PIXELFORMAT_RGBA8,
        .usage.dynamic_update = true,
        .label = "epd_fb",
    });

	g.tex_view = sg_make_view(&(sg_view_desc){
		.texture.image = g.tex,
	});

    g.smp = sg_make_sampler(&(sg_sampler_desc){
        .min_filter = SG_FILTER_NEAREST, /* nearest-neighbour: no blurring    */
        .mag_filter = SG_FILTER_NEAREST,
        .wrap_u     = SG_WRAP_CLAMP_TO_EDGE,
        .wrap_v     = SG_WRAP_CLAMP_TO_EDGE,
        .label      = "epd_smp",
    });
}

static void cb_frame(void)
{
	cmd_t cmd;
	if (xQueueReceive(g.cmd_q, &cmd, 0) == pdPASS) {
		switch (cmd) {
			case CMD_BLIT:
				fb_upload();
				break;

			case CMD_CLEAR:
				memset(g.fb, 0xFF, sizeof(g.fb));
				fb_upload();
				break;

			default:
				break;
		}
	}

    fb_draw();
}

static void cb_event(const sapp_event *e)
{
    /* Read callback pointer atomically under lock */
    xSemaphoreTake(g.touch_cb_lock, portMAX_DELAY);
    const hal_touch_cb_t cb = g.touch_cb;
    void *const          ud = g.touch_ud;
    xSemaphoreGive(g.touch_cb_lock);

    if (!cb) return;

    /*
     * Map left-mouse-button events → single-point HAL touch events.
     * Mouse position is already in framebuffer pixel space (high_dpi=false).
     * The GT911 supports two touch points; we simulate id=0 only.
     */
    hal_touch_event_t te = {
        .id   = 0,
        .size = 12,
        .x    = (uint16_t)e->mouse_x,
        .y    = (uint16_t)e->mouse_y,
    };

    switch (e->type) {

    case SAPP_EVENTTYPE_MOUSE_DOWN:
        if (e->mouse_button == SAPP_MOUSEBUTTON_LEFT) {
            g.btn_down = true;
            te.type    = HAL_TOUCH_DOWN;
            cb(&te, ud);
        }
        break;

    case SAPP_EVENTTYPE_MOUSE_MOVE:
        if (g.btn_down) {
            te.type = HAL_TOUCH_MOVE;
            cb(&te, ud);
        }
        break;

    case SAPP_EVENTTYPE_MOUSE_UP:
        if (e->mouse_button == SAPP_MOUSEBUTTON_LEFT && g.btn_down) {
            g.btn_down = false;
            te.type    = HAL_TOUCH_UP;
            cb(&te, ud);
        }
        break;

    default:
        break;
    }
}

static void cb_cleanup(void)
{
    sg_destroy_view(g.tex_view);
    sg_destroy_image(g.tex);
    sg_destroy_sampler(g.smp);
    sgl_shutdown();
    sg_shutdown();
}

/* ==========================================================================
 * Sokol thread entry point
 * ========================================================================== */

static void sokol(void *arg)
{
    (void)arg;
    sapp_run(&(sapp_desc){
        .init_cb       = cb_init,
        .frame_cb      = cb_frame,
        .event_cb      = cb_event,
        .cleanup_cb    = cb_cleanup,
        .width         = (int)EPD_W,
        .height        = (int)EPD_H,
        .window_title  = "PaperS3 Simulator",
        .high_dpi      = false, /* pixel coords match FB coords directly     */
        .swap_interval = 1,     /* vsync; idle frames cost ~0 CPU            */
        .logger.func   = slog_func,
    });

	vTaskDelete(NULL);
}

/* ==========================================================================
 * Public HAL API
 * ========================================================================== */

void hal_init(void)
{
    if (g.active) return;

    memset(&g, 0, sizeof g);
	g.cmd_q = xQueueCreate(2, sizeof(cmd_t));
	g.touch_cb_lock = xSemaphoreCreateMutex();

	xTaskCreate(sokol, "hal", 4 * 1024 * 1024, NULL, 1, NULL);

    g.active = true;
}

void hal_deinit(void)
{
    if (!g.active) return;

    g.active = false;
}

hal_fb_t hal_display_get_fb(void)
{
    if (!g.active) {
        return (hal_fb_t){ .pixels = NULL, .width = 0, .height = 0 };
    }
    return (hal_fb_t){
        .pixels = g.fb,
        .width  = EPD_W,
        .height = EPD_H,
    };
}

/*
 * Post a blocking command to the sokol thread and wait for completion.
 *
 * The sokol thread picks it up on its next frame callback (≤ 1 frame ≈ 16 ms
 * at 60 fps), performs the operation, then calls cmd_ack() which unblocks us.
 */
static void post_and_wait(cmd_t type, hal_blit_mode_t mode)
{
	xQueueSend(g.cmd_q, &type, portMAX_DELAY);
}

void hal_display_blit(hal_blit_mode_t mode)
{
    if (!g.active) return;
    post_and_wait(CMD_BLIT, mode);
}

void hal_display_clear(void)
{
    if (!g.active) return;
    post_and_wait(CMD_CLEAR, HAL_BLIT_FULL);
}

void hal_input_set_callback(hal_touch_cb_t cb, void *user_data)
{
    xSemaphoreTake(g.touch_cb_lock, portMAX_DELAY);
    g.touch_cb = cb;
    g.touch_ud = user_data;
    xSemaphoreGive(g.touch_cb_lock);
}
