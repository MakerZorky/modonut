# 硬件实现指南

## 概述

本指南说明如何在板级代码中实现硬件功能，确保所有硬件初始化在板级代码中完成，并通过回调函数通知 Application。

## 架构设计

### 1. 硬件管理器架构

```
Application (应用层)
    ↓
Board Interface (板级接口)
    ↓
Hardware Manager (硬件管理器)
    ↓
Hardware Components (硬件组件)
```

### 2. 事件流

```
硬件事件 → 硬件组件 → 硬件管理器 → Board接口 → Application
```

## 实现步骤

### 1. 创建硬件管理器

#### 1.1 继承 HardwareManager 基类

```cpp
class ModoHardwareManager : public HardwareManager {
private:
    // 硬件组件
    Button* boot_button_;
    Button* volume_up_button_;
    Button* volume_down_button_;
    Ws2812Led* ws2812_led_;
    Rc522* rc522_;
    Axp2101* axp2101_;
    
    // 状态变量
    bool nfc_card_present_;
    std::string current_nfc_uid_;
    bool is_connected_;
    bool is_initialized_;
    
public:
    ModoHardwareManager();
    virtual ~ModoHardwareManager();
    
    // 实现硬件管理器接口
    virtual bool Initialize() override;
    virtual void Deinitialize() override;
    virtual bool IsInitialized() const override;
    virtual std::string GetHardwareType() const override;
};
```

#### 1.2 实现初始化函数

```cpp
bool ModoHardwareManager::Initialize() {
    ESP_LOGI(TAG, "Initializing MODO hardware manager...");
    
    // 1. 初始化I2C总线
    if (!InitializeI2C()) {
        ESP_LOGE(TAG, "Failed to initialize I2C");
        return false;
    }
    
    // 2. 初始化按钮
    if (!InitializeButtons()) {
        ESP_LOGE(TAG, "Failed to initialize buttons");
        return false;
    }
    
    // 3. 初始化WS2812 LED
    if (!InitializeWS2812()) {
        ESP_LOGE(TAG, "Failed to initialize WS2812 LED");
        return false;
    }
    
    // 4. 初始化RC522 NFC
    if (!InitializeRC522()) {
        ESP_LOGE(TAG, "Failed to initialize RC522 NFC");
        return false;
    }
    
    // 5. 显示初始化完成状态
    if (ws2812_led_) {
        ws2812_led_->SetAllPixels(0, 255, 0); // 绿色表示初始化完成
        ws2812_led_->Show();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    
    is_initialized_ = true;
    ESP_LOGI(TAG, "MODO hardware manager initialized successfully");
    return true;
}
```

### 2. 实现硬件事件处理

#### 2.1 按钮事件处理

```cpp
bool ModoHardwareManager::InitializeButtons() {
    // 创建按钮对象
    boot_button_ = new Button(BOOT_BUTTON_GPIO);
    volume_up_button_ = new Button(VOLUME_UP_BUTTON_GPIO);
    volume_down_button_ = new Button(VOLUME_DOWN_BUTTON_GPIO);
    
    // 设置BOOT按钮回调
    boot_button_->OnClick([this]() {
        OnButtonEvent("boot", false);
    });
    
    boot_button_->OnLongPress([this]() {
        OnButtonEvent("boot", true);
        // 长按清空NVS
        ESP_LOGI(TAG, "Long press detected, clearing NVS");
        nvs_flash_erase();
        esp_restart();
    });
    
    // 设置音量按钮回调
    volume_up_button_->OnClick([this]() {
        OnButtonEvent("volume_up", false);
    });
    
    volume_down_button_->OnClick([this]() {
        OnButtonEvent("volume_down", false);
    });
    
    ESP_LOGI(TAG, "Buttons initialized successfully");
    return true;
}

void ModoHardwareManager::OnButtonEvent(const std::string& button_name, bool is_long_press) {
    ESP_LOGI(TAG, "Button event: %s %s", button_name.c_str(), is_long_press ? "(long press)" : "(press)");
    
    // 发送硬件事件
    EventType event_type = is_long_press ? EventType::BUTTON_LONG_PRESS : EventType::BUTTON_PRESS;
    SendEvent(HardwareEvent(event_type, button_name));
    
    // 通知Application
    Application::GetInstance().OnButtonEvent(button_name, is_long_press);
}
```

