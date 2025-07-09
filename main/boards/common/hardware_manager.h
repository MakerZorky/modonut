#ifndef HARDWARE_MANAGER_H
#define HARDWARE_MANAGER_H

#include <string>
#include <functional>
#include <esp_log.h>

class HardwareManager {
public:
    // 硬件事件类型
    enum class EventType {
        BUTTON_PRESS,
        BUTTON_LONG_PRESS,
        NFC_CARD_DETECTED,
        NFC_CARD_REMOVED,
        CHARGING_STATE_CHANGED,
        BATTERY_LOW,
        NETWORK_STATUS_CHANGED,
        VOLUME_CHANGED
    };

    // 硬件事件结构
    struct HardwareEvent {
        EventType type;
        std::string data;
        int value;
        
        HardwareEvent(EventType t, const std::string& d = "", int v = 0) 
            : type(t), data(d), value(v) {}
    };

    // 事件回调函数类型
    using EventCallback = std::function<void(const HardwareEvent&)>;

protected:
    EventCallback event_callback_;
    
    // 发送硬件事件
    void SendEvent(const HardwareEvent& event) {
        if (event_callback_) {
            event_callback_(event);
        }
    }

public:
    virtual ~HardwareManager() = default;
    
    // 设置事件回调
    void SetEventCallback(EventCallback callback) {
        event_callback_ = callback;
    }
    
    // 硬件初始化接口
    virtual bool Initialize() = 0;
    
    // 硬件反初始化接口
    virtual void Deinitialize() = 0;
    
    // 硬件状态查询接口
    virtual bool IsInitialized() const = 0;
    
    // 硬件类型标识
    virtual std::string GetHardwareType() const = 0;
};

#endif // HARDWARE_MANAGER_H 