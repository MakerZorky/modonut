#include "axp2101.h"
#include "board.h"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG "Axp2101"

Axp2101::Axp2101(i2c_master_bus_handle_t i2c_bus, uint8_t addr) 
    : I2cDevice(i2c_bus, addr), monitoring_active_(false), 
      last_charging_state_(false), last_battery_level_(0) {
    // //M5stack配置
    // uint8_t data = ReadReg(0x90);
    // data |= 0b10110100;
    // WriteReg(0x90, data);    //DCDC1/2/3、LDO2/3/4/5 全部开，LDO1 关。
    // WriteReg(0x99, (0b11110 - 5)); //LDO Voltage Setting
    // WriteReg(0x97, (0b11110 - 2));
    WriteReg(0x30, 0b111111);   // ADC Channel Enable
    WriteReg(0x69, 0b00110100); // CHGLED 配置，充电时常亮、充满或放电时熄灭, bit0为使能位
    // WriteReg(0x90, 0xBF);    // LDO使能
    // WriteReg(0x94, 33 - 5);  // Fuel Gauge 参数（电池容量学习常数）
    // WriteReg(0x95, 33 - 5);  // Fuel Gauge 参数


    // ** settings **
    WriteReg(0x12, 0b00000000); // PMU在关机时会断开BATFET，从而将电池与VSYS系统总线物理隔离，实现真正的关机（超低功耗，约40μA）。
    WriteReg(0x15, 0b00000110); // 设置输入电压限制为4.36v
    WriteReg(0x16, 0b00000101); // 设置输入电流限制为2000mA

    WriteReg(0x22, 0b00000110); // 过温保护，按键关机使能并关机
    WriteReg(0x24, 0b00000110); // 将系统电压 (VSYS) 的关机阈值设置为 3.2V,太低会损害电池。 
    WriteReg(0x27, 0b00010010); // 长按4秒关机，1秒开机
    
    WriteReg(0x61, 0b00000101); // 设置预充电电流125mA
    WriteReg(0x62, 0b00001111); // 设置快充阶段的恒流充电电流900ma。( 0x08-200mA, 0x09-300mA, 0x0A-400mA, 0x0B-500mA, 0X0F-900mA)
    WriteReg(0x63, 0b00001101); // 设置充电终止电流125mA
    WriteReg(0x64, 0b00000011); // 设置锂电池的恒压充电终止电压为4.2v

    WriteReg(0x50, 0b00010100); // set TS pin to EXTERNAL input (not temperature)
}

int Axp2101::GetBatteryCurrentDirection() {
    return (ReadReg(0x01) & 0b01100000) >> 5;
}

bool Axp2101::IsCharging() {
    return GetBatteryCurrentDirection() == 1;
}

bool Axp2101::IsDischarging() {
    return GetBatteryCurrentDirection() == 2;
}

bool Axp2101::IsChargingDone() {
    uint8_t value = ReadReg(0x01);
    return (value & 0b00000111) == 0b00000100;
}

bool Axp2101::IsBatteryConnected() {
    uint8_t value = ReadReg(0x01);
    return (value & 0b10000000) != 0;
}

bool Axp2101::IsPowerConnected() {
    uint8_t value = ReadReg(0x00);
    return (value & 0b00000001) != 0;
}

int Axp2101::GetBatteryLevel() {
    return ReadReg(0xA4);
}

float Axp2101::GetBatteryVoltage() {
    uint16_t raw = (ReadReg(0x57) << 4) | (ReadReg(0x56) & 0x0F);
    return raw * 0.26855f; // 转换为毫伏
}

float Axp2101::GetBatteryCurrent() {
    uint16_t raw = (ReadReg(0x59) << 4) | (ReadReg(0x58) & 0x0F);
    return raw * 0.5f; // 转换为毫安
}

float Axp2101::GetBatteryPower() {
    uint16_t raw = (ReadReg(0x5B) << 4) | (ReadReg(0x5A) & 0x0F);
    return raw * 0.5f; // 转换为毫瓦
}

int Axp2101::GetBatteryTemperature() {
    uint8_t raw = ReadReg(0x5C);
    return raw - 144; // 转换为摄氏度
}

void Axp2101::PowerOff() {
    uint8_t value = ReadReg(0x10);
    value = value | 0x01;
    WriteReg(0x10, value);
}

void Axp2101::SetPowerSaveMode(bool enable) {
    uint8_t value = ReadReg(0x12);
    if (enable) {
        value |= 0x01;
    } else {
        value &= ~0x01;
    }
    WriteReg(0x12, value);
}

// --- 充电电流（ICC, 0x62） -----------------------------------------
void Axp2101::SetChargingCurrent(uint16_t mA)
{
    uint8_t n;
    if (mA == 0) { n = 0; }
    else if (mA <= 200) n = std::clamp<uint8_t>((mA + 12) / 25, 1, 8);
    else                n = 8 + std::clamp<uint8_t>((mA - 200 + 50) / 100, 1, 8);
    WriteReg(0x62, n & 0x1F);
}

// --- 充电电压（CV, 0x64） ------------------------------------------
void Axp2101::SetChargingVoltage(uint16_t mV)
{
    mV  = std::clamp<uint16_t>(mV, 4000, 5500);
    uint8_t n = (mV - 4000) / 100;
    WriteReg(0x64, n & 0x0F);
}

void Axp2101::SetLowBatteryCallback(std::function<void(int)> callback) {
    low_battery_callback_ = callback;
}

void Axp2101::SetChargingStateCallback(std::function<void(bool)> callback) {
    charging_state_callback_ = callback;
}

void Axp2101::StartMonitoring() {
    if (!monitoring_active_) {
        monitoring_active_ = true;
        xTaskCreate(MonitoringTaskWrapper, "axp2101_monitor", 4096, this, 1, nullptr);
    }
}

void Axp2101::StopMonitoring() {
    monitoring_active_ = false;
}

void Axp2101::MonitoringTaskWrapper(void* param) {
    Axp2101* axp = static_cast<Axp2101*>(param);
    axp->MonitoringTask();
}

void Axp2101::MonitoringTask() {
    // 创建SingleLed实例，使用GPIO_NUM_6
    SingleLed* BatteryLed = new SingleLed(GPIO_NUM_6);
    
    // 初始状态：绿灯亮2秒表示开机成功
    BatteryLed->SetColor(0, 255, 0);
    BatteryLed->TurnOn();
    vTaskDelay(pdMS_TO_TICKS(2000));

    while (monitoring_active_) {
        bool current_charging = IsCharging();
        int current_level = GetBatteryLevel();
        bool is_charging_done = IsChargingDone();
        // bool is_battery_connected = IsBatteryConnected();
        // bool is_device_off = !is_battery_connected || (!IsCharging() && !IsDischarging());

        // 检查充电状态变化
        if (current_charging != last_charging_state_) {
            last_charging_state_ = current_charging;
            if (charging_state_callback_) {
                charging_state_callback_(current_charging);
            }
            ESP_LOGI(TAG, "Charging state changed: %s", current_charging ? "Charging" : "Not charging");
        }
        
        // 检查电量
        if (current_level != last_battery_level_) {
            last_battery_level_ = current_level;
            ESP_LOGI(TAG, "battery level: %d%%", current_level);
        }
        
        //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        //////////////////////////////////////////////////日志打印/////////////////////////////////////////////////////////////
        //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        
        // if (is_device_off) { // 设备关机状态 - LED不亮
        //     BatteryLed->TurnOff();
        // } else 
        if (current_charging) { // 充电状态
            if (is_charging_done) { // 充满 绿常亮
                BatteryLed->SetColor(0, DEFAULT_BRIGHTNESS, 0);
                BatteryLed->TurnOn();
                ESP_LOGD(TAG, "Charging is done.");
            } else { // 未充满 红常亮
                BatteryLed->SetColor(DEFAULT_BRIGHTNESS, 0, 0);
                BatteryLed->TurnOn();
                ESP_LOGD(TAG, "Charging in progress.");
            }
        } else { // 不充电状态
            if (current_level <= 20) { // 低电量 红闪烁间隔2s
                BatteryLed->SetColor(DEFAULT_BRIGHTNESS, 0, 0);
                BatteryLed->StartContinuousBlink(2000);
                ESP_LOGW(TAG, "Low battery: %d%%", current_level);
            } else { // 正常工作状态 白常亮
                BatteryLed->SetColor(DEFAULT_BRIGHTNESS, DEFAULT_BRIGHTNESS, DEFAULT_BRIGHTNESS);
                BatteryLed->TurnOn();
                ESP_LOGD(TAG, "Normal operation, battery: %d%%", current_level);
            }
        }
        
        //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

        vTaskDelay(pdMS_TO_TICKS(5000)); // 5秒检查一次

    }
    delete BatteryLed; // 清理资源
}