#### 2.2 NFC事件处理

```cpp
bool ModoHardwareManager::InitializeRC522() {
    rc522_ = new Rc522(RC522_SPI_HOST, RC522_GPIO_MISO, RC522_GPIO_MOSI, 
                      RC522_GPIO_SCLK, RC522_GPIO_CS, RC522_GPIO_RST);
    
    if (!rc522_->Init()) {
        ESP_LOGE(TAG, "Failed to initialize RC522");
        return false;
    }
    
    // 设置NFC卡片检测回调
    rc522_->SetCardDetectedCallback([this](const std::string& uid) {
        OnNFCEvent(uid, true);
    });
    
    rc522_->SetCardRemovedCallback([this]() {
        OnNFCEvent("", false);
    });
    
    rc522_->StartCardDetection();
    
    ESP_LOGI(TAG, "RC522 NFC initialized successfully");
    return true;
}

void ModoHardwareManager::OnNFCEvent(const std::string& uid, bool detected) {
    if (detected) {
        ESP_LOGI(TAG, "NFC card detected: %s", uid.c_str());
        current_nfc_uid_ = uid;
        nfc_card_present_ = true;
        
        // 显示绿色表示NFC已识别
        if (ws2812_led_) {
            ws2812_led_->SetAllPixels(0, 255, 0);
            ws2812_led_->Show();
        }
        
        // 发送硬件事件
        SendEvent(HardwareEvent(EventType::NFC_CARD_DETECTED, uid));
        
        // 通知Application
        Application::GetInstance().OnNFCCardDetected(uid);
    } else {
        ESP_LOGI(TAG, "NFC card removed");
        nfc_card_present_ = false;
        current_nfc_uid_.clear();
        
        // 发送硬件事件
        SendEvent(HardwareEvent(EventType::NFC_CARD_REMOVED));
        
        // 通知Application
        Application::GetInstance().OnNFCCardRemoved();
    }
}
```

#### 2.3 电源管理事件处理

```cpp
bool ModoHardwareManager::InitializeAXP2101() {
    if (!i2c_bus_) {
        ESP_LOGE(TAG, "I2C bus not initialized, cannot initialize AXP2101");
        return false;
    }
    
    axp2101_ = new Axp2101(i2c_bus_, AXP2101_I2C_ADDR);
    
    // 设置低电量回调
    axp2101_->SetLowBatteryCallback([this](int level) {
        OnBatteryEvent(level);
    });
    
    // 设置充电状态回调
    axp2101_->SetChargingStateCallback([this](bool charging) {
        OnChargingEvent(charging);
    });
    
    // 开始监控
    axp2101_->StartMonitoring();
    
    ESP_LOGI(TAG, "AXP2101 power management initialized successfully");
    return true;
}

void ModoHardwareManager::OnChargingEvent(bool charging) {
    ESP_LOGI(TAG, "Charging state changed: %s", charging ? "Charging" : "Not charging");
    
    if (charging) {
        // 充电时显示紫色
        if (ws2812_led_) {
            ws2812_led_->SetAllPixels(255, 0, 255);
            ws2812_led_->Show();
        }
        PlayNotificationSound("charging_start");
    } else {
        PlayNotificationSound("charging_stop");
    }
    
    // 发送硬件事件
    SendEvent(HardwareEvent(EventType::CHARGING_STATE_CHANGED, "", charging ? 1 : 0));
    
    // 通知Application
    Application::GetInstance().OnChargingStateChanged(charging);
}
```

### 3. 在Board类中集成硬件管理器

