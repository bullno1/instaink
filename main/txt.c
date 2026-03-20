/*
 * txt.c — FreeType-backed text renderer for the PaperS3 HAL.
 *
 * Glyph cache: direct-mapped hash table (CACHE_SLOTS entries).
 * On a hash collision the existing entry is unconditionally kicked.
 */

#include "txt.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * Glyph cache entry
 * ====================================================================== */

#define CACHE_SLOTS  512u   /* Must be a power of two */

typedef struct {
    txt_font_t *font;        /* NULL = empty slot                          */
    uint32_t    codepoint;
    int32_t     bearing_x;  /* FT bitmap_left  — pixels right of pen      */
    int32_t     bearing_y;  /* FT bitmap_top   — pixels above baseline    */
    int32_t     advance_x;  /* Horizontal pen advance, pixels             */
    uint16_t    bmp_w;
    uint16_t    bmp_h;
    uint8_t    *bmp;        /* Row-major Y8, one byte per pixel.
                               NULL when the glyph has no visible bitmap
                               (whitespace, zero-size glyphs, OOM).       */
} glyph_t;

/* =========================================================================
 * Opaque type bodies
 * ====================================================================== */

struct txt_renderer {
    FT_Library ft;
    glyph_t    cache[CACHE_SLOTS];
};

struct txt_font {
    txt_renderer_t *renderer;
    FT_Face         face;
    uint16_t        size_px;
};

/* =========================================================================
 * UTF-8 decoder
 * ====================================================================== */

/*
 * Decode one codepoint from *pp and advance *pp past it.
 * Returns 0 at end-of-string.
 * Returns U+FFFD and skips one byte on malformed input.
 */
static uint32_t utf8_next(const char **pp)
{
    const uint8_t *s = (const uint8_t *)*pp;
    uint32_t cp;
    unsigned len;

    if (*s == 0u) return 0u;

    if      (*s < 0x80u) { cp = *s;          len = 1u; }
    else if (*s < 0xC0u) { ++*pp; return 0xFFFDu; }      /* stray continuation */
    else if (*s < 0xE0u) { cp = *s & 0x1Fu;  len = 2u; }
    else if (*s < 0xF0u) { cp = *s & 0x0Fu;  len = 3u; }
    else                 { cp = *s & 0x07u;  len = 4u; }

    for (unsigned i = 1u; i < len; i++) {
        if ((s[i] & 0xC0u) != 0x80u) { ++*pp; return 0xFFFDu; }
        cp = (cp << 6) | (s[i] & 0x3Fu);
    }
    *pp += len;
    return cp;
}

/* =========================================================================
 * Cache internals
 * ====================================================================== */

static uint32_t cache_hash(const txt_font_t *font, uint32_t cp)
{
    /* Mix font pointer and codepoint with a Knuth multiplicative hash */
    uint32_t h = (uint32_t)((uintptr_t)font * 2654435761u) ^ cp;
    h ^= h >> 16;
    h *= 0x45d9f3bu;
    h ^= h >> 16;
    return h & (CACHE_SLOTS - 1u);
}

static void glyph_evict(glyph_t *g)
{
    free(g->bmp);
    memset(g, 0, sizeof *g);
}

/*
 * Return the cached glyph for (font, cp), rasterising it on a miss.
 *
 * On a hash collision the incumbent entry is evicted and the new glyph
 * takes the slot (direct-mapped / "kick on conflict" policy).
 *
 * Never returns NULL.  On any FreeType error the slot is populated with
 * advance_x == 0 and bmp == NULL so the call site never needs to branch.
 */
