#ifndef PAPERS3_PAPERS3_H
#define PAPERS3_PAPERS3_H

#include <driver/i2c.h>
#include <esp_err.h>

/*
 * Common PaperS3 hardware definitions and initialisation.
 * All device modules (bmi270, bm8563, gt911 …) assume
 * papers3_init() has been called before their own init.
 */

#define PAPERS3_I2C_PORT  I2C_NUM_0
#define PAPERS3_I2C_SDA   41
#define PAPERS3_I2C_SCL   42
#define PAPERS3_I2C_SPEED 400000

/**
 * Initialise shared hardware: I2C bus.
 * Call once at startup before any device init.
 */
esp_err_t papers3_init(void);

/**
 * Release shared hardware.
 */
esp_err_t papers3_deinit(void);

#endif
