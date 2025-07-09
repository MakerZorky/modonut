# MODO Board

这是一个基于ESP32-S3的智能语音助手开发板，具有以下特性：

## 硬件特性

### 音频系统
- 支持I2S音频输入输出
- 支持单声道和双声道模式
- 音频采样率：输入16kHz，输出24kHz

### 按键功能
- **BOOT按钮**：
  - 短按：切换聊天状态/开始配网
  - 长按：清空NVS配置
- **音量+按钮**：
  - 短按：音量+10
  - 长按：音量设为100%
- **音量-按钮**：
  - 短按：音量-10
  - 长按：音量设为0%

### WS2812 LED指示灯
- 8个RGB LED
- 状态指示：
  - 红色：未连接/错误
  - 蓝色：已连接/待机
  - 绿色：NFC已识别
  - 黄色：配网中
  - 紫色：充电中
- 音量指示：根据音量大小显示不同颜色
- 电量指示：根据电量显示不同颜色

### RC522 NFC模块
- 支持MIFARE卡片检测
- NFC卡片识别后自动开始语音对话
- NFC卡片移除后自动停止对话
- 通过SPI接口连接

### AXP2101电源管理
- 电池电量检测
- 充电状态监控
- 低电量警告
- 电源管理功能

### Rainmaker配网
- 支持ESP Rainmaker配网
- 自动WiFi配置
- 配网状态指示

## 引脚定义

### 音频引脚
```
AUDIO_I2S_MIC_GPIO_WS   GPIO_NUM_4
AUDIO_I2S_MIC_GPIO_SCK  GPIO_NUM_5
AUDIO_I2S_MIC_GPIO_DIN  GPIO_NUM_6
AUDIO_I2S_SPK_GPIO_DOUT GPIO_NUM_7
AUDIO_I2S_SPK_GPIO_BCLK GPIO_NUM_15
AUDIO_I2S_SPK_GPIO_LRCK GPIO_NUM_16
```

### 按键引脚
```
BOOT_BUTTON_GPIO        GPIO_NUM_0
VOLUME_UP_BUTTON_GPIO   GPIO_NUM_40
VOLUME_DOWN_BUTTON_GPIO GPIO_NUM_39
```

### WS2812 LED
```
WS2812_GPIO             GPIO_NUM_48
```

### RC522 NFC
```
RC522_GPIO_MISO         GPIO_NUM_13
RC522_GPIO_MOSI         GPIO_NUM_11
RC522_GPIO_SCLK         GPIO_NUM_12
RC522_GPIO_CS           GPIO_NUM_10
RC522_GPIO_RST          GPIO_NUM_9
```

### AXP2101电源管理
```
AXP2101_GPIO_SDA        GPIO_NUM_41
AXP2101_GPIO_SCL        GPIO_NUM_42
```

### 其他
```
BUILTIN_LED_GPIO        GPIO_NUM_47
```

## 使用方法

### 1. 编译配置
在menuconfig中选择：
```
Board Type -> MODO Board (无屏幕版本)
```

### 2. 配网
1. 首次使用时，设备会自动进入配网模式
2. 用手机连接WiFi热点：`MODO_DEVICE_XXXX`
3. 密码：`12345678`
4. 在配网页面输入WiFi信息
5. 配网成功后LED显示绿色

### 3. NFC使用
1. 将MIFARE卡片靠近RC522模块
2. LED显示绿色表示识别成功
3. 自动开始语音对话
4. 移除卡片后自动停止对话

### 4. 音量调节
- 使用音量+/-按钮调节音量
- LED会显示当前音量大小
- 3秒后恢复状态显示

### 5. 电量监控
- 设备会自动监控电池电量
- 低电量时LED显示红色
- 充电时LED显示紫色

## 注意事项

1. 确保所有硬件连接正确
2. NFC卡片需要是MIFARE类型
3. 配网时需要稳定的网络环境
4. 电池电量低于20%时会发出警告
5. 长按BOOT按钮会清空所有配置

## 故障排除

### 配网失败
- 检查WiFi密码是否正确
- 确保设备在配网范围内
- 重启设备重新配网

### NFC不工作
- 检查RC522模块连接
- 确保卡片类型正确
- 检查SPI配置

### 音频问题
- 检查I2S连接
- 确认音频编解码器配置
- 检查音量设置

### 电源问题
- 检查AXP2101连接
- 确认电池连接
- 检查充电器规格 