static glyph_t *cache_get(txt_renderer_t *r, txt_font_t *font, uint32_t cp)
{
    uint32_t idx = cache_hash(font, cp);
    glyph_t *g   = &r->cache[idx];

    if (g->font == font && g->codepoint == cp) return g;   /* cache hit  */

    glyph_evict(g);                                        /* cache miss */

    FT_UInt glyph_idx = FT_Get_Char_Index(font->face, cp);
    if (glyph_idx == 0                                                        ||
        FT_Load_Glyph(font->face, glyph_idx, FT_LOAD_DEFAULT)          != 0  ||
        FT_Render_Glyph(font->face->glyph, FT_RENDER_MODE_NORMAL)      != 0)
    {
        goto done;   /* store a blank entry; caller sees advance_x == 0 */
    }

    {
        FT_GlyphSlot slot  = font->face->glyph;
        FT_Bitmap   *bm    = &slot->bitmap;
        unsigned     pitch = (bm->pitch < 0)
                           ? (unsigned)(-bm->pitch)
                           : (unsigned)( bm->pitch);

        g->bearing_x = slot->bitmap_left;
        g->bearing_y = slot->bitmap_top;
        g->advance_x = (int32_t)(slot->advance.x >> 6);
        g->bmp_w     = (uint16_t)bm->width;
        g->bmp_h     = (uint16_t)bm->rows;

        if (bm->width > 0u && bm->rows > 0u) {
            g->bmp = (uint8_t *)malloc((size_t)bm->width * bm->rows);
            if (!g->bmp) goto done;   /* OOM — treat as invisible */

            /* Copy row by row to handle pitch != width */
            for (unsigned row = 0u; row < bm->rows; row++) {
                memcpy(g->bmp  +           (size_t)row * bm->width,
                       bm->buffer + (size_t)row * pitch,
                       bm->width);
            }
        }
    }

done:
    g->font      = font;
    g->codepoint = cp;
    return g;
}

/* =========================================================================
 * Renderer lifecycle
 * ====================================================================== */

txt_renderer_t *txt_renderer_create(void)
{
    txt_renderer_t *r = (txt_renderer_t *)calloc(1, sizeof *r);
    if (!r) return NULL;
    if (FT_Init_FreeType(&r->ft) != 0) { free(r); return NULL; }
    return r;
}

void txt_renderer_destroy(txt_renderer_t *r)
{
    if (!r) return;
    for (uint32_t i = 0u; i < CACHE_SLOTS; i++) glyph_evict(&r->cache[i]);
    FT_Done_FreeType(r->ft);
    free(r);
}

/* =========================================================================
 * Font management
 * ====================================================================== */

txt_font_t *txt_font_load(txt_renderer_t  *r,
                          const uint8_t   *data,
                          size_t           len,
                          uint16_t         size_px)
{
    txt_font_t *f = (txt_font_t *)calloc(1, sizeof *f);
    if (!f) return NULL;

    if (FT_New_Memory_Face(r->ft, (const FT_Byte *)data, (FT_Long)len, 0, &f->face) != 0)
        goto err;
    if (FT_Set_Pixel_Sizes(f->face, 0u, size_px) != 0)
        goto err_face;

    f->renderer = r;
    f->size_px  = size_px;
    return f;

err_face: FT_Done_Face(f->face);
err:      free(f); return NULL;
}

void txt_font_unload(txt_renderer_t *r, txt_font_t *font)
{
    if (!r || !font) return;
    for (uint32_t i = 0u; i < CACHE_SLOTS; i++)
        if (r->cache[i].font == font) glyph_evict(&r->cache[i]);
    FT_Done_Face(font->face);
    free(font);
}

/* =========================================================================
 * Glyph compositing
 * ====================================================================== */

/*
 * Greyscale-alpha composite one glyph onto the framebuffer at pixel
 * position (px, py) — the top-left corner of the glyph bitmap.
 * Out-of-bounds pixels are silently clipped.
 */
