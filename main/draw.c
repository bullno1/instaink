/**
 * @file draw.c
 * @brief Basic drawing primitives over a HAL framebuffer.
 */

#include "draw.h"

/* Absolute value without pulling in math.h */
#define ABS(x) ((x) < 0 ? -(x) : (x))

void draw_pixel(hal_fb_t fb, int x, int y, uint8_t colour)
{
    if (x >= 0 && x < fb.width && y >= 0 && y < fb.height) {
        fb.pixels[y * fb.width + x] = colour;
    }
}

void draw_line(hal_fb_t fb,
               int x0, int y0,
               int x1, int y1,
               uint8_t colour)
{
    int dx =  ABS(x1 - x0);
    int dy = -ABS(y1 - y0);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    while (1) {
		draw_pixel(fb, x0, y0, colour);

        if (x0 == x1 && y0 == y1) break;

        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void draw_rect(hal_fb_t fb,
               int x, int y,
               int w, int h,
               uint8_t colour)
{
    draw_line(fb, x,         y,         x + w - 1, y,         colour); /* top    */
    draw_line(fb, x,         y + h - 1, x + w - 1, y + h - 1, colour); /* bottom */
    draw_line(fb, x,         y,         x,         y + h - 1, colour); /* left   */
    draw_line(fb, x + w - 1, y,         x + w - 1, y + h - 1, colour); /* right  */
}
