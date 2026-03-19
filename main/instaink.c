#include <stdio.h>
#include <esp_log.h>
#include "esp_chip_info.h"
#include "esp_psram.h"
#include "driver/i2c.h"

static const char *TAG = "papers3";

static void log_chip_info(void)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    ESP_LOGI(TAG, "ESP32-S3, %d cores, WiFi%s%s",
             chip.cores,
             (chip.features & CHIP_FEATURE_BT) ? "/BT" : "",
             (chip.features & CHIP_FEATURE_BLE) ? "/BLE" : "");
    ESP_LOGI(TAG, "Flash: %dMB %s",
             (int)(esp_flash_default_chip->size / (1024 * 1024)),
             (chip.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");
}

static void log_psram_info(void)
{
    if (esp_psram_is_initialized()) {
        ESP_LOGI(TAG, "PSRAM: %zu KB available", esp_psram_get_size() / 1024);
    } else {
        ESP_LOGE(TAG, "PSRAM not initialized — check sdkconfig (SPIRAM_MODE_OCT required)");
    }
}

static esp_err_t i2c_bus_init(void)
{
    i2c_config_t conf = {
        .mode             = I2C_MODE_MASTER,
        .sda_io_num       = I2C_SDA,
        .scl_io_num       = I2C_SCL,
        .sda_pullup_en    = GPIO_PULLUP_ENABLE,
        .scl_pullup_en    = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 400000,  // 400kHz — GT911 and BM8563 both support this
    };
    esp_err_t ret = i2c_param_config(I2C_NUM_0, &conf);
    if (ret != ESP_OK) return ret;
    return i2c_driver_install(I2C_NUM_0, conf.mode, 0, 0, 0);
}

void
app_main(void) {
    ESP_LOGI(TAG, "PaperS3 starting up");

    log_chip_info();
    log_psram_info();

    ESP_ERROR_CHECK(i2c_bus_init());
    ESP_LOGI(TAG, "I2C bus initialized (SDA=%d, SCL=%d)", I2C_SDA, I2C_SCL);

    // TODO: init EPD via epdiy
    // TODO: init GT911 touch
    // TODO: init BM8563 RTC
    // TODO: init BMI270 IMU

    while (1) {
        ESP_LOGI(TAG, "Running...");
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
