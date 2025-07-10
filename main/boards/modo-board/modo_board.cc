#include "wifi_board.h"
#include "audio_codecs/box_audio_codec.h"
#include "system_reset.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "led/single_led.h"
#include "led/circular_strip.h"
#include "rc522.h"
#include "axp2101.h"
#include "assets/lang_config.h"

#include <esp_log.h>
#include <driver/i2c_master.h>
#include <driver/spi_master.h>
#include "driver/dac_oneshot.h"
#include <nvs_flash.h>

#define TAG "ModoBoard"

class ModoBoard : public WifiBoard {
private:
    // 硬件组件
    Button boot_button_;
    Button volume_up_button_;
    Button volume_down_button_;
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

    // void InitializeDAC() {
    //     dac_oneshot_handle_t dac1_handle;
    //     dac_oneshot_config_t dac1_config = { .chan_id = DAC_CHAN_1 };
    //     ESP_ERROR_CHECK(dac_oneshot_new_channel(&dac1_config, &dac1_handle));
    //     uint8_t dac_value = static_cast<uint8_t>(2.5/3.3*255);
    //     ESP_ERROR_CHECK(dac_oneshot_output_voltage(dac1_handle, dac_value));
    //     ESP_LOGI("DAC", "Set DAC1(GPIO17) output to ~2.5V (raw=%d)", dac_value);
    // }

    void InitializeButtons() {
        // BOOT按钮：长按清空NVS，短按切换聊天状态或进入配网
        // boot_button_.OnClick([this]() {
        //     OnButtonPressed("boot");
        //     auto& app = Application::GetInstance();
            
        //     // 检查是否已经在配网过程中
        //     if (app.GetDeviceState() == kDeviceStateStarting && !is_connected_) {
        //         // 检查是否已经在配网
        //         static bool provisioning_started = false;
        //         if (!provisioning_started) {
        //             // 启动RainMaker BLE配网
        //             ESP_LOGI(TAG, "Starting RainMaker BLE provisioning...");
        //             provisioning_started = true;
        //             StartNetwork();
        //         } else {
        //             ESP_LOGI(TAG, "Provisioning already started, ignoring button press");
        //         }
        //     } else {
        //         app.ToggleChatState();
        //     }
        // });
        
        // boot_button_.OnLongPress([this]() {
        //     // 通知Application长按事件
        //     OnButtonLongPressed("boot");
            
        //     ESP_LOGI(TAG, "Long press detected, clearing NVS");
        //     ClearNVS();
        // });

        // 音量增加按钮 - 只在音频可用时启用
        volume_up_button_.OnClick([this]() {
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() + 10;
            if (volume > 100) {
                volume = 100;
            }
            codec->SetOutputVolume(volume);
            ShowVolumeIndicator(volume);
        });

        volume_up_button_.OnLongPress([this]() {  
            ESP_LOGI(TAG, "Long press detected, clearing NVS");
            ClearNVS();
        });

        // 音量减少按钮 - 只在音频可用时启用
        volume_down_button_.OnClick([this]() {
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() - 10;
            if (volume < 0) {
                volume = 0;
            }
            codec->SetOutputVolume(volume);
            ShowVolumeIndicator(volume);
        });

        volume_down_button_.OnLongPress([this]() {
            auto& app = Application::GetInstance();
            app.ToggleChatState();
        });
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
        
        // 清除NVS
        esp_err_t ret = nvs_flash_erase();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to erase NVS: %s", esp_err_to_name(ret));
            vTaskDelay(pdMS_TO_TICKS(2000));
            return;
        }
        ESP_LOGI(TAG, "NVS cleared and reinitialized successfully");
    
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
        
        InitializeI2c_CODEC();
        //InitializeDAC();
        InitializeButtons();
        ESP_LOGI(TAG, "Buttons initialized");
        InitializeRC522();
        ESP_LOGI(TAG, "RC522 NFC initialized");
        // InitializeI2c_AXP2101();
        // InitializeAXP2101();
        
        ESP_LOGI(TAG, "MODO board initialization completed");
    }

    ~ModoBoard() {
        if (rc522_) delete rc522_;
        if (axp2101_) delete axp2101_;
        if (i2c_bus_) i2c_del_master_bus(i2c_bus_);
        if (axp2101_i2c_bus_) i2c_del_master_bus(axp2101_i2c_bus_);
    }

    virtual Led* GetLed() override {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    virtual Led* GetCircularStrip() override{
        static CircularStrip strip(WS2812_GPIO, WS2812_LED_COUNT);
        return &strip;
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
    
    // 硬件状态指示接口
    // virtual void ShowBatteryLevel(int level) override {
    //     if (!ws2812_led_) return;
        
    //     if (level < 20) {
    //         ws2812_led_->SetAllPixels(255, 0, 0); // 红色
    //     } else {
    //         ws2812_led_->ShowBatteryLevel(level);
    //     }
    //     ws2812_led_->Show();
    // }

    // virtual void OnChargingStateChanged(bool charging) override {
    //     ESP_LOGI(TAG, "Charging state changed: %s", charging ? "Charging" : "Not charging");
    //     if (!ws2812_led_) return;
        
    //     if (charging) {
    //         ws2812_led_->SetAllPixels(255, 0, 255); // 紫色
    //     } else {
    //         // UpdateStatusDisplay(); // 删除
    //     }
    //     ws2812_led_->Show();
    // }

    // 硬件事件回调接口
    virtual void OnNFCCardDetected(const std::string& uid) override {
        ESP_LOGI(TAG, "NFC card detected: %s", uid.c_str());
        current_nfc_uid_ = uid;
        nfc_card_present_ = true;
        
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
};

// 静态成员变量初始化
bool ModoBoard::es8311_available_ = true;  // 初始假设可用，检测后会更新
bool ModoBoard::axp2101_available_ = true; // 初始假设可用，检测后会更新

DECLARE_BOARD(ModoBoard); 