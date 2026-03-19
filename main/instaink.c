#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_psram.h"
#include "esp_mac.h"
#include "esp_log.h"

static const char *TAG = "main";

void app_main(void)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);

    uint32_t flash_size = 0;
    esp_flash_get_size(NULL, &flash_size);

    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);

    ESP_LOGI(TAG, "╔══════════════════════════════╗");
    ESP_LOGI(TAG, "║        PaperS3 Boot          ║");
    ESP_LOGI(TAG, "╚══════════════════════════════╝");
    ESP_LOGI(TAG, "Chip:    ESP32-S3 rev %d", chip.revision);
    ESP_LOGI(TAG, "Cores:   %d", chip.cores);
    ESP_LOGI(TAG, "Flash:   %"PRIu32" MB", flash_size / (1024 * 1024));
    ESP_LOGI(TAG, "PSRAM:   %zu MB", esp_psram_get_size() / (1024 * 1024));
    ESP_LOGI(TAG, "MAC:     %02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    ESP_LOGI(TAG, "Features:%s%s%s",
             (chip.features & CHIP_FEATURE_WIFI_BGN) ? " WiFi"  : "",
             (chip.features & CHIP_FEATURE_BT)       ? " BT"    : "",
             (chip.features & CHIP_FEATURE_BLE)      ? " BLE"   : "");
}