#### 3.1 修改Board类

```cpp
class ModoBoard : public WifiBoard {
private:
    ModoHardwareManager* hardware_manager_;
    
public:
    ModoBoard() {
        // 创建硬件管理器
        hardware_manager_ = new ModoHardwareManager();
        
        // 设置事件回调
        hardware_manager_->SetEventCallback([this](const HardwareEvent& event) {
            HandleHardwareEvent(event);
        });
        
        // 初始化硬件
        if (!hardware_manager_->Initialize()) {
            ESP_LOGE(TAG, "Failed to initialize hardware manager");
        }
        
        ESP_LOGI(TAG, "ModoBoard initialized successfully");
    }
    
    ~ModoBoard() {
        if (hardware_manager_) {
            delete hardware_manager_;
        }
    }
    
    // 处理硬件事件
    void HandleHardwareEvent(const HardwareEvent& event) {
        switch (event.type) {
            case HardwareManager::EventType::BUTTON_PRESS:
                ESP_LOGI(TAG, "Button pressed: %s", event.data.c_str());
                break;
            case HardwareManager::EventType::NFC_CARD_DETECTED:
                ESP_LOGI(TAG, "NFC card detected: %s", event.data.c_str());
                break;
            case HardwareManager::EventType::CHARGING_STATE_CHANGED:
                ESP_LOGI(TAG, "Charging state: %s", event.value ? "Charging" : "Not charging");
                break;
            default:
                break;
        }
    }
    
    // 实现Board接口
    virtual void ShowVolumeIndicator(int volume) override {
        if (hardware_manager_) {
            hardware_manager_->ShowVolumeIndicator(volume);
        }
    }
    
    virtual void OnNFCCardDetected(const std::string& uid) override {
        if (hardware_manager_) {
            // 硬件管理器已经处理了NFC事件，这里只需要通知Application
            Application::GetInstance().OnNFCCardDetected(uid);
        }
    }
    
    // ... 实现其他Board接口方法
};
```

### 4. 硬件状态管理

#### 4.1 状态同步

```cpp
void ModoHardwareManager::UpdateStatusDisplay() {
    if (!ws2812_led_) return;
    
    if (nfc_card_present_) {
        ws2812_led_->SetAllPixels(0, 255, 0); // 绿色表示NFC已识别
    } else if (is_connected_) {
        ws2812_led_->SetAllPixels(0, 0, 255); // 蓝色表示已连接
    } else {
        ws2812_led_->SetAllPixels(255, 0, 0); // 红色表示未连接
    }
    ws2812_led_->Show();
}
```

#### 4.2 网络状态处理

```cpp
void ModoHardwareManager::SetNetworkStatus(bool connected) {
    is_connected_ = connected;
    OnNetworkEvent(connected);
}

void ModoHardwareManager::OnNetworkEvent(bool connected) {
    ESP_LOGI(TAG, "Network status changed: %s", connected ? "Connected" : "Disconnected");
    
    // 播放网络状态提示音
    if (connected) {
        PlayNotificationSound("wifi_connected");
    } else {
        PlayNotificationSound("wifi_disconnected");
    }
    
    // 发送硬件事件
    SendEvent(HardwareEvent(EventType::NETWORK_STATUS_CHANGED, "", connected ? 1 : 0));
    
    // 通知Application
    Application::GetInstance().OnNetworkStatusChanged(connected);
}
```

## 最佳实践

### 1. 错误处理

