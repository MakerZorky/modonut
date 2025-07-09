#include <esp_log.h>
#include <esp_err.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <driver/gpio.h>
#include <esp_event.h>

#include "application.h"
#include "system_info.h"

#define TAG "main"

extern "C" void app_main(void)
{
    // Initialize NVS flash for WiFi configuration
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "Erasing NVS flash to fix corruption");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    
    // 增强NVS初始化错误处理
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS Flash init failed: %s", esp_err_to_name(ret));
        
        // 强制擦除并重新初始化
        ESP_LOGW(TAG, "Force erasing and reinitializing NVS flash");
        esp_err_t erase_ret = nvs_flash_erase();
        if (erase_ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to erase NVS flash: %s", esp_err_to_name(erase_ret));
            ESP_LOGE(TAG, "System cannot continue without NVS. Rebooting in 5 seconds...");
            vTaskDelay(pdMS_TO_TICKS(5000));
            esp_restart();
        }
        
        ret = nvs_flash_init();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "NVS Flash reinit failed: %s", esp_err_to_name(ret));
            ESP_LOGE(TAG, "System cannot continue without NVS. Rebooting in 5 seconds...");
            vTaskDelay(pdMS_TO_TICKS(5000));
            esp_restart();
        } else {
            ESP_LOGI(TAG, "NVS Flash reinitialized successfully");
        }
    } else {
        ESP_LOGI(TAG, "NVS Flash initialized successfully");
    }

    // Launch the application
    Application::GetInstance().Start();
    // The main thread will exit and release the stack memory
}
