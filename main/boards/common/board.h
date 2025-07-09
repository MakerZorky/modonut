#ifndef BOARD_H
#define BOARD_H

#include <http.h>
#include <web_socket.h>
#include <mqtt.h>
#include <udp.h>
#include <string>

#include "led/led.h"
#include "backlight.h"
#include <functional>

void* create_board();
class AudioCodec;
class Board {
private:
    Board(const Board&) = delete; // 禁用拷贝构造函数
    Board& operator=(const Board&) = delete; // 禁用赋值操作
    virtual std::string GetBoardJson() = 0;

protected:
    Board();
    std::string GenerateUuid();

    // 软件生成的设备唯一标识
    std::string uuid_;

public:
    static Board& GetInstance() {
        static Board* instance = static_cast<Board*>(create_board());
        return *instance;
    }

    virtual ~Board() = default;
    virtual std::string GetBoardType() = 0;
    virtual std::string GetUuid() { return uuid_; }
    virtual Backlight* GetBacklight() { return nullptr; }
    virtual Led* GetLed();
    virtual AudioCodec* GetAudioCodec() = 0;
    virtual Http* CreateHttp() = 0;
    virtual WebSocket* CreateWebSocket() = 0;
    virtual Mqtt* CreateMqtt() = 0;
    virtual Udp* CreateUdp() = 0;
    virtual void StartNetwork() = 0;
    virtual const char* GetNetworkStateIcon() = 0;
    virtual bool GetBatteryLevel(int &level, bool& charging, bool& discharging);
    virtual std::string GetJson();
    virtual void SetPowerSaveMode(bool enabled) = 0;
    
    // === 新增硬件状态指示接口 ===
    virtual void ShowVolumeIndicator(int volume) { /* 默认实现：无操作 */ }
    virtual void ShowBatteryLevel(int level) { /* 默认实现：无操作 */ }
    virtual void OnChargingStateChanged(bool charging) { /* 默认实现：无操作 */ }
    virtual void ShowNetworkStatus(bool connected) { /* 默认实现：无操作 */ }
    virtual void ShowDeviceState(const std::string& state) { /* 默认实现：无操作 */ }
    
    // === 新增硬件事件回调接口 ===
    virtual void OnNFCCardDetected(const std::string& uid) { /* 默认实现：无操作 */ }
    virtual void OnNFCCardRemoved() { /* 默认实现：无操作 */ }
    virtual void OnButtonPressed(const std::string& button_name) { /* 默认实现：无操作 */ }
    virtual void OnButtonLongPressed(const std::string& button_name) { /* 默认实现：无操作 */ }
    
    // === 新增硬件控制接口 ===
    virtual void SetLedColor(int r, int g, int b) { /* 默认实现：无操作 */ }
    virtual void SetLedPattern(const std::string& pattern) { /* 默认实现：无操作 */ }
    virtual void PlayNotificationSound(const std::string& sound_type) { /* 默认实现：无操作 */ }
    virtual void Vibrate(int duration_ms) { /* 默认实现：无操作 */ }
    
    // === 新增音频控制接口 ===
    virtual void EnableAudioInput(bool enable) { /* 默认实现：无操作 */ }
    virtual void EnableAudioOutput(bool enable) { /* 默认实现：无操作 */ }
    virtual void StartAudioCodec() { /* 默认实现：无操作 */ }
    virtual void StopAudioCodec() { /* 默认实现：无操作 */ }
    
    // === 新增硬件状态查询接口 ===
    virtual bool IsNFCCardPresent() const { return false; }
    virtual std::string GetCurrentNFCUID() const { return ""; }
    virtual bool IsCharging() const { return false; }
    virtual int GetCurrentVolume() const { return 50; }
};

#define DECLARE_BOARD(BOARD_CLASS_NAME) \
void* create_board() { \
    return new BOARD_CLASS_NAME(); \
}

#endif // BOARD_H
