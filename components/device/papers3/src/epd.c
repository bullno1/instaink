/*
 * epdiy v2 board definition for M5Stack PaperS3
 *
 * Pin mapping confirmed from:
 *  - https://docs.m5stack.com/en/core/papers3
 *  - ESPHome working config (Frogy76/epdiy fork)
 *  - jslawek epdiy PR #392
 *
 *  D0–D7   : G6, G14, G7, G12, G9, G11, G8, G10
 *  PCLK    : G16  ← LCD_CAM pixel clock output
 *  XSTL    : G13  ← STH / horizontal start pulse  (NOT the clock)
 *  XLE     : G15  ← latch enable
 *  SPV     : G17  ← vertical start pulse
 *  CKV     : G18  ← gate clock (driven by RMT)
 *  PWR     : G45  ← EPD power rail enable
 *  BST_EN  : G46  ← boost converter enable (separate from PWR)
 */

#include "epd_board.h"
#include "output_lcd/lcd_driver.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "board_m5papers3";

/* ── Pins ────────────────────────────────────────────────────────── */
#define EPD_D0    6
#define EPD_D1   14
#define EPD_D2    7
#define EPD_D3   12
#define EPD_D4    9
#define EPD_D5   11
#define EPD_D6    8
#define EPD_D7   10

#define EPD_PCLK 16   /* LCD_CAM pixel clock output                  */
#define EPD_STH  13   /* XSTL — horizontal start pulse (STH)         */
#define EPD_LE   15   /* XLE  — latch enable                         */
#define EPD_SPV  17   /* SPV  — vertical start pulse                 */
#define EPD_CKV  18   /* CKV  — gate clock (driven by RMT)           */
#define EPD_PWR  45   /* EPD power rail enable                       */
#define EPD_BST  46   /* Boost converter enable                      */

/* ── Board callbacks ─────────────────────────────────────────────── */

static void papers3_init(uint32_t epd_row_width)
{
    /* PWR and BST are not part of lcd_bus_config_t — configure manually */
    gpio_config_t pwr_cfg = {
        .pin_bit_mask = (1ULL << EPD_PWR) | (1ULL << EPD_BST),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&pwr_cfg);
    gpio_set_level(EPD_PWR, 0);
    gpio_set_level(EPD_BST, 0);

    const LcdEpdConfig_t cfg = {
        .pixel_clock      = 12000000,  /* 12 MHz (reduced to 6/3 if cache line <64B) */
        .ckv_high_time    = 70,        /* 70 * 1/10µs = 7µs CKV high time            */
        .line_front_porch = 4,
        .le_high_time     = 4,
        .bus_width        = 8,         /* 8-bit parallel bus                          */

        .bus = {
            .data = {
                EPD_D0, EPD_D1, EPD_D2, EPD_D3,
                EPD_D4, EPD_D5, EPD_D6, EPD_D7,
                /* bus_width=8, so data[8..15] are never touched */
                GPIO_NUM_NC, GPIO_NUM_NC, GPIO_NUM_NC, GPIO_NUM_NC,
                GPIO_NUM_NC, GPIO_NUM_NC, GPIO_NUM_NC, GPIO_NUM_NC,
            },
            .clock       = EPD_PCLK,  /* G16 — LCD_CAM pixel clock output            */
            .ckv         = EPD_CKV,   /* G18 — gate clock via RMT                    */
            .start_pulse = EPD_STH,   /* G13 — XSTL / horizontal start pulse         */
            .leh         = EPD_LE,    /* G15 — XLE / latch enable                    */
            .stv         = EPD_SPV,   /* G17 — SPV / vertical start pulse            */
        },
    };

    epd_lcd_init(&cfg, 960, 540);

    ESP_LOGI(TAG, "PaperS3 LCD init done");
}

static void papers3_deinit(void)
{
    epd_lcd_deinit();
    gpio_set_level(EPD_BST, 0);
    gpio_set_level(EPD_PWR, 0);
}

static void papers3_poweron(epd_ctrl_state_t *state)
{
    gpio_set_level(EPD_PWR, 1);
    vTaskDelay(pdMS_TO_TICKS(1));
    gpio_set_level(EPD_BST, 1);
    vTaskDelay(pdMS_TO_TICKS(10));  /* wait for HV rail to stabilise */
    ESP_LOGI(TAG, "EPD power ON");
}

static void papers3_poweroff(epd_ctrl_state_t *state)
{
    gpio_set_level(EPD_BST, 0);
    vTaskDelay(pdMS_TO_TICKS(500));
    gpio_set_level(EPD_PWR, 0);
    ESP_LOGI(TAG, "EPD power OFF");
}

static void papers3_set_ctrl(epd_ctrl_state_t *state,
                              const epd_ctrl_state_t *const target)
{
    *state = *target;
}

/* ── Public board definition ─────────────────────────────────────── */
const EpdBoardDefinition epd_board_papers3 = {
    .init               = papers3_init,
    .deinit             = papers3_deinit,
    .set_ctrl           = papers3_set_ctrl,
    .poweron            = papers3_poweron,
    .poweroff           = papers3_poweroff,
};
