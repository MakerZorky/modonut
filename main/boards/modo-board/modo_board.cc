#include "audio_codecs/box_audio_codec.h"
#include "system_reset.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "iot/thing_manager.h"
#include "led/single_led.h"
#include "ws2812_led.h"
#include "rc522.h"
#include "axp2101.h"
#include "assets/lang_config.h"

#include <esp_log.h>
#include <driver/i2c_master.h>
#include <driver/spi_master.h>
#include <nvs_flash.h>

#define TAG "ModoBoard"

class ModoBoard : public Board {
private:
    // 硬件组件
    Button boot_button_;
    Button volume_up_button_;
    Button volume_down_button_;
    Ws2812Led* ws2812_led_;
    Rc522* rc522_;
    Axp2101* axp2101_;
    
    // I2C总线 - 分离音频编解码器和AXP2101的I2C总线
    i2c_master_bus_handle_t i2c_bus_;           // 音频编解码器使用
    i2c_master_bus_handle_t axp2101_i2c_bus_;   // AXP2101专用
    
    // 状态变量
    bool nfc_card_present_;
    std::string current_nfc_uid_;
    bool is_connected_;
    
    // 静态标志位 - 用于彻底禁用I2C相关功能
    static bool es8311_available_;
    static bool axp2101_available_;

