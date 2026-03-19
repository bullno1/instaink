#include <papers3.h>
#include <esp_log.h>

static const char *TAG = "papers3_common";
static bool s_installed = false;

esp_err_t papers3_init(void)
{
    if (s_installed) {
        return ESP_OK;
    }

    i2c_config_t conf = {
        .mode             = I2C_MODE_MASTER,
        .sda_io_num       = PAPERS3_I2C_SDA,
        .scl_io_num       = PAPERS3_I2C_SCL,
        .sda_pullup_en    = GPIO_PULLUP_ENABLE,
        .scl_pullup_en    = GPIO_PULLUP_ENABLE,
        .master.clk_speed = PAPERS3_I2C_SPEED,
    };

    esp_err_t err = i2c_param_config(PAPERS3_I2C_PORT, &conf);
    if (err != ESP_OK) return err;

    err = i2c_driver_install(PAPERS3_I2C_PORT, I2C_MODE_MASTER, 0, 0, 0);
    if (err != ESP_OK) return err;

    s_installed = true;
    ESP_LOGI(TAG, "I2C bus installed (SDA=%d SCL=%d %dHz)",
             PAPERS3_I2C_SDA, PAPERS3_I2C_SCL, PAPERS3_I2C_SPEED);
    return ESP_OK;
}

esp_err_t papers3_release(void)
{
    if (!s_installed) {
        return ESP_OK;
    }

    esp_err_t err = i2c_driver_delete(PAPERS3_I2C_PORT);
    if (err != ESP_OK) return err;

    s_installed = false;
    ESP_LOGI(TAG, "I2C bus uninstalled");
    return ESP_OK;
}
