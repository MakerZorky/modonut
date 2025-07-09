#include "ws2812_led.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG "Ws2812Led"

Ws2812Led::Ws2812Led(gpio_num_t gpio, uint16_t led_count)
    : gpio_(gpio), led_count_(led_count), brightness_(255), led_strip_(nullptr) {
    led_data_.resize(led_count_ * 3);
    InitLedStrip();
    Clear();
    Show();
}

Ws2812Led::~Ws2812Led() {
    if (led_strip_) {
        led_strip_del(led_strip_);
    }
}

void Ws2812Led::InitLedStrip() {
    led_strip_config_t strip_config = {
        .strip_gpio_num = gpio_,
        .max_leds = led_count_,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        .led_model = LED_MODEL_WS2812,
        .flags = { .invert_out = false }
    };
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .mem_block_symbols = 64,
        .flags = { .with_dma = false }
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip_));
}

void Ws2812Led::SetPixel(uint16_t index, uint8_t red, uint8_t green, uint8_t blue) {
    if (index >= led_count_) return;
    // WS2812使用GRB顺序
    uint16_t pixel_index = index * 3;
    led_data_[pixel_index] = (green * brightness_) / 255;
    led_data_[pixel_index + 1] = (red * brightness_) / 255;
    led_data_[pixel_index + 2] = (blue * brightness_) / 255;
    led_strip_set_pixel(led_strip_, index, led_data_[pixel_index + 1], led_data_[pixel_index], led_data_[pixel_index + 2]);
}

void Ws2812Led::SetAllPixels(uint8_t red, uint8_t green, uint8_t blue) {
    for (uint16_t i = 0; i < led_count_; i++) {
        SetPixel(i, red, green, blue);
    }
}

void Ws2812Led::Clear() {
    SetAllPixels(0, 0, 0);
}

void Ws2812Led::Show() {
    ESP_ERROR_CHECK(led_strip_refresh(led_strip_));
}

void Ws2812Led::SetBrightness(uint8_t brightness) {
    brightness_ = brightness;
}

void Ws2812Led::Rainbow(uint16_t start_index, uint16_t count, uint8_t delay_ms) {
    static uint8_t hue = 0;
    for (uint16_t i = 0; i < count; i++) {
        uint16_t index = (start_index + i) % led_count_;
        uint8_t hue_val = (hue + i * 256 / count) % 256;
        uint8_t sector = hue_val / 43;
        uint8_t f = (hue_val % 43) * 6;
        uint8_t p = 0;
        uint8_t q = 255 - f;
        uint8_t t = f;
        uint8_t r, g, b;
        switch (sector) {
            case 0: r = 255; g = t; b = p; break;
            case 1: r = q; g = 255; b = p; break;
            case 2: r = p; g = 255; b = t; break;
            case 3: r = p; g = q; b = 255; break;
            case 4: r = t; g = p; b = 255; break;
            default: r = 255; g = p; b = q; break;
        }
        SetPixel(index, r, g, b);
    }
    Show();
    hue++;
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
}

void Ws2812Led::Breathing(uint8_t red, uint8_t green, uint8_t blue, uint8_t delay_ms) {
    static uint8_t brightness = 0;
    static bool increasing = true;
    if (increasing) {
        brightness++;
        if (brightness >= 255) {
            brightness = 255;
            increasing = false;
        }
    } else {
        brightness--;
        if (brightness == 0) {
            brightness = 0;
            increasing = true;
        }
    }
    uint8_t r = (red * brightness) / 255;
    uint8_t g = (green * brightness) / 255;
    uint8_t b = (blue * brightness) / 255;
    SetAllPixels(r, g, b);
    Show();
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
}

void Ws2812Led::ShowBatteryLevel(int battery_percent) {
    Clear();
    if (battery_percent <= 20) {
        SetAllPixels(255, 0, 0);
    } else if (battery_percent <= 50) {
        SetAllPixels(255, 255, 0);
    } else {
        SetAllPixels(0, 255, 0);
    }
    Show();
} 