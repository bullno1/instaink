#include <papers3.h>
#include <papers3/gt911.h>

#include "driver/i2c.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "gt911";

#define GT911_ADDR          0x14
#define GT911_INT_PIN       48

#define REG_PRODUCT_ID      0x8140
#define REG_STATUS          0x814E
#define REG_POINT_1         0x814F

#define STATUS_BUFFER_READY (1 << 7)
#define STATUS_POINT_MASK   0x0F
#define POINT_SIZE          8

static SemaphoreHandle_t  s_touch_sem = NULL;
static gt911_touch_cb_t   s_touch_cb  = NULL;
static void              *s_user_data = NULL;
static const uint8_t GT911_ADDRS[] = { 0x14, 0x5D };
static uint8_t s_addr = 0;

/* ── Helpers ─────────────────────────────────────────────────────── */

static esp_err_t gt911_probe_address(void)
{
    for (int i = 0; i < sizeof(GT911_ADDRS); i++) {
        uint8_t product_id[4] = {0};
        uint16_t reg = REG_PRODUCT_ID;
        uint8_t  addr_buf[2] = { reg >> 8, reg & 0xFF };

        esp_err_t err = i2c_master_write_read_device(
            PAPERS3_I2C_PORT, GT911_ADDRS[i],
            addr_buf, sizeof(addr_buf),
            product_id, sizeof(product_id),
            pdMS_TO_TICKS(10));

        if (err == ESP_OK &&
            product_id[0] == '9' &&
            product_id[1] == '1' &&
            product_id[2] == '1') {
            s_addr = GT911_ADDRS[i];
            ESP_LOGI(TAG, "found at 0x%02X (product ID: %.4s)",
                     s_addr, (char *)product_id);
            return ESP_OK;
        }
    }
    ESP_LOGE(TAG, "GT911 not found at 0x14 or 0x5D");
    return ESP_ERR_NOT_FOUND;
}

static esp_err_t reg_read(uint16_t reg, uint8_t *out, size_t len)
{
    uint8_t addr[2] = { reg >> 8, reg & 0xFF };
    return i2c_master_write_read_device(
        PAPERS3_I2C_PORT, s_addr,
        addr, sizeof(addr),
        out, len,
        pdMS_TO_TICKS(10)
    );
}

static esp_err_t reg_write_byte(uint16_t reg, uint8_t value)
{
    uint8_t buf[3] = { reg >> 8, reg & 0xFF, value };
    return i2c_master_write_to_device(
        PAPERS3_I2C_PORT, s_addr,
        buf, sizeof(buf),
        pdMS_TO_TICKS(10)
    );
}

/* ── ISR ─────────────────────────────────────────────────────────── */

static void IRAM_ATTR gt911_isr_handler(void *arg)
{
    xSemaphoreGiveFromISR(s_touch_sem, NULL);
}

/* ── Touch task ──────────────────────────────────────────────────── */

static void gt911_touch_task(void *arg)
{
    gt911_state_t touch;

    while (1) {
        xSemaphoreTake(s_touch_sem, portMAX_DELAY);
        while (xSemaphoreTake(s_touch_sem, pdMS_TO_TICKS(10)) == pdTRUE) {
		}

        if (gt911_read(&touch) == ESP_OK && s_touch_cb != NULL) {
            s_touch_cb(&touch, s_user_data);
        }
    }
}

/* ── Public API ──────────────────────────────────────────────────── */

esp_err_t gt911_init(gt911_touch_cb_t touch_cb, void *user_data)
{
    s_touch_cb  = touch_cb;
    s_user_data = user_data;

    esp_err_t err = gt911_probe_address();
    if (err != ESP_OK) return err;

    s_touch_sem = xSemaphoreCreateBinary();
    if (s_touch_sem == NULL) return ESP_ERR_NO_MEM;

    gpio_config_t int_in = {
        .pin_bit_mask = (1ULL << GT911_INT_PIN),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&int_in);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(GT911_INT_PIN, gt911_isr_handler, NULL);
    gpio_set_intr_type(GT911_INT_PIN, GPIO_INTR_POSEDGE);

    xTaskCreate(gt911_touch_task, "gt911", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "init ok");
    return ESP_OK;
}

esp_err_t gt911_read(gt911_state_t *out_touch)
{
    if (out_touch == NULL) return ESP_ERR_INVALID_ARG;

    out_touch->count = 0;

    uint8_t status = 0;
    esp_err_t err = reg_read(REG_STATUS, &status, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read status: %s", esp_err_to_name(err));
        return err;
    }

    if (!(status & STATUS_BUFFER_READY)) {
        return ESP_OK;
    }

    uint8_t count = status & STATUS_POINT_MASK;
    if (count > GT911_MAX_POINTS) {
        count = GT911_MAX_POINTS;
    }

    if (count > 0) {
        uint8_t buf[GT911_MAX_POINTS * POINT_SIZE];
        err = reg_read(REG_POINT_1, buf, count * POINT_SIZE);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read points: %s", esp_err_to_name(err));
            reg_write_byte(REG_STATUS, 0x00);
            return err;
        }

        for (uint8_t i = 0; i < count; i++) {
            const uint8_t *p = &buf[i * POINT_SIZE];
			out_touch->points[i].id = p[0];
            out_touch->points[i].x = (uint16_t)(p[1] | (p[2] << 8));
            out_touch->points[i].y = (uint16_t)(p[3] | (p[4] << 8));
            out_touch->points[i].size = (uint16_t)(p[5] | (p[6] << 8));

            ESP_LOGD(TAG, "point[%d]: x=%d y=%d",
                     i, out_touch->points[i].x, out_touch->points[i].y);
        }
    }

    out_touch->count = count;

    /* Clear status → INT goes low → ready for next touch */
    reg_write_byte(REG_STATUS, 0x00);

    return ESP_OK;
}