    void InitializeI2c_CODEC() {
        // 恢复I2C初始化，无条件执行
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = (i2c_port_t)1,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus_));
        ESP_LOGI(TAG, "I2C bus initialized");
    }

    void InitializeI2c_AXP2101() {
        // 恢复I2C初始化，无条件执行
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = AXP2101_I2C_PORT,
            .sda_io_num = AXP2101_GPIO_SDA,
            .scl_io_num = AXP2101_GPIO_SCL,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &axp2101_i2c_bus_));
        ESP_LOGI(TAG, "AXP2101 I2C bus initialized");
    }

    void InitializeButtons() {
        // BOOT按钮：长按清空NVS，短按切换聊天状态或进入配网
        boot_button_.OnClick([this]() {
            OnButtonPressed("boot");
            auto& app = Application::GetInstance();
            
            // 检查是否已经在配网过程中
            if (app.GetDeviceState() == kDeviceStateStarting && !is_connected_) {
                // 检查是否已经在配网
                static bool provisioning_started = false;
                if (!provisioning_started) {
                    // 启动RainMaker BLE配网
                    ESP_LOGI(TAG, "Starting RainMaker BLE provisioning...");
                    provisioning_started = true;
                    StartNetwork();
                } else {
                    ESP_LOGI(TAG, "Provisioning already started, ignoring button press");
                }
            } else {
                app.ToggleChatState();
            }
        });
        
        boot_button_.OnLongPress([this]() {
            // 通知Application长按事件
            OnButtonLongPressed("boot");
            
            ESP_LOGI(TAG, "Long press detected, clearing NVS");
            ClearNVS();
        });

        // 音量增加按钮 - 只在音频可用时启用
        volume_up_button_.OnClick([this]() {
            // 通知Application按钮事件
            OnButtonPressed("volume_up");
            
            if (!es8311_available_) {
                ESP_LOGI(TAG, "Volume up button pressed (audio not available)");
                return;
            }
            
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() + 10;
            if (volume > 100) {
                volume = 100;
            }
            codec->SetOutputVolume(volume);
            ShowVolumeIndicator(volume);
        });

        volume_up_button_.OnLongPress([this]() {
            // 通知Application长按事件
            OnButtonLongPressed("volume_up");
            
            if (!es8311_available_) {
                ESP_LOGI(TAG, "Volume up long press (audio not available)");
                return;
            }
            
            GetAudioCodec()->SetOutputVolume(100);
            ShowVolumeIndicator(100);
        });

        // 音量减少按钮 - 只在音频可用时启用
        volume_down_button_.OnClick([this]() {
            // 通知Application按钮事件
            OnButtonPressed("volume_down");
            
            if (!es8311_available_) {
                ESP_LOGI(TAG, "Volume down button pressed (audio not available)");
                return;
            }
            
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() - 10;
            if (volume < 0) {
                volume = 0;
            }
            codec->SetOutputVolume(volume);
            ShowVolumeIndicator(volume);
        });

        volume_down_button_.OnLongPress([this]() {
            // 通知Application长按事件
            OnButtonLongPressed("volume_down");
            
            if (!es8311_available_) {
                ESP_LOGI(TAG, "Volume down long press (audio not available)");
                return;
            }
            
            GetAudioCodec()->SetOutputVolume(0);
            ShowVolumeIndicator(0);
        });
    }

    void InitializeWS2812() {
        ws2812_led_ = new Ws2812Led(WS2812_GPIO, WS2812_LED_COUNT);
        ws2812_led_->SetAllPixels(0, 0, 255); // 蓝色表示待机
        ws2812_led_->Show();
    }

    void InitializeRC522() {
        rc522_ = new Rc522(RC522_SPI_HOST, RC522_GPIO_MISO, RC522_GPIO_MOSI, 
                           RC522_GPIO_SCLK, RC522_GPIO_CS, RC522_GPIO_RST);
        if (rc522_->Init()) {
            rc522_->SetCardDetectedCallback([this](const std::string& uid) {
                OnNFCCardDetected(uid);
            });
            rc522_->SetCardRemovedCallback([this]() {
                OnNFCCardRemoved();
            });
            rc522_->StartCardDetection();
            ESP_LOGI(TAG, "RC522 initialized successfully");
        } else {
            ESP_LOGE(TAG, "Failed to initialize RC522");
        }
    }

    void InitializeAXP2101() {
        // 无条件初始化AXP2101
        axp2101_ = new Axp2101(axp2101_i2c_bus_, AXP2101_I2C_ADDR);
        
        // 设置低电量回调
        axp2101_->SetLowBatteryCallback([this](int level) {
            OnLowBattery(level);
        });
        
        // 设置充电状态回调
        axp2101_->SetChargingStateCallback([this](bool charging) {
            OnChargingStateChanged(charging);
        });
        
        // 开始监控
        axp2101_->StartMonitoring();
        
        // 显示当前电量
        int battery_level = axp2101_->GetBatteryLevel();
        ShowBatteryLevel(battery_level);
        
        ESP_LOGI(TAG, "AXP2101 initialized, battery level: %d%%", battery_level);
    }

    void ClearNVS() {
        ESP_LOGI(TAG, "Clearing NVS...");
        
        // 显示清除状态
        if (ws2812_led_) {
            ws2812_led_->SetAllPixels(255, 255, 0); // 黄色表示正在清除
            ws2812_led_->Show();
        }
        
        // 清除NVS
        esp_err_t ret = nvs_flash_erase();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to erase NVS: %s", esp_err_to_name(ret));
            if (ws2812_led_) {
                ws2812_led_->SetAllPixels(255, 0, 0); // 红色表示失败
                ws2812_led_->Show();
            }
            vTaskDelay(pdMS_TO_TICKS(2000));
            return;
        }
        
        // 重新初始化NVS
        ret = nvs_flash_init();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to reinitialize NVS: %s", esp_err_to_name(ret));
            if (ws2812_led_) {
                ws2812_led_->SetAllPixels(255, 0, 0); // 红色表示失败
                ws2812_led_->Show();
            }
            vTaskDelay(pdMS_TO_TICKS(2000));
            return;
        }
        
        ESP_LOGI(TAG, "NVS cleared and reinitialized successfully");
        
        // 显示成功状态
        if (ws2812_led_) {
            ws2812_led_->SetAllPixels(0, 255, 0); // 绿色表示成功
            ws2812_led_->Show();
        }
        
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        // 重启设备
        ESP_LOGI(TAG, "Restarting device...");
        esp_restart();
    }

    void OnLowBattery(int level) {
        ESP_LOGW(TAG, "Low battery: %d%%", level);
        ShowBatteryLevel(level);
        
        // 可以在这里添加低电量警告音效
        PlayNotificationSound("low_battery");
    }

    void OnProvisioningSuccess() {
        ESP_LOGI(TAG, "Provisioning successful");
        ws2812_led_->SetAllPixels(0, 255, 0); // 绿色表示成功
        ws2812_led_->Show();
        vTaskDelay(pdMS_TO_TICKS(2000));
        // UpdateStatusDisplay(); // 删除
    }

    void OnProvisioningFailure(const std::string& error) {
        ESP_LOGE(TAG, "Provisioning failed: %s", error.c_str());
        ws2812_led_->SetAllPixels(255, 0, 0); // 红色表示失败
        ws2812_led_->Show();
        vTaskDelay(pdMS_TO_TICKS(2000));
        // UpdateStatusDisplay(); // 删除
    }

    // 物联网初始化
    void InitializeIot() {
        auto& thing_manager = iot::ThingManager::GetInstance();
        thing_manager.AddThing(iot::CreateThing("Speaker"));
        thing_manager.AddThing(iot::CreateThing("Lamp"));
    }

    bool DetectES8311() {
        if (!i2c_bus_) {
            ESP_LOGI(TAG, "I2C bus not initialized for ES8311 detection");
            return false;
        }
        i2c_device_config_t dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = AUDIO_CODEC_ES8311_ADDR,
            .scl_speed_hz = 100000,
        };
        i2c_master_dev_handle_t dev_handle = NULL;
        esp_err_t ret = i2c_master_bus_add_device(i2c_bus_, &dev_cfg, &dev_handle);
        if (ret != ESP_OK || dev_handle == NULL) {
            ESP_LOGE(TAG, "Failed to add ES8311 device: %s", esp_err_to_name(ret));
            return false;
        }
        uint8_t device_id = 0;
        ret = i2c_master_receive(dev_handle, &device_id, 1, pdMS_TO_TICKS(1000));
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "ES8311 detected at address 0x%02X, device ID: 0x%02X", AUDIO_CODEC_ES8311_ADDR, device_id);
            i2c_master_bus_rm_device(dev_handle);
            return true;
        } else {
            ESP_LOGE(TAG, "ES8311 not detected at address 0x%02X, error: %s", AUDIO_CODEC_ES8311_ADDR, esp_err_to_name(ret));
            i2c_master_bus_rm_device(dev_handle);
            return false;
        }
    }

    bool DetectAXP2101() {
        if (!axp2101_i2c_bus_) {
            ESP_LOGI(TAG, "AXP2101 I2C bus not initialized");
            return false;
        }
        i2c_device_config_t dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = AXP2101_I2C_ADDR,
            .scl_speed_hz = 100000,
        };
        i2c_master_dev_handle_t dev_handle = NULL;
        esp_err_t ret = i2c_master_bus_add_device(axp2101_i2c_bus_, &dev_cfg, &dev_handle);
        if (ret != ESP_OK || dev_handle == NULL) {
            ESP_LOGE(TAG, "Failed to add AXP2101 device: %s", esp_err_to_name(ret));
            return false;
        }
        uint8_t device_id = 0;
        ret = i2c_master_receive(dev_handle, &device_id, 1, pdMS_TO_TICKS(1000));
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "AXP2101 detected at address 0x%02X", AXP2101_I2C_ADDR);
            i2c_master_bus_rm_device(dev_handle);
            return true;
        } else {
            ESP_LOGE(TAG, "AXP2101 not detected at address 0x%02X, error: %s", AXP2101_I2C_ADDR, esp_err_to_name(ret));
            i2c_master_bus_rm_device(dev_handle);
            return false;
        }
    }

