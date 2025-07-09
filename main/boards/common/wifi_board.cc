#include "wifi_board.h"

#include "display.h"
#include "application.h"
#include "system_info.h"
#include "font_awesome_symbols.h"
#include "settings.h"
#include "assets/lang_config.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_http.h>
#include <esp_mqtt.h>
#include <esp_udp.h>
#include <tcp_transport.h>
#include <tls_transport.h>
#include <web_socket.h>
#include <esp_log.h>

#include <wifi_station.h>
#include <wifi_configuration_ap.h>
#include <ssid_manager.h>
#include <app_network.h>
#include <esp_rmaker_core.h>
#include <esp_rmaker_ota.h>

static const char *TAG = "WifiBoard";

WifiBoard::WifiBoard() {
    Settings settings("wifi", true);
    wifi_config_mode_ = settings.GetInt("force_ap") == 1;
    if (wifi_config_mode_) {
        ESP_LOGI(TAG, "force_ap is set to 1, reset to 0");
        settings.SetInt("force_ap", 0);
    }
}

std::string WifiBoard::GetBoardType() {
    return "wifi";
}

void WifiBoard::EnterWifiConfigMode() {
    auto& application = Application::GetInstance();
    application.SetDeviceState(kDeviceStateWifiConfiguring);

    auto& wifi_ap = WifiConfigurationAp::GetInstance();
    wifi_ap.SetLanguage(Lang::CODE);
    wifi_ap.SetSsidPrefix("Xiaozhi");
    wifi_ap.Start();

    // 显示 WiFi 配置 AP 的 SSID 和 Web 服务器 URL
    std::string hint = Lang::Strings::CONNECT_TO_HOTSPOT;
    hint += wifi_ap.GetSsid();
    hint += Lang::Strings::ACCESS_VIA_BROWSER;
    hint += wifi_ap.GetWebServerUrl();
    hint += "\n\n";
    
    // 播报配置 WiFi 的提示
    application.Alert(Lang::Strings::WIFI_CONFIG_MODE, hint.c_str(), "", Lang::Sounds::P3_WIFICONFIG);
    
    // Wait forever until reset after configuration
    while (true) {
        int free_sram = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        int min_free_sram = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
        ESP_LOGI(TAG, "Free internal: %u minimal internal: %u", free_sram, min_free_sram);
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

void WifiBoard::StartNetwork() {
    auto& application = Application::GetInstance();
    // auto builtin_led = Board::GetInstance().GetBuiltinLed();

#if AP_CONNECT
    // User can press BOOT button while starting to enter WiFi configuration mode
    if (wifi_config_mode_) {
        EnterWifiConfigMode();
        return;
    }

    // If no WiFi SSID is configured, enter WiFi configuration mode
    auto& ssid_manager = SsidManager::GetInstance();
    auto ssid_list = ssid_manager.GetSsidList();
    if (ssid_list.empty()) {
        wifi_config_mode_ = true;
        EnterWifiConfigMode();
        return;
    }

    auto& wifi_station = WifiStation::GetInstance();
    wifi_station.OnScanBegin([this]() {
        auto display = Board::GetInstance().GetDisplay();
        display->ShowNotification(Lang::Strings::SCANNING_WIFI, 30000);
    });
    wifi_station.OnConnect([this](const std::string& ssid) {
        auto display = Board::GetInstance().GetDisplay();
        std::string notification = Lang::Strings::CONNECT_TO;
        notification += ssid;
        notification += "...";
        display->ShowNotification(notification.c_str(), 30000);
    });
    wifi_station.OnConnected([this](const std::string& ssid) {
        auto display = Board::GetInstance().GetDisplay();
        std::string notification = Lang::Strings::CONNECTED_TO;
        notification += ssid;
        display->ShowNotification(notification.c_str(), 30000);
    });
    wifi_station.Start();

    // Try to connect to WiFi, if failed, launch the WiFi configuration AP
    if (!wifi_station.WaitForConnected(60 * 1000)) {
        wifi_station.Stop();
        wifi_config_mode_ = true;
        EnterWifiConfigMode();
        return;
    }
#endif 

#if BLE_CONNECT
    // 检查NVS状态
    esp_err_t nvs_ret = nvs_flash_init();
    if (nvs_ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS Flash not initialized, attempting recovery");
        nvs_flash_erase();
        nvs_ret = nvs_flash_init();
        if (nvs_ret != ESP_OK) {
            ESP_LOGE(TAG, "NVS Flash recovery failed: %s", esp_err_to_name(nvs_ret));
            ESP_LOGE(TAG, "Rainmaker requires NVS to function. Rebooting...");
            vTaskDelay(pdMS_TO_TICKS(3000));
            esp_restart();
        }
    }
    
    app_network_init();
    
    /* Initialize the ESP RainMaker Agent.
    * Note that this should be called after app_network_init() but before app_network_start()
    * */
    esp_rmaker_config_t rainmaker_cfg = {
        .enable_time_sync = false,
    };
    esp_rmaker_node_t *node = esp_rmaker_node_init(&rainmaker_cfg, "ESP RainMaker Device", "modomodo");
    if (!node) {
        ESP_LOGE(TAG, "Could not initialise node. Aborting!!!");
        ESP_LOGE(TAG, "This may be due to NVS Flash issues. Attempting recovery...");
        
        // 尝试恢复NVS
        nvs_flash_erase();
        nvs_flash_init();
        
        // 重新尝试初始化Rainmaker
        node = esp_rmaker_node_init(&rainmaker_cfg, "ESP RainMaker Device", "modomodo");
        if (!node) {
            ESP_LOGE(TAG, "Rainmaker initialization failed even after NVS recovery");
            ESP_LOGE(TAG, "System will continue without Rainmaker functionality");
            application.Alert("Warning", "Rainmaker init failed, continuing without it");
            return;
        } else {
            ESP_LOGI(TAG, "Rainmaker initialized successfully after NVS recovery");
        }
    }

    bool wifi_provisioned = app_network_get_provisioned();
    if (!wifi_provisioned) {
        ESP_LOGI(TAG, "Starting provisioning");
        application.Alert("Info", "Configuring WiFi");
        // builtin_led->SetBlue();
        // builtin_led->Blink(1000, 500);
        /* Create a device and add the relevant parameters to it */
        esp_rmaker_device_t *modomodo_device = esp_rmaker_device_create("modomodo_V1", NULL, NULL);
        esp_rmaker_node_add_device(node, modomodo_device);
    } else {
        ESP_LOGI(TAG, "Already provisioned, starting Wi-Fi STA");
        application.Alert("Info", "Connecting WiFi");
    }

    /* Start the ESP RainMaker Agent.必须写在这里，因为每次联网都需要这句话 */
    esp_rmaker_start();

    esp_err_t err = app_network_start(POP_TYPE_NONE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not start Wifi. Aborting!!!");
        application.Alert("Info", "wifi connection failed");
        vTaskDelay(pdMS_TO_TICKS(5000));
        abort();
    }

    /* Enable OTA */
    esp_rmaker_ota_enable_default();
    
#endif
}

Http* WifiBoard::CreateHttp() {
    return new EspHttp();
}

WebSocket* WifiBoard::CreateWebSocket() {
#ifdef CONFIG_CONNECTION_TYPE_WEBSOCKET
    std::string url = CONFIG_WEBSOCKET_URL;
    if (url.find("wss://") == 0) {
        return new WebSocket(new TlsTransport());
    } else {
        return new WebSocket(new TcpTransport());
    }
#endif
    return nullptr;
}

Mqtt* WifiBoard::CreateMqtt() {
    return new EspMqtt();
}

Udp* WifiBoard::CreateUdp() {
    return new EspUdp();
}

const char* WifiBoard::GetNetworkStateIcon() {
    if (wifi_config_mode_) {
        return FONT_AWESOME_WIFI;
    }
    auto& wifi_station = WifiStation::GetInstance();
    if (!wifi_station.IsConnected()) {
        return FONT_AWESOME_WIFI_OFF;
    }
    int8_t rssi = wifi_station.GetRssi();
    if (rssi >= -60) {
        return FONT_AWESOME_WIFI;
    } else if (rssi >= -70) {
        return FONT_AWESOME_WIFI_FAIR;
    } else {
        return FONT_AWESOME_WIFI_WEAK;
    }
}

std::string WifiBoard::GetBoardJson() {
    // Set the board type for OTA
    auto& wifi_station = WifiStation::GetInstance();
    std::string board_json = std::string("{\"type\":\"" BOARD_TYPE "\",");
    board_json += "\"name\":\"" BOARD_NAME "\",";
    if (!wifi_config_mode_) {
        board_json += "\"ssid\":\"" + wifi_station.GetSsid() + "\",";
        board_json += "\"rssi\":" + std::to_string(wifi_station.GetRssi()) + ",";
        board_json += "\"channel\":" + std::to_string(wifi_station.GetChannel()) + ",";
        board_json += "\"ip\":\"" + wifi_station.GetIpAddress() + "\",";
    }
    board_json += "\"mac\":\"" + SystemInfo::GetMacAddress() + "\"}";
    return board_json;
}

void WifiBoard::SetPowerSaveMode(bool enabled) {
    auto& wifi_station = WifiStation::GetInstance();
    wifi_station.SetPowerSaveMode(enabled);
}

void WifiBoard::ResetWifiConfiguration() {
    // Set a flag and reboot the device to enter the network configuration mode
    {
        Settings settings("wifi", true);
        settings.SetInt("force_ap", 1);
    }
    GetDisplay()->ShowNotification(Lang::Strings::ENTERING_WIFI_CONFIG_MODE);
    vTaskDelay(pdMS_TO_TICKS(1000));
    // Reboot the device
    esp_restart();
}
