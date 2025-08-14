#ifndef __AXP2101_H__
#define __AXP2101_H__

#include "i2c_device.h"
#include <functional>
#include "led/single_led.h"

#define DEFAULT_BRIGHTNESS 8

// 先声明基类
class Axp2101 : public I2cDevice {
public:
    Axp2101(i2c_master_bus_handle_t i2c_bus, uint8_t addr);
    
    // 基本状态检测
    bool IsCharging();
    bool IsDischarging();
    bool IsChargingDone();
    bool IsBatteryConnected();
    bool IsPowerConnected();
    
    // 电量相关
    int GetBatteryLevel();
    float GetBatteryVoltage();
    float GetBatteryCurrent();
    float GetBatteryPower();
    int GetBatteryTemperature();
    
    // 电源管理
    void PowerOff();
    void SetPowerSaveMode(bool enable);
    void SetChargingCurrent(uint16_t current_ma);
    void SetChargingVoltage(uint16_t voltage_mv);
    
    // 回调设置
    void SetLowBatteryCallback(std::function<void(int)> callback);
    void SetChargingStateCallback(std::function<void(bool)> callback);
    
    // 监控任务
    void StartMonitoring();
    void StopMonitoring();

private:
    int GetBatteryCurrentDirection();
    void MonitoringTask();
    static void MonitoringTaskWrapper(void* param);
    
    std::function<void(int)> low_battery_callback_;
    std::function<void(bool)> charging_state_callback_;
    bool monitoring_active_;
    bool last_charging_state_;
    int last_battery_level_;
};

// 后声明派生类
class Pmic : public Axp2101 {
public:
    Pmic(i2c_master_bus_handle_t i2c_bus, uint8_t addr) 
        : Axp2101(i2c_bus, addr) {}
};

#endif
