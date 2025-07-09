#ifndef __WS2812_LED_H__
#define __WS2812_LED_H__

#include <driver/gpio.h>
#include <led_strip.h>
#include <vector>

class Ws2812Led {
public:
    Ws2812Led(gpio_num_t gpio, uint16_t led_count);
    ~Ws2812Led();

    // 设置单个LED颜色
    void SetPixel(uint16_t index, uint8_t red, uint8_t green, uint8_t blue);
    
    // 设置所有LED颜色
    void SetAllPixels(uint8_t red, uint8_t green, uint8_t blue);
    
    // 清除所有LED
    void Clear();
    
    // 更新显示
    void Show();
    
    // 设置亮度
    void SetBrightness(uint8_t brightness);
    
    // 彩虹效果
    void Rainbow(uint16_t start_index, uint16_t count, uint8_t delay_ms);
    
    // 呼吸灯效果
    void Breathing(uint8_t red, uint8_t green, uint8_t blue, uint8_t delay_ms);
    
    // 电量指示（红色=低电量，黄色=中等，绿色=高电量）
    void ShowBatteryLevel(int battery_percent);

private:
    gpio_num_t gpio_;
    uint16_t led_count_;
    uint8_t brightness_;
    led_strip_handle_t led_strip_;
    std::vector<uint8_t> led_data_; // GRB
    
    void InitLedStrip();
};

#endif // __WS2812_LED_H__ 