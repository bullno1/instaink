#include <hal.h>

#include "sokol/sokol_log.h"
#include "sokol/sokol_app.h"
#include "sokol/sokol_gfx.h"
#include "sokol/util/sokol_gl.h"
#include "sokol/sokol_glue.h"

#include <pthread.h>
#include <semaphore.h>
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

typedef struct {
    pthread_mutex_t mx;
    pthread_cond_t  c_done;     /* sokol → caller: command finished         */
    cmd_t           type;
    hal_blit_mode_t mode;
    bool            pending;    /* a command is waiting for the sokol thread */
    bool            done;       /* sokol has finished processing it          */
} cmdq_t;

/* ==========================================================================
 * Global HAL state
 * ========================================================================== */

static struct {
    /*
     * Y8 framebuffer: one byte per pixel, row-major, top-left origin.
     * 0x00 = black, 0xFF = white.
     * Exposed to the application via hal_display_get_fb().
     */
    uint8_t         fb  [EPD_W * EPD_H];

    /*
     * Scratch buffer for Y8→RGBA8 conversion before GPU upload.
     * Owned exclusively by the sokol thread during blit.
     */
    uint8_t         rgba[EPD_W * EPD_H * 4];

    /* Command queue (caller ↔ sokol thread) */
    cmdq_t          q;

    /* Sokol thread + init barrier */
    pthread_t       thread;
    sem_t           ready;      /* posted by cb_init() to unblock hal_init() */

    /* Touch callback (protected by cb_mx) */
    pthread_mutex_t cb_mx;
    hal_touch_cb_t  touch_cb;
    void           *touch_ud;

    /* sokol_gfx resources – created/destroyed by sokol thread */
    sg_image        tex;
    sg_sampler      smp;
	sg_view         tex_view;

    /* Mouse-down tracking for synthetic HAL_TOUCH_MOVE generation */
    bool            btn_down;

    bool            active;     /* true between hal_init() and hal_deinit()  */
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

/*
 * Draw g.tex as a full-window textured quad and present.
 *
 * NDC vertices (sokol_gl default projection = identity):
 *
 *   (-1,+1) ─────────── (+1,+1)   ← top of window
 *      │                    │
 *   (-1,-1) ─────────── (+1,-1)   ← bottom of window
 *
 * UV assignment (GL convention: V=0 = bottom of uploaded data = FB row 0):
 *
 *   (0, 0) ──────────── (1, 0)    ← FB row 0  (top of image)
 *      │                    │
 *   (0, 1) ──────────── (1, 1)    ← FB row 539 (bottom of image)
 *
 * ┌─ vertex order (quads) ───────────────────────────────────────────────┐
 * │  sgl_begin_quads() expects vertices in winding order: TL TR BR BL   │
 * └──────────────────────────────────────────────────────────────────────┘
 */
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

/*
 * Unblock the caller that is waiting in post_and_wait().
 * Must be called by the sokol thread after completing a command.
 */
static void cmd_ack(void)
{
    pthread_mutex_lock(&g.q.mx);
    g.q.done    = true;
    g.q.pending = false;
    pthread_cond_signal(&g.q.c_done);
    pthread_mutex_unlock(&g.q.mx);
}

/* ==========================================================================
 * sokol_app callbacks  (all execute on the sokol thread)
 * ========================================================================== */

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

    /*
     * Create a streaming RGBA8 texture pre-filled with white.
     * SG_USAGE_STREAM allows sg_update_image() on every blit call.
     */
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

    /* Signal hal_init() that sokol is ready */
    sem_post(&g.ready);
}

static void execute_command(cmd_t type)
{
	switch (type) {

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

static void cb_frame(void)
{
    /* Snapshot command state under lock; act outside it */
    pthread_mutex_lock(&g.q.mx);
    const bool            have = g.q.pending;
    const cmd_t           type = g.q.type;
    pthread_mutex_unlock(&g.q.mx);

    if (have) {
		execute_command(type);
        cmd_ack();
	}

    fb_draw();
}

static void cb_event(const sapp_event *e)
{
    /* Read callback pointer atomically under lock */
    pthread_mutex_lock(&g.cb_mx);
    const hal_touch_cb_t cb = g.touch_cb;
    void *const          ud = g.touch_ud;
    pthread_mutex_unlock(&g.cb_mx);

    if (!cb) return;

    /*
     * Map left-mouse-button events → single-point HAL touch events.
     * Mouse position is already in framebuffer pixel space (high_dpi=false).
     * The GT911 supports two touch points; we simulate id=0 only.
     */
    hal_touch_event_t te = {
        .id   = 0,
        .size = 1,
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

static void *sokol_thread_fn(void *arg)
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
    return NULL;
}

/* ==========================================================================
 * Public HAL API
 * ========================================================================== */

void hal_init(void)
{
    if (g.active) return;

    memset(&g, 0, sizeof g);

    /* Initialise synchronisation primitives */
    pthread_mutex_init(&g.q.mx,    NULL);
    pthread_cond_init (&g.q.c_done, NULL);
    pthread_mutex_init(&g.cb_mx,   NULL);
    sem_init(&g.ready, /*pshared=*/0, /*value=*/0);

    if (pthread_create(&g.thread, NULL, sokol_thread_fn, NULL) != 0) {
        perror("hal_init: pthread_create failed");
        return;
    }

    /* Block until sokol's cb_init() has completed and g.tex is ready */
    sem_wait(&g.ready);
    g.active = true;
}

void hal_deinit(void)
{
    if (!g.active) return;

    /*
     * Post CMD_QUIT and wait for the sokol thread to acknowledge.
     * After the ack, sapp_quit() has been called; we join to ensure
     * cb_cleanup() completes before we tear down the sync objects.
     */
    pthread_mutex_lock(&g.q.mx);
    g.q.type    = CMD_QUIT;
    g.q.pending = true;
    g.q.done    = false;
    /* No c_ready condvar needed: cb_frame() polls g.q.pending each frame */
    while (!g.q.done)
        pthread_cond_wait(&g.q.c_done, &g.q.mx);
    pthread_mutex_unlock(&g.q.mx);

    pthread_join(g.thread, NULL);

    sem_destroy         (&g.ready);
    pthread_cond_destroy (&g.q.c_done);
    pthread_mutex_destroy(&g.q.mx);
    pthread_mutex_destroy(&g.cb_mx);

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
	if (pthread_equal(pthread_self(), g.thread)) {
		execute_command(type);
	} else {
		pthread_mutex_lock(&g.q.mx);
		g.q.type    = type;
		g.q.mode    = mode;
		g.q.pending = true;
		g.q.done    = false;
		while (!g.q.done)
			pthread_cond_wait(&g.q.c_done, &g.q.mx);
		pthread_mutex_unlock(&g.q.mx);
	}
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
    pthread_mutex_lock(&g.cb_mx);
    g.touch_cb = cb;
    g.touch_ud = user_data;
    pthread_mutex_unlock(&g.cb_mx);
}

#define SOKOL_IMPL
#define SOKOL_GLCORE
#define SOKOL_NO_ENTRY
#include "sokol/sokol_log.h"
#include "sokol/sokol_app.h"
#include "sokol/sokol_gfx.h"
#include "sokol/util/sokol_gl.h"
#include "sokol/sokol_glue.h"
