#ifndef TXT_H
#define TXT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <hal.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Pass as max_width or max_height to impose no limit. */
#define TXT_SIZE_UNLIMITED  (0xFFFFu)


/**
 * @brief Text renderer context.
 *
 * Owns the FreeType library instance, the glyph cache, and all loaded
 * fonts. One context is sufficient for an entire application.
 */
typedef struct txt_renderer txt_renderer_t;

/**
 * @brief Loaded font at a fixed pixel size.
 *
 * Always associated with the renderer that created it. The same font
 * data may be opened multiple times at different sizes; each produces an
 * independent handle.
 */
typedef struct txt_font txt_font_t;


/* =========================================================================
 * Renderer lifecycle
 * ====================================================================== */

/**
 * @brief Create a text renderer.
 *
 * Initialises FreeType and allocates an internal glyph cache.
 *
 * @return  Pointer to a newly allocated renderer, or NULL on failure.
 */
txt_renderer_t *txt_renderer_create(void);

/**
 * @brief Destroy a renderer and free all associated resources.
 *
 * All font handles obtained from this renderer become invalid after this
 * call.
 *
 * @param r  Renderer to destroy. NULL is a no-op.
 */
void txt_renderer_destroy(txt_renderer_t *r);


/* =========================================================================
 * Font management
 * ====================================================================== */

/**
 * @brief Load a font from a memory buffer at a given pixel size.
 *
 * The buffer must remain valid for the lifetime of the font handle.
 * Ownership of the buffer stays with the caller.
 *
 * @param r        Renderer that will own this font.
 * @param data     Pointer to raw font file data (TrueType or OpenType).
 * @param len      Size of @p data in bytes.
 * @param size_px  Glyph height in pixels (em square).
 * @return  Font handle, or NULL on failure.
 */
txt_font_t *txt_font_load(txt_renderer_t  *r,
                          const uint8_t   *data,
                          size_t           len,
                          uint16_t         size_px);

/**
 * @brief Unload a font and evict all of its glyphs from the cache.
 *
 * @param r     Renderer that owns @p font.
 * @param font  Handle to unload. NULL is a no-op.
 */
void txt_font_unload(txt_renderer_t *r, txt_font_t *font);


/* =========================================================================
 * Style
 * ====================================================================== */

/**
 * @brief Visual properties for a run of text.
 */
typedef struct {
    txt_font_t *font; /**< (Required) Font and size to use.                 */
    uint8_t     fg;   /**< Glyph colour, Y8. 0 = black, 255 = white.       */
} txt_style_t;

/** Convenience initialiser — black text. */
#define TXT_STYLE_DEFAULT(fnt)  { .font = (fnt), .fg = 0 }


/* =========================================================================
 * Draw arguments
 * ====================================================================== */

/**
 * @brief Arguments passed to txt_draw().
 */
typedef struct {
    /** NUL-terminated UTF-8 string to render. */
    const char    *text;

    /**
     * @brief Top-left X origin of the text box in framebuffer pixels.
     *
     * The pen starts at (x + x_offset) for the first glyph.
     */
    uint16_t       x;

    /**
     * @brief Baseline Y position of the first line in framebuffer pixels
     *        (top-left origin).
     */
    uint16_t       y;

    /**
     * @brief Additional horizontal offset applied before rendering the
     *        first glyph.
     *
     * Use this when continuing a mixed-style line: pass the next_x value
     * returned by the previous txt_draw() call as (next_x - x) so the
     * pen resumes exactly where the prior run ended, while x still
     * anchors the left edge of the shared text box for wrapping purposes.
     */
    uint16_t       x_offset;

    /** Visual style for this text run. */
    txt_style_t    style;

    /**
     * @brief Maximum line width in pixels before word-wrapping.
     *
     * The wrap column is (x + max_width). Set to TXT_SIZE_UNLIMITED to
     * disable wrapping.
     */
    uint16_t       max_width;

    /**
     * @brief Maximum total height of the bounding box in pixels.
     *
     * The bounding box spans from @p y to (y + max_height). A line is
     * only rendered if it fits entirely within that box — including the
     * very first line. Rendering stops before any line whose full height
     * (ascender + descender) would exceed the bottom of the box.
     *
     * Set to TXT_SIZE_UNLIMITED to impose no limit.
     */
    uint16_t       max_height;

    /**
     * @brief Destination framebuffer.
     *
     * When pixels is non-NULL, glyphs are composited into this buffer.
     * When pixels is NULL the function performs a dry-run: glyphs are
     * still rasterised and cached, but no pixels are written. This is
     * the intended way to measure text extents.
     *
     * Obtain a valid buffer with hal_display_get_fb(). A zeroed-out
     * hal_fb_t (all members zero / NULL) is the canonical "no output"
     * value.
     */
    hal_fb_t       output;
} txt_draw_t;


/* =========================================================================
 * Draw result
 * ====================================================================== */

/**
 * @brief Metrics returned by txt_draw().
 *
 * All coordinates and sizes are in framebuffer pixel units.
 */
typedef struct {
    /**
     * @brief Pixel width of the drawn content.
     *
     * Measured from x to the rightmost painted (or advanced) pixel.
     */
    uint16_t  width;

    /**
     * @brief Pixel height of the drawn content.
     *
     * Distance from the top of the first line to the bottom of the last.
     */
    uint16_t  height;

    /**
     * @brief Number of UTF-8 characters (codepoints) actually rendered.
     *
     * May be less than strlen(text) when max_width / max_height caused
     * early termination. Inspect this value to resume rendering the
     * remainder of a string.
     */
    size_t    num_chars_drawn;

    /**
     * @brief X coordinate at which the next glyph would begin.
     *
     * Use as x_offset in a follow-on txt_draw() call to append a
     * differently-styled run on the same line.
     */
    uint16_t  next_x;

    /**
     * @brief Baseline Y coordinate for the next line.
     *
     * Equal to the baseline Y of the last rendered line plus line height.
     * Use to position a subsequent txt_draw() call directly below this
     * block.
     */
    uint16_t  next_y;
} txt_draw_result_t;


/* =========================================================================
 * Drawing / measuring
 * ====================================================================== */

/**
 * @brief Render or measure a UTF-8 text run.
 *
 * **Rendering** (args->output.pixels != NULL):
 *   Glyphs are rasterised, cached, and composited into args->output using
 *   greyscale alpha blending. Text is always word-wrapped at max_width and
 *   clipped at max_height. The framebuffer is not flushed to the display;
 *   call hal_display_blit() when all compositing is complete.
 *
 * **Measuring** (args->output.pixels == NULL):
 *   Identical to rendering, except no pixels are written. Glyphs are still
 *   rasterised and inserted into the cache so that a subsequent rendering
 *   call incurs no extra rasterisation cost.
 *
 * Word-wrapping always breaks on whitespace boundaries. A line that
 * contains a single word wider than max_width is rendered as-is on one
 * line without mid-word breaking.
 *
 * Rendering halts before starting any line whose baseline would fall
 * beyond (args->y + args->max_height).
 *
 * @param renderer  Text renderer context.
 * @param args      Draw arguments. Must not be NULL.
 * @return          Metrics describing the rendered or measured text.
 *                  All fields are zero on error.
 */
txt_draw_result_t txt_draw(txt_renderer_t    *renderer,
                           const txt_draw_t  *args);

#ifdef __cplusplus
}
#endif

#endif /* TXT_H */
