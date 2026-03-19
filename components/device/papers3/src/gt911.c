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

/* ── Helpers ─────────────────────────────────────────────────────── */

static esp_err_t reg_read(uint16_t reg, uint8_t *out, size_t len)
{
    uint8_t addr[2] = { reg >> 8, reg & 0xFF };
    return i2c_master_write_read_device(
        PAPERS3_I2C_PORT, GT911_ADDR,
        addr, sizeof(addr),
        out, len,
        pdMS_TO_TICKS(10)
    );
}

static esp_err_t reg_write_byte(uint16_t reg, uint8_t value)
{
    uint8_t buf[3] = { reg >> 8, reg & 0xFF, value };
    return i2c_master_write_to_device(
        PAPERS3_I2C_PORT, GT911_ADDR,
        buf, sizeof(buf),
        pdMS_TO_TICKS(10)
    );
}

/* ── Address latch sequence ──────────────────────────────────────── */

static void gt911_latch_address(void)
{
    /* Drive INT low as output — this latches address 0x14 on reset */
    gpio_config_t out_conf = {
        .pin_bit_mask = (1ULL << GT911_INT_PIN),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&out_conf);
    gpio_set_level(GT911_INT_PIN, 0);

    /* Hold low long enough for the GT911 to sample it on power-up */
    vTaskDelay(pdMS_TO_TICKS(10));

    /* Switch INT back to input — device firmware boot takes ~50ms */
    gpio_config_t in_conf = {
        .pin_bit_mask = (1ULL << GT911_INT_PIN),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&in_conf);

    vTaskDelay(pdMS_TO_TICKS(50));
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

    /* Latch I2C address before attempting any communication */
    gt911_latch_address();

    /* Verify product ID */
    uint8_t product_id[4] = {0};
    esp_err_t err = reg_read(REG_PRODUCT_ID, product_id, sizeof(product_id));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read product ID: %s", esp_err_to_name(err));
        return err;
    }
    if (product_id[0] != '9' || product_id[1] != '1' || product_id[2] != '1') {
        ESP_LOGE(TAG, "Unexpected product ID: %.4s", (char *)product_id);
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGI(TAG, "product ID: %.4s", (char *)product_id);

    /* Binary semaphore — multiple ISR fires collapse into one read */
    s_touch_sem = xSemaphoreCreateBinary();
    if (s_touch_sem == NULL) return ESP_ERR_NO_MEM;

    /* Install ISR on rising edge — INT goes high when data is ready */
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
