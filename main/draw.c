#include "draw.h"
#include <string.h>

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
			   int thickness,
               uint8_t colour)
{
    for (int i = 0; i < thickness; i++) {
        int d = i - (thickness - 1) / 2;
        draw_line(fb, x - d,         y - d,         x + w - 1 + d, y - d,         colour); /* top    */
        draw_line(fb, x - d,         y + h - 1 + d, x + w - 1 + d, y + h - 1 + d, colour); /* bottom */
        draw_line(fb, x - d,         y - d,         x - d,         y + h - 1 + d, colour); /* left   */
        draw_line(fb, x + w - 1 + d, y - d,         x + w - 1 + d, y + h - 1 + d, colour); /* right  */
    }
}

void draw_rect_filled(hal_fb_t fb,
                      int x, int y,
                      int w, int h,
                      uint8_t colour)
{
    for (int row = y; row < y + h; row++) {
        draw_line(fb, x, row, x + w - 1, row, colour);
    }
}

void draw_clear(hal_fb_t fb, uint8_t color)
{
	memset(fb.pixels, color, fb.width * fb.height);
}