```cpp
bool ModoHardwareManager::Initialize() {
    ESP_LOGI(TAG, "Initializing MODO hardware manager...");
    
    // 使用RAII模式管理资源
    std::unique_ptr<Button> temp_boot_button;
    std::unique_ptr<Ws2812Led> temp_ws2812_led;
    
    try {
        // 初始化各个组件
        if (!InitializeI2C()) {
            throw std::runtime_error("Failed to initialize I2C");
        }
        
        if (!InitializeButtons()) {
            throw std::runtime_error("Failed to initialize buttons");
        }
        
        if (!InitializeWS2812()) {
            throw std::runtime_error("Failed to initialize WS2812 LED");
        }
        
        // 如果所有初始化都成功，转移所有权
        boot_button_ = temp_boot_button.release();
        ws2812_led_ = temp_ws2812_led.release();
        
        is_initialized_ = true;
        ESP_LOGI(TAG, "MODO hardware manager initialized successfully");
        return true;
        
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "Initialization failed: %s", e.what());
        Deinitialize();
        return false;
    }
}
```

### 2. 资源管理

```cpp
void ModoHardwareManager::Deinitialize() {
    ESP_LOGI(TAG, "Deinitializing MODO hardware manager...");
    
    // 按相反顺序释放资源
    if (rc522_) {
        delete rc522_;
        rc522_ = nullptr;
    }
    
    if (ws2812_led_) {
        delete ws2812_led_;
        ws2812_led_ = nullptr;
    }
    
    if (boot_button_) {
        delete boot_button_;
        boot_button_ = nullptr;
    }
    
    if (i2c_bus_) {
        i2c_del_master_bus(i2c_bus_);
        i2c_bus_ = nullptr;
    }
    
    is_initialized_ = false;
    ESP_LOGI(TAG, "MODO hardware manager deinitialized");
}
```

### 3. 事件处理

```cpp
void ModoHardwareManager::OnButtonEvent(const std::string& button_name, bool is_long_press) {
    // 1. 记录日志
    ESP_LOGI(TAG, "Button event: %s %s", button_name.c_str(), is_long_press ? "(long press)" : "(press)");
    
    // 2. 发送硬件事件
    EventType event_type = is_long_press ? EventType::BUTTON_LONG_PRESS : EventType::BUTTON_PRESS;
    SendEvent(HardwareEvent(event_type, button_name));
    
    // 3. 通知Application
    Application::GetInstance().OnButtonEvent(button_name, is_long_press);
    
    // 4. 执行硬件特定的操作
    if (button_name == "volume_up") {
        // 处理音量增加
        HandleVolumeUp();
    } else if (button_name == "volume_down") {
        // 处理音量减少
        HandleVolumeDown();
    }
}
```

## 调试和测试

### 1. 日志输出

```cpp
#define TAG "ModoHardwareManager"

// 在关键位置添加日志
ESP_LOGI(TAG, "Initializing MODO hardware manager...");
ESP_LOGI(TAG, "I2C bus initialized successfully");
ESP_LOGI(TAG, "Buttons initialized successfully");
ESP_LOGI(TAG, "WS2812 LED initialized successfully");
ESP_LOGI(TAG, "RC522 NFC initialized successfully");
ESP_LOGI(TAG, "MODO hardware manager initialized successfully");
```

### 2. 状态检查

```cpp
bool ModoHardwareManager::IsInitialized() const {
    return is_initialized_ && 
           boot_button_ != nullptr && 
           ws2812_led_ != nullptr && 
           rc522_ != nullptr;
}
```

### 3. 错误恢复

```cpp
void ModoHardwareManager::HandleInitializationError(const std::string& component) {
    ESP_LOGE(TAG, "Failed to initialize %s", component.c_str());
    
    // 尝试重新初始化
    if (component == "I2C") {
        vTaskDelay(pdMS_TO_TICKS(1000));
        if (InitializeI2C()) {
            ESP_LOGI(TAG, "I2C reinitialized successfully");
        }
    }
}
```

## 总结

通过这种架构设计，我们实现了：

1. **职责分离**：硬件管理器和Application各司其职
2. **事件驱动**：所有硬件事件通过回调函数通知Application
3. **资源管理**：统一的资源初始化和释放
4. **错误处理**：完善的错误处理和恢复机制
5. **可扩展性**：易于添加新的硬件组件和功能

这种设计使得代码更加模块化、可维护，并且为未来的硬件扩展提供了良好的基础。 