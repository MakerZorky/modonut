# Board 接口使用指南

## 概述

Board 接口是小智 AI 聊天机器人项目的硬件抽象层，提供了统一的硬件访问接口。通过这个接口，Application 类可以访问各种硬件功能，而不需要直接管理硬件对象。

## 接口分类

### 1. 硬件状态指示接口

这些接口用于显示设备的各种状态：

```cpp
// 显示音量指示
Board::GetInstance().ShowVolumeIndicator(50);

// 显示电量指示
Board::GetInstance().ShowBatteryLevel(80);

// 显示充电状态
Board::GetInstance().OnChargingStateChanged(true);

// 显示网络状态
Board::GetInstance().ShowNetworkStatus(true);

// 显示设备状态
Board::GetInstance().ShowDeviceState("listening");
```

### 2. 硬件事件回调接口

这些接口用于处理硬件事件：

```cpp
// NFC 卡片检测
Board::GetInstance().OnNFCCardDetected("12345678");

// NFC 卡片移除
Board::GetInstance().OnNFCCardRemoved();

// 按钮事件
Board::GetInstance().OnButtonPressed("boot");
Board::GetInstance().OnButtonLongPressed("volume_up");
```

### 3. 硬件控制接口

这些接口用于控制硬件：

```cpp
// 设置LED颜色
Board::GetInstance().SetLedColor(255, 0, 0); // 红色

// 设置LED模式
Board::GetInstance().SetLedPattern("rainbow");
Board::GetInstance().SetLedPattern("breathing");
Board::GetInstance().SetLedPattern("off");

// 播放提示音
Board::GetInstance().PlayNotificationSound("success");
Board::GetInstance().PlayNotificationSound("error");

// 震动反馈
Board::GetInstance().Vibrate(100); // 震动100ms
```

### 4. 硬件状态查询接口

这些接口用于查询硬件状态：

```cpp
// 查询NFC状态
bool has_card = Board::GetInstance().IsNFCCardPresent();
std::string uid = Board::GetInstance().GetCurrentNFCUID();

// 查询充电状态
bool charging = Board::GetInstance().IsCharging();

// 查询音量
int volume = Board::GetInstance().GetCurrentVolume();
```

## 在 Application 中使用

### 状态变化通知

在 Application 类中，当设备状态发生变化时，会自动通知 Board 接口：

```cpp
void Application::SetDeviceState(DeviceState state) {
    // ... 其他代码 ...
    
    // 通知Board接口设备状态变化
    OnDeviceStateChanged(state);
    
    // ... 其他代码 ...
}
```

### 硬件事件处理

Application 类提供了硬件事件的处理方法：

```cpp
// NFC 事件
void Application::OnNFCCardDetected(const std::string& uid);
void Application::OnNFCCardRemoved();

// 设备状态变化
void Application::OnDeviceStateChanged(DeviceState new_state);
void Application::OnNetworkStatusChanged(bool connected);
void Application::OnButtonEvent(const std::string& button_name, bool is_long_press);
```

## 板级实现

每个板级实现都需要继承 Board 类并实现相应的接口。以 MODO Board 为例：

```cpp
class ModoBoard : public WifiBoard {
private:
    Ws2812Led* ws2812_led_;
    Rc522* rc522_;
    // ... 其他硬件对象 ...

public:
    // 实现硬件状态指示接口
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
    }
    
    // 实现硬件事件回调接口
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
    
    // ... 实现其他接口 ...
};
```

## 最佳实践

### 1. 职责分离

- **Application 类**：专注于应用逻辑和状态管理
- **Board 接口**：提供硬件抽象和统一访问
- **板级实现**：处理具体的硬件操作

### 2. 错误处理

在硬件操作中应该包含适当的错误处理：

```cpp
virtual void ShowVolumeIndicator(int volume) override {
    if (!ws2812_led_) {
        ESP_LOGW(TAG, "WS2812 LED not available");
        return;
    }
    
    // ... 实现代码 ...
}
```

### 3. 状态同步

确保硬件状态与软件状态保持同步：

```cpp
virtual void OnNFCCardDetected(const std::string& uid) override {
    current_nfc_uid_ = uid;
    nfc_card_present_ = true;
    
    // 更新硬件显示
    if (ws2812_led_) {
        ws2812_led_->SetAllPixels(0, 255, 0);
        ws2812_led_->Show();
    }
    
    // 通知应用层
    Application::GetInstance().OnNFCCardDetected(uid);
}
```

### 4. 性能考虑

避免在硬件操作中使用阻塞调用：

```cpp
// 好的做法：异步处理
virtual void ShowVolumeIndicator(int volume) override {
    // 立即更新显示
    UpdateVolumeDisplay(volume);
    
    // 异步恢复状态
    Schedule([this]() {
        UpdateStatusDisplay();
    });
}

// 避免的做法：阻塞调用
virtual void ShowVolumeIndicator(int volume) override {
    UpdateVolumeDisplay(volume);
    vTaskDelay(pdMS_TO_TICKS(3000)); // 阻塞3秒
    UpdateStatusDisplay();
}
```

## 扩展接口

如果需要添加新的硬件功能，可以按照以下步骤：

1. 在 `board.h` 中添加新的虚函数
2. 提供默认实现（避免破坏现有代码）
3. 在具体的板级实现中重写该方法
4. 在 Application 类中添加相应的调用

```cpp
// 在 board.h 中添加
virtual void NewHardwareFeature() { /* 默认实现 */ }

// 在板级实现中重写
virtual void NewHardwareFeature() override {
    // 具体实现
}

// 在 Application 中调用
Board::GetInstance().NewHardwareFeature();
```

这样的设计确保了代码的可维护性和扩展性。 