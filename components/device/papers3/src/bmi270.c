#include <papers3.h>
#include <papers3/bmi270.h>

#include <driver/i2c.h>
#include "esp_log.h"

static const char *TAG = "bmi270";

/* I2C address */
#define BMI270_ADDR        0x68

/* Registers */
#define REG_CHIP_ID        0x00
#define REG_TEMP_LSB       0x22
#define REG_TEMP_MSB       0x23
#define REG_PWR_CONF       0x7C
#define REG_PWR_CTRL       0x7D
#define REG_INIT_CTRL      0x59
#define REG_INTERNAL_ST    0x21

/* Expected chip ID */
#define BMI270_CHIP_ID     0x24

/* ── Helpers ─────────────────────────────────────────────────────── */

static esp_err_t reg_write(uint8_t reg, uint8_t value)
{
    uint8_t buf[2] = { reg, value };
    return i2c_master_write_to_device(
        PAPERS3_I2C_PORT, BMI270_ADDR,
        buf, sizeof(buf),
        pdMS_TO_TICKS(10)
    );
}

static esp_err_t reg_read(uint8_t reg, uint8_t *out, size_t len)
{
    return i2c_master_write_read_device(
        PAPERS3_I2C_PORT, BMI270_ADDR,
        &reg, 1,
        out, len,
        pdMS_TO_TICKS(10)
    );
}

/* ── Public API ──────────────────────────────────────────────────── */

esp_err_t bmi270_init(void)
{
    esp_err_t err;

    /* Verify chip ID */
    uint8_t chip_id = 0;
    err = reg_read(REG_CHIP_ID, &chip_id, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read chip ID: %s", esp_err_to_name(err));
        return err;
    }
    if (chip_id != BMI270_CHIP_ID) {
        ESP_LOGE(TAG, "Unexpected chip ID: 0x%02X (expected 0x%02X)",
                 chip_id, BMI270_CHIP_ID);
        return ESP_ERR_NOT_FOUND;
    }

    /* Disable advanced power save so the temp sensor is accessible */
    err = reg_write(REG_PWR_CONF, 0x00);
    if (err != ESP_OK) return err;

    vTaskDelay(pdMS_TO_TICKS(1));   /* 450µs required after disabling APS */

    ESP_LOGI(TAG, "BMI270 initialised (chip ID 0x%02X)", chip_id);
    return ESP_OK;
}

esp_err_t bmi270_get_temperature(float *out_temp)
{
    if (out_temp == NULL) return ESP_ERR_INVALID_ARG;

    uint8_t buf[2] = { 0 };
    esp_err_t err = reg_read(REG_TEMP_LSB, buf, 2);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read temperature: %s", esp_err_to_name(err));
        return err;
    }

    /* BMI270 datasheet: temp = (raw / 512.0) + 23.0 */
    int16_t raw = (int16_t)((buf[1] << 8) | buf[0]);
    *out_temp = (raw / 512.0f) + 23.0f;

    return ESP_OK;
}
