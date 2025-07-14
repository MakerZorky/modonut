#include "wifi_board.h"
#include "audio_codecs/box_audio_codec.h"
#include "system_reset.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "led/single_led.h"
#include "led/circular_strip.h"
#include "axp2101.h"
#include "power_save_timer.h"
#include "../common/nfc_tasks.h"
#include "assets/lang_config.h"

#include <esp_log.h>
#include <driver/i2c_master.h>
#include <driver/spi_master.h>
#include <driver/gpio.h>
#include <nvs_flash.h>

#define TAG "ModoBoard"

class Pmic : public Axp2101 {
    public:
        // Power Init
        Pmic(i2c_master_bus_handle_t i2c_bus, uint8_t addr) : Axp2101(i2c_bus, addr) {
            uint8_t data = ReadReg(0x90);
            data |= 0b10110100;
            WriteReg(0x90, data);
            WriteReg(0x99, (0b11110 - 5));
            WriteReg(0x97, (0b11110 - 2));
            WriteReg(0x69, 0b00110101);
            WriteReg(0x30, 0b111111);
            WriteReg(0x90, 0xBF);
            WriteReg(0x94, 33 - 5);
            WriteReg(0x95, 33 - 5);
        }
    };

class ModoBoard : public WifiBoard {
private:
    // 硬件组件
    Button boot_button_;
    Button volume_up_button_;
    Button volume_down_button_;
    Axp2101* axp2101_;
    Pmic* pmic_;
    PowerSaveTimer* power_save_timer_;
    
    // I2C总线 - 分离音频编解码器和AXP2101的I2C总线
    i2c_master_bus_handle_t i2c_bus_;           // 音频编解码器使用
    i2c_master_bus_handle_t axp2101_i2c_bus_;   // AXP2101专用

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

    void InitializePowerSaveTimer() {
        power_save_timer_ = new PowerSaveTimer(-1, 60, 300);
        power_save_timer_->OnEnterSleepMode([this]() {
            ESP_LOGI(TAG, "Enabling sleep mode");
        });
        power_save_timer_->OnExitSleepMode([this]() {
        });
        power_save_timer_->OnShutdownRequest([this]() {
            pmic_->PowerOff();
        });
        power_save_timer_->SetEnabled(true);
    }

    void InitializePowerAmplifier() {
        // 配置 GPIO_NUM_15 为输出模式
        gpio_config_t io_conf = {};
        io_conf.intr_type = GPIO_INTR_DISABLE;      // 禁止中断
        io_conf.mode = GPIO_MODE_OUTPUT;            // 设置为输出模式
        io_conf.pin_bit_mask = (1ULL << GPIO_NUM_15); // 选择引脚
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
        gpio_config(&io_conf);

        // 拉高引脚（输出高电平）
        gpio_set_level(GPIO_NUM_15, 1);
    }

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
            ESP_LOGI(TAG, "Click detected, volue + 10");
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
            ESP_LOGI(TAG, "Click detected, volue - 10");
        });

        volume_down_button_.OnLongPress([this]() {
            auto& app = Application::GetInstance();
            // app.ToggleChatState();
            app.WakeWordInvoke("1");
        });
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

    void InitializeAxp2101() {
        ESP_LOGI(TAG, "Init AXP2101");
        pmic_ = new Pmic(i2c_bus_, 0x34);
    }

public:
    ModoBoard() : boot_button_(BOOT_BUTTON_GPIO), 
                  volume_up_button_(VOLUME_UP_BUTTON_GPIO),
                  volume_down_button_(VOLUME_DOWN_BUTTON_GPIO) {
        ESP_LOGI(TAG, "Initializing MODO board...");
        
        InitializeI2c_CODEC();
        InitializePowerAmplifier();
        InitializeButtons();
        // InitializeRC522();
        InitializeAxp2101();
        bool current_charging = pmic_->IsCharging();
        int current_level = pmic_->GetBatteryLevel();
        ESP_LOGI(TAG, "Battery LEVEL: %d%%", current_level);
        ESP_LOGI(TAG, "Charging state changed: %s", current_charging ? "Charging" : "Not charging");
        
        ESP_LOGI(TAG, "MODO board initialization completed");
    }

    ~ModoBoard() {
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

    virtual NfcTask* GetNfc() override {
        static NfcTask nfc_task_;
        return &nfc_task_;
    }

    virtual bool GetBatteryLevel(int &level, bool& charging, bool& discharging) override {
        static bool last_discharging = false;
        charging = pmic_->IsCharging();
        discharging = pmic_->IsDischarging();
        if (discharging != last_discharging) {
            power_save_timer_->SetEnabled(discharging);
            last_discharging = discharging;
        }

        level = pmic_->GetBatteryLevel();
        return true;
    }
};

DECLARE_BOARD(ModoBoard);