static void blit_glyph(const hal_fb_t *fb,
                       const glyph_t  *g,
                       int32_t         px,
                       int32_t         py,
                       uint8_t         fg)
{
    if (!fb->pixels || !g->bmp) return;  // Measuring or invalid glyph

    for (int32_t row = 0; row < (int32_t)g->bmp_h; row++) {
        int32_t dy = py + row;
        if (dy < 0 || dy >= (int32_t)fb->height) continue;

        for (int32_t col = 0; col < (int32_t)g->bmp_w; col++) {
            int32_t dx = px + col;
            if (dx < 0 || dx >= (int32_t)fb->width) continue;

            uint8_t  alpha = g->bmp[(size_t)row * g->bmp_w + (size_t)col];
            if (alpha == 0u) continue;

            uint8_t *dst = &fb->pixels[(size_t)dy * fb->width + (size_t)dx];

            if (alpha == 255u) {
                *dst = fg;
            } else {
                /* dst + round(alpha/255 × (fg − dst)) */
                *dst = (uint8_t)((int32_t)*dst +
                       ((int32_t)alpha * ((int32_t)fg - (int32_t)*dst) + 127) / 255);
            }
        }
    }
}

/* =========================================================================
 * Font metrics  (all values in pixels, matching FT 26.6 → integer shift)
 * ====================================================================== */

static int32_t font_ascender(const txt_font_t *f)
{
    return (int32_t)(f->face->size->metrics.ascender >> 6);
}

static int32_t font_descender(const txt_font_t *f)
{
    /* FT descender is negative; return a positive magnitude */
    return -(int32_t)(f->face->size->metrics.descender >> 6);
}

static int32_t font_line_height(const txt_font_t *f)
{
    return (int32_t)(f->face->size->metrics.height >> 6);
}

/* =========================================================================
 * Word-width measurement  (side-effect: warms the glyph cache)
 * ====================================================================== */

/*
 * Sum the advance widths of all codepoints in the next word starting at p.
 * A word ends at the first whitespace character, '\n', or '\0'.
 * Sets *end to the first character after the word.
 */
static int32_t measure_word(txt_renderer_t *r,
                             txt_font_t     *font,
                             const char     *p,
                             const char    **end)
{
    int32_t w = 0;
    while (*p != '\0' && *p != ' ' && *p != '\t' && *p != '\n') {
        uint32_t cp = utf8_next(&p);
        w += cache_get(r, font, cp)->advance_x;
    }
    *end = p;
    return w;
}

/* =========================================================================
 * txt_draw
 * ====================================================================== */

