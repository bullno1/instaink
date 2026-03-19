/**
 * @file draw.h
 * @brief Basic drawing primitives over a HAL framebuffer.
 */

#ifndef DRAW_H
#define DRAW_H

#include <hal.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Plot a single pixel, clipping silently if out of bounds.
 *
 * @param fb     Framebuffer to draw into.
 * @param x      X coordinate.
 * @param y      Y coordinate.
 * @param colour Pixel value (0 = black … 255 = white).
 */
void draw_pixel(hal_fb_t fb, int x, int y, uint8_t colour);

/**
 * @brief Draw a straight line between two points.
 *
 * Uses Bresenham's line algorithm. Coordinates outside the framebuffer
 * bounds are clipped per-pixel. The colour value follows the HAL Y8
 * convention: 0 = black, 255 = white.
 *
 * @param fb     Framebuffer to draw into.
 * @param x0     Start X coordinate.
 * @param y0     Start Y coordinate.
 * @param x1     End X coordinate.
 * @param y1     End Y coordinate.
 * @param colour Pixel value (0 = black … 255 = white).
 */
void draw_line(hal_fb_t fb,
               int x0, int y0,
               int x1, int y1,
               uint8_t colour);

/**
 * @brief Draw the outline of an axis-aligned rectangle.
 *
 * @param fb     Framebuffer to draw into.
 * @param x      Left edge X coordinate.
 * @param y      Top edge Y coordinate.
 * @param w      Width  in pixels.
 * @param h      Height in pixels.
 * @param colour Pixel value (0 = black … 255 = white).
 */
void draw_rect(hal_fb_t fb,
               int x, int y,
               int w, int h,
               uint8_t colour);

#ifdef __cplusplus
}
#endif

#endif /* DRAW_H */
