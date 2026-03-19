#ifndef PAPERS3_GT911_H
#define PAPERS3_GT911_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

/*
 * GT911 — capacitive touch controller
 * I2C address : 0x14
 * INT pin     : G48
 * Bus         : initialised by papers3_init()
 */

#define GT911_MAX_POINTS 2

typedef struct {
	uint8_t id;
    uint16_t x;
    uint16_t y;
    uint16_t size;
} gt911_point_t;

typedef struct {
    uint8_t       count;
    gt911_point_t points[GT911_MAX_POINTS];
} gt911_touch_t;

/**
 * Callback invoked from the touch task when new touch data is ready.
 * Runs in its own task context — safe to call gt911_read() from here.
 */
typedef void (*gt911_touch_cb_t)(const gt911_touch_t *touch, void *user_data);

/**
 * Initialise the GT911 and install the INT pin interrupt.
 * touch_cb is called from a dedicated task whenever a touch is detected.
 * Requires papers3_init() to have been called first.
 */
esp_err_t gt911_init(gt911_touch_cb_t touch_cb, void *user_data);

/**
 * Read current touch state.
 * Normally called from within the touch_cb — clears the INT pin.
 */
esp_err_t gt911_read(gt911_touch_t *out_touch);

#endif