txt_draw_result_t txt_draw(txt_renderer_t   *renderer,
                           const txt_draw_t *args)
{
    txt_draw_result_t res = {0};
    if (!renderer || !args || !args->text || !args->style.font) return res;

    txt_font_t *const font      = args->style.font;
    const bool        do_render = (args->output.pixels != NULL);

    const int32_t box_left  = (int32_t)args->x;
    const int32_t box_right = (args->max_width == TXT_SIZE_UNLIMITED)
                            ? INT32_MAX
                            : box_left + (int32_t)args->max_width;

    const int32_t ascender  = font_ascender(font);
    const int32_t descender = font_descender(font);
    const int32_t lh        = font_line_height(font);

    /*
     * The bounding box is anchored at the top of the first line:
     *   box_top    = args->y − ascender
     *   box_bottom = box_top + max_height
     *
     * A line whose bottom edge (baseline + descender) exceeds box_bottom
     * is not rendered, including the very first line.
     */
    const int32_t box_bottom = (args->max_height == TXT_SIZE_UNLIMITED)
                             ? INT32_MAX
                             : ((int32_t)args->y - ascender + (int32_t)args->max_height);

    /* ---- Render state ---- */
    int32_t     baseline      = (int32_t)args->y;
    bool        first_line    = true;

    /* ---- Result accumulators ---- */
    int32_t     max_pen_x     = box_left;              /* rightmost advance, any line     */
    int32_t     last_end_x    = box_left + (int32_t)args->x_offset;
    int32_t     last_baseline = baseline;
    const char *p             = args->text;
    const char *resume        = args->text;            /* byte-level resume point         */
    bool        rendered_any  = false;

    while (*p != '\0') {
        /* ----------------------------------------------------------------
         * Pre-flight: reject any line whose bottom falls outside the box.
         * This enforces max_height on every line, including the first.
         * ---------------------------------------------------------------- */
        if (baseline + descender > box_bottom) break;

        /* First line starts at x + x_offset; all others start at box_left */
        const int32_t pen = first_line
                          ? (box_left + (int32_t)args->x_offset)
                          : box_left;
        first_line = false;

        /* ----------------------------------------------------------------
         * Word-packing pass: find the break point for this line.
         *
         * cur_x    — running pen estimate used for overflow checks.
         * line_end — one past the last committed word (render boundary).
         * scan     — lookahead; after the loop it is the resume point for
         *            the next line (past any inter-line whitespace).
         *
         * The first word is always committed even if it exceeds box_right,
         * which prevents an infinite loop on oversized single words.
         * ---------------------------------------------------------------- */
        int32_t     cur_x     = pen;
        const char *line_end  = p;
        const char *scan      = p;
        bool        first_word = true;

        while (*scan != '\0') {
            /* Consume inter-word whitespace (spaces and tabs, not newlines) */
            int32_t ws_w = 0;
            while (*scan == ' ' || *scan == '\t') {
                uint32_t cp = utf8_next(&scan);
                ws_w += cache_get(renderer, font, cp)->advance_x;
            }

            /* Hard newline: consume it, mark the line end, and stop */
            if (*scan == '\n') {
                ++scan;
                line_end = scan;
                break;
            }
            if (*scan == '\0') break;

            /* Measure the next word (also warms the glyph cache) */
            const char *word_end;
            const int32_t word_w = measure_word(renderer, font, scan, &word_end);

            if (first_word) {
                cur_x    += ws_w + word_w;
                line_end  = word_end;
                scan      = word_end;
                first_word = false;
            } else {
                if (cur_x + ws_w + word_w > box_right) {
                    /*
                     * Word does not fit.  `scan` is already past the
                     * whitespace so the next line skips the inter-line gap.
                     */
                    break;
                }
                cur_x   += ws_w + word_w;
                line_end = word_end;
                scan     = word_end;
            }
        }

        /* ----------------------------------------------------------------
         * Render pass: walk p..line_end and blit each glyph.
         *
         * Whitespace glyphs within the range are fed through cache_get so
         * the pen advances correctly, but they produce no visible pixels
         * (blit_glyph is a no-op when glyph_t::bmp is NULL).
         * ---------------------------------------------------------------- */
        int32_t    rx = pen;
        const char *rp = p;

        while (rp < line_end) {
            uint32_t cp = utf8_next(&rp);
            if (cp == '\n') break;

            glyph_t *g = cache_get(renderer, font, cp);

            if (do_render) {
                blit_glyph(&args->output, g,
                           rx + g->bearing_x,
                           baseline - g->bearing_y,
                           args->style.fg);
            }
            rx += g->advance_x;
        }

        /* ---- Update accumulators ---- */
        if (rx > max_pen_x) max_pen_x = rx;
        last_end_x    = rx;
        last_baseline = baseline;
        resume        = scan;
        rendered_any  = true;

        p         = scan;
        baseline += lh;
    }

    /* ---- Populate result ---- */

    /*
     * num_chars_drawn is the byte offset into args->text of the first
     * character not yet rendered.  The caller resumes with
     * args->text + result.num_chars_drawn.
     */
    res.num_chars_drawn = (size_t)(resume - args->text);

    if (rendered_any) {
        res.width  = (max_pen_x > box_left)
                   ? (uint16_t)(max_pen_x - box_left)
                   : 0u;
        /*
         * Height spans from the top of the first line to the bottom of
         * the last:  (last_baseline + descender) − (args->y − ascender)
         */
        res.height = (uint16_t)((last_baseline + descender)
                              - ((int32_t)args->y - ascender));
        res.next_x = (uint16_t)last_end_x;
        res.next_y = (uint16_t)(last_baseline + lh);
    } else {
        res.next_x = (uint16_t)(box_left + (int32_t)args->x_offset);
        res.next_y = (uint16_t)((int32_t)args->y + lh);
    }

    return res;
}
