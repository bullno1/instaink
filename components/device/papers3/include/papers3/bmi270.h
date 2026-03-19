#ifndef PAPERS3_BMI270_H
#define PAPERS3_BMI270_H

#include "esp_err.h"

/*
 * BMI270 — gyroscope + temperature sensor
 * I2C address: 0x68
 * Bus: shared I2C_NUM_0 via i2c_manager
 */

/**
 * Initialise the BMI270.
 */
esp_err_t bmi270_init(void);

/**
 * Read the onboard temperature sensor.
 * Accuracy: ±1°C
 * Returns temperature in degrees Celsius.
 */
esp_err_t bmi270_get_temperature(float *out_temp);

#endif
