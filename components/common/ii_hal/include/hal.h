#ifndef HAL_H
#define HAL_H

#include <stdint.h>
#include <stdbool.h>

/* =========================================================================
 * Common lifecycle
 * ====================================================================== */

/**
 * @brief Initialise the HAL
 *
 * Must be called once before any other hal_* function.
 */
void hal_init(void);

/**
 * @brief Tear down the HAL and release all resources.
 */
void hal_deinit(void);

/* =========================================================================
 * Display
 * ====================================================================== */

/**
 * @brief Blit mode — governs EPD waveform quality vs. speed.
 *
 * HAL_BLIT_FULL  High-quality 16-level greyscale waveform (~800 ms).
 *                Use for initial renders and image content.
 *
 * HAL_BLIT_FAST  Low-latency 2-level (black/white) waveform (~120 ms).
 *                Use for UI updates, animations, and incremental redraws.
 *                On Linux both modes repaint the window immediately.
 */
typedef enum {
    HAL_BLIT_FULL = 0,
    HAL_BLIT_FAST,
} hal_blit_mode_t;

/**
 * @brief Framebuffer descriptor returned by hal_display_get_fb().
 *
 * Pixels are laid out row-major, top-left origin, one byte per pixel (Y8).
 * Value range: 0 = black, 255 = white.
 * Total buffer size in bytes: width * height.
 *
 * The pixels pointer is valid between hal_init() and hal_deinit() and
 * never changes address. width and height reflect the actual resolution
 * of the active back-end's display.
 */
typedef struct {
    uint8_t  *pixels; /**< Framebuffer data (HAL-owned; do not free). */
    uint16_t  width;  /**< Display width  in pixels.                  */
    uint16_t  height; /**< Display height in pixels.                  */
} hal_fb_t;

/**
 * @brief Return the HAL-owned framebuffer and its dimensions.
 *
 * Write pixels directly into fb.pixels, then call hal_display_blit() to
 * push them to the screen.
 *
 * @return hal_fb_t with a valid pixels pointer, or {NULL, 0, 0} if the
 *         HAL has not been initialised.
 */
hal_fb_t hal_display_get_fb(void);

/**
 * @brief Push the current framebuffer contents to the display.
 *
 * The implementation handles EPD power sequencing (power on -> blit ->
 * power off) internally. The call blocks until the waveform completes.
 *
 * @param mode  HAL_BLIT_FULL or HAL_BLIT_FAST.
 */
void hal_display_blit(hal_blit_mode_t mode);

/**
 * @brief Hardware-clear the EPD panel to white.
 *
 * Uses the EPD's built-in clear waveform, which is faster and more
 * thorough than blitting a white framebuffer. Use this to remove
 * ghosting between major content changes.
 *
 * The call blocks until the clear waveform completes.
 */
void hal_display_clear(void);

/* =========================================================================
 * Input
 * ====================================================================== */

/** Touch event type */
typedef enum {
    HAL_TOUCH_DOWN = 0, /**< Finger placed on panel  */
    HAL_TOUCH_MOVE,     /**< Finger moved on panel   */
    HAL_TOUCH_UP,       /**< Finger lifted            */
} hal_touch_type_t;

/** Touch event payload */
typedef struct {
    hal_touch_type_t type; /**< Down, Move, or Up.                          */
    uint8_t          id;   /**< Touch-point tracking ID (0 or 1 on GT911).  */
    uint16_t         x;    /**< X coordinate in framebuffer pixel space.    */
    uint16_t         y;    /**< Y coordinate in framebuffer pixel space.    */
    uint16_t         size; /**< Contact area hint (arbitrary units; 1 if
                                unavailable).                               */
} hal_touch_event_t;

/**
 * @brief Touch event callback.
 *
 * Invoked by the HAL for every raw touch report from the GT911 (device)
 * or the mouse (Linux). Gesture recognition is intentionally left to the
 * application layer.
 *
 * @param event      Touch event data.
 * @param user_data  Opaque pointer supplied at registration time.
 */
typedef void (*hal_touch_cb_t)(const hal_touch_event_t *event, void *user_data);

/**
 * @brief Register the touch event callback.
 *
 * Only one callback is active at a time; calling this again replaces the
 * previous registration. Pass NULL to unregister.
 *
 * @param cb         Callback function, or NULL.
 * @param user_data  Forwarded to cb unchanged.
 */
void hal_input_set_callback(hal_touch_cb_t cb, void *user_data);

#endif /* HAL_H */