public:
    ModoBoard() : boot_button_(BOOT_BUTTON_GPIO), 
                  volume_up_button_(VOLUME_UP_BUTTON_GPIO),
                  volume_down_button_(VOLUME_DOWN_BUTTON_GPIO) {
        ESP_LOGI(TAG, "Initializing MODO board...");
        
        // // 在构造函数中完成所有硬件初始化
        InitializeAllHardware();
        
        ESP_LOGI(TAG, "MODO board initialization completed");
    }

    ~ModoBoard() {
        if (ws2812_led_) delete ws2812_led_;
        if (rc522_) delete rc522_;
        if (axp2101_) delete axp2101_;
        if (i2c_bus_) i2c_del_master_bus(i2c_bus_);
        if (axp2101_i2c_bus_) i2c_del_master_bus(axp2101_i2c_bus_);
    }

    virtual Led* GetLed() override {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    virtual AudioCodec* GetAudioCodec() override {
        // 无条件返回音频编解码器实例
        static BoxAudioCodec audio_codec(
            i2c_bus_, 
            AUDIO_INPUT_SAMPLE_RATE, 
            AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_MCLK, 
            AUDIO_I2S_GPIO_BCLK, 
            AUDIO_I2S_GPIO_WS, 
            AUDIO_I2S_GPIO_DOUT, 
            AUDIO_I2S_GPIO_DIN,
            AUDIO_CODEC_PA_PIN, 
            AUDIO_CODEC_ES8311_ADDR, 
            AUDIO_CODEC_ES7210_ADDR, 
            AUDIO_INPUT_REFERENCE);
        return &audio_codec;
    }

    // 设置连接状态
    void SetConnected(bool connected) {
        is_connected_ = connected;
        // UpdateStatusDisplay(); // 删除
    }

    // === 实现新的Board接口 ===
    
    // 硬件状态指示接口
    virtual void ShowVolumeIndicator(int volume) override {
        if (!ws2812_led_) return;
        
        int led_count = (volume * WS2812_LED_COUNT) / 100;
        ws2812_led_->Clear();
        
        for (int i = 0; i < led_count; i++) {
            if (volume > 80) {
                ws2812_led_->SetPixel(i, 255, 0, 0); // 红色
            } else if (volume > 40) {
                ws2812_led_->SetPixel(i, 255, 255, 0); // 黄色
            } else {
                ws2812_led_->SetPixel(i, 0, 255, 0); // 绿色
            }
        }
        ws2812_led_->Show();
        
        // 3秒后恢复状态显示
        vTaskDelay(pdMS_TO_TICKS(3000));
        // UpdateStatusDisplay(); // 删除
    }

    virtual void ShowBatteryLevel(int level) override {
        if (!ws2812_led_) return;
        
        if (level < 20) {
            ws2812_led_->SetAllPixels(255, 0, 0); // 红色
        } else {
            ws2812_led_->ShowBatteryLevel(level);
        }
        ws2812_led_->Show();
    }

    virtual void OnChargingStateChanged(bool charging) override {
        ESP_LOGI(TAG, "Charging state changed: %s", charging ? "Charging" : "Not charging");
        if (!ws2812_led_) return;
        
        if (charging) {
            ws2812_led_->SetAllPixels(255, 0, 255); // 紫色
        } else {
            // UpdateStatusDisplay(); // 删除
        }
        ws2812_led_->Show();
    }

    virtual void ShowNetworkStatus(bool connected) override {
        is_connected_ = connected;
        // UpdateStatusDisplay(); // 删除
        
        // 播放网络状态提示音
        if (connected) {
            PlayNotificationSound("wifi_connected");
        } else {
            PlayNotificationSound("wifi_disconnected");
        }
    }

    virtual void ShowDeviceState(const std::string& state) override {
        ESP_LOGI(TAG, "Device state: %s", state.c_str());
        // 可以根据状态显示不同的LED效果
        if (state == "listening") {
            SetLedColor(0, 255, 255); // 青色
        } else if (state == "speaking") {
            SetLedColor(255, 255, 0); // 黄色
        } else if (state == "error") {
            SetLedColor(255, 0, 0); // 红色
        } else {
            // UpdateStatusDisplay(); // 删除
        }
    }

    // 硬件事件回调接口
    virtual void OnNFCCardDetected(const std::string& uid) override {
        ESP_LOGI(TAG, "NFC card detected: %s", uid.c_str());
        current_nfc_uid_ = uid;
        nfc_card_present_ = true;
        
        if (ws2812_led_) {
            ws2812_led_->SetAllPixels(0, 255, 0); // 绿色
            ws2812_led_->Show();
        }
        
        // 通知Application
        Application::GetInstance().OnNFCCardDetected(uid);
    }

    virtual void OnNFCCardRemoved() override {
        ESP_LOGI(TAG, "NFC card removed");
        nfc_card_present_ = false;
        current_nfc_uid_.clear();
        
        // UpdateStatusDisplay(); // 删除
        
        // 通知Application
        Application::GetInstance().OnNFCCardRemoved();
    }

    virtual void OnButtonPressed(const std::string& button_name) override {
        ESP_LOGI(TAG, "Button pressed: %s", button_name.c_str());
        // 可以在这里添加按钮音效或其他反馈
    }

    virtual void OnButtonLongPressed(const std::string& button_name) override {
        ESP_LOGI(TAG, "Button long pressed: %s", button_name.c_str());
        // 可以在这里添加长按反馈
    }

    // 硬件控制接口
    virtual void SetLedColor(int r, int g, int b) override {
        if (ws2812_led_) {
            ws2812_led_->SetAllPixels(r, g, b);
            ws2812_led_->Show();
        }
    }

    virtual void SetLedPattern(const std::string& pattern) override {
        if (!ws2812_led_) return;
        
        if (pattern == "rainbow") {
            ws2812_led_->Rainbow(0, WS2812_LED_COUNT, 50);
        } else if (pattern == "breathing") {
            ws2812_led_->Breathing(255, 255, 255, 50);
        } else if (pattern == "off") {
            ws2812_led_->Clear();
            ws2812_led_->Show();
        }
    }

    virtual void PlayNotificationSound(const std::string& sound_type) override {
        // 这里可以播放不同类型的提示音
        ESP_LOGI(TAG, "Playing notification sound: %s", sound_type.c_str());
    }

    virtual void Vibrate(int duration_ms) override {
        // 这里可以实现震动反馈
        ESP_LOGI(TAG, "Vibrating for %d ms", duration_ms);
    }

    // 硬件状态查询接口
    virtual bool IsNFCCardPresent() const override {
        return nfc_card_present_;
    }

    virtual std::string GetCurrentNFCUID() const override {
        return current_nfc_uid_;
    }

    virtual bool IsCharging() const override {
        return axp2101_ ? axp2101_->IsCharging() : false;
    }

    virtual int GetCurrentVolume() const override {
        auto codec = const_cast<ModoBoard*>(this)->GetAudioCodec();
        return codec ? codec->output_volume() : 50;
    }

    // === 实现音频控制接口 ===
    virtual void EnableAudioInput(bool enable) override {
        auto codec = GetAudioCodec();
        if (codec) {
            codec->EnableInput(enable);
            ESP_LOGI(TAG, "Audio input %s", enable ? "enabled" : "disabled");
        }
    }

    virtual void EnableAudioOutput(bool enable) override {
        auto codec = GetAudioCodec();
        if (codec) {
            codec->EnableOutput(enable);
            ESP_LOGI(TAG, "Audio output %s", enable ? "enabled" : "disabled");
        }
    }

    virtual void StartAudioCodec() override {
        auto codec = GetAudioCodec();
        if (codec) {
            codec->Start();
            ESP_LOGI(TAG, "Audio codec started");
        }
    }

    virtual void StopAudioCodec() override {
        auto codec = GetAudioCodec();
        if (codec) {
            codec->EnableInput(false);
            codec->EnableOutput(false);
            ESP_LOGI(TAG, "Audio codec stopped");
        }
    }

    // === 完整的硬件初始化函数 ===
    void InitializeAllHardware() {
        ESP_LOGI(TAG, "Starting hardware initialization...");
        
        // 1. 初始化I2C总线（音频编解码器）
        InitializeI2c_CODEC();
        
        // 2. 检测ES8311设备是否存在
        // if (i2c_bus_) {
        //     if (!DetectES8311()) {
        //         ESP_LOGI(TAG, "ES8311 not detected, audio functionality disabled");
        //     } else {
        //         ESP_LOGI(TAG, "ES8311 detected successfully");
        //     }
        // }
        
        // // 3. 初始化I2C总线（AXP2101）
        // InitializeI2c_AXP2101();
        
        // // 4. 检测AXP2101设备是否存在
        // if (axp2101_i2c_bus_) {
        //     if (!DetectAXP2101()) {
        //         ESP_LOGI(TAG, "AXP2101 not detected, power management disabled");
        //     } else {
        //         ESP_LOGI(TAG, "AXP2101 detected successfully");
        //     }
        // }
        
        // 5. 初始化按钮
        InitializeButtons();
        ESP_LOGI(TAG, "Buttons initialized");
        
        // 6. 初始化WS2812 LED
        InitializeWS2812();
        ESP_LOGI(TAG, "WS2812 LED initialized");
        
        // 7. 初始化RC522 NFC
        InitializeRC522();
        ESP_LOGI(TAG, "RC522 NFC initialized");
        
        // // 9. 初始化AXP2101电源管理
        // InitializeAXP2101();
        
        // 10. 初始化物联网设备
        // InitializeIot();
        // ESP_LOGI(TAG, "IoT devices initialized");
        
        // // 11. 显示初始化完成状态
        if (ws2812_led_) {
            ws2812_led_->SetAllPixels(0, 255, 0); // 绿色表示初始化完成
            ws2812_led_->Show();
            vTaskDelay(pdMS_TO_TICKS(1000));
            // UpdateStatusDisplay(); // 删除
        }
        
        ESP_LOGI(TAG, "All hardware initialization completed");
        ESP_LOGI(TAG, "Ready for RainMaker provisioning - press BOOT button to start");
    }

    std::string GetBoardJson() override { return "{}"; }
    std::string GetBoardType() override { return "modo-board"; }
    Http* CreateHttp() override { return nullptr; }
    WebSocket* CreateWebSocket() override { return nullptr; }
    Mqtt* CreateMqtt() override { return nullptr; }
    Udp* CreateUdp() override { return nullptr; }
    void StartNetwork() override { /* 可留空 */ }
    const char* GetNetworkStateIcon() override { return nullptr; }
    void SetPowerSaveMode(bool) override { /* 可留空 */ }
};

// 静态成员变量初始化
bool ModoBoard::es8311_available_ = true;  // 初始假设可用，检测后会更新
bool ModoBoard::axp2101_available_ = true; // 初始假设可用，检测后会更新

DECLARE_BOARD(ModoBoard); 