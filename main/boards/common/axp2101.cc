#include "axp2101.h"
#include "board.h"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG "Axp2101"

Axp2101::Axp2101(i2c_master_bus_handle_t i2c_bus, uint8_t addr) 
    : I2cDevice(i2c_bus, addr), monitoring_active_(false), 
      last_charging_state_(false), last_battery_level_(0) {
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

void Axp2101::SetChargingCurrent(uint16_t current_ma) {
    uint8_t value = (current_ma / 100) & 0x3F;
    WriteReg(0x61, value);
}

void Axp2101::SetChargingVoltage(uint16_t voltage_mv) {
    uint8_t value = ((voltage_mv - 4000) / 100) & 0x0F;
    WriteReg(0x62, value);
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
        xTaskCreate(MonitoringTaskWrapper, "axp2101_monitor", 4096, this, 5, nullptr);
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
    while (monitoring_active_) {
        bool current_charging = IsCharging();
        int current_level = GetBatteryLevel();
        
        // 检查充电状态变化
        if (current_charging != last_charging_state_) {
            last_charging_state_ = current_charging;
            if (charging_state_callback_) {
                charging_state_callback_(current_charging);
            }
            ESP_LOGI(TAG, "Charging state changed: %s", current_charging ? "Charging" : "Not charging");
        }
        
        // 检查低电量
        if (current_level <= 20 && current_level != last_battery_level_) {
            last_battery_level_ = current_level;
            if (low_battery_callback_) {
                low_battery_callback_(current_level);
            }
            ESP_LOGW(TAG, "Low battery: %d%%", current_level);
        }
        
        vTaskDelay(pdMS_TO_TICKS(5000)); // 5秒检查一次
    }
}
