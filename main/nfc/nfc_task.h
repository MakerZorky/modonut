#ifndef SPI_TASK_H
#define SPI_TASK_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include <functional>
#include "fm175xx.h"
#include <cstring>
#include "spi.h"

#include <string>
#include <stdlib.h>
#include <stdio.h>

#define SPI_RUNNING_EVENT (1 << 0)

class NfcTask {
public:
    NfcTask();
    ~NfcTask(); 

    // 启动SPI任务
    bool nfcInit();
    void start();

    // 回调函数设置
    void OnReady(std::function<bool(void)> callback);
    void OnNfcStateChange(std::function<void(bool)> callback);
    void OnNfcWakeDetected(std::function<void(const std::string& wake_word)> callback);    // 检测到NFC后的逻辑操作
    void OnNfcDisCon(std::function<void(void)> callback);

    // 事件控制
    void StartDetection();
    void StopDetection();
    bool IsDetectionRunning();

private:
    uint32_t stack_size_ = 8192;
    bool detected_ = false;
    TaskHandle_t nfc_task_handle_;
    EventGroupHandle_t event_group_;

    // 回调函数
    std::function<bool(void)> on_ready_;  // NFC识别回调函数
    std::function<void(bool)> spi_state_change_callback_;
    std::function<void(const std::string& wake_word)> nfc_wake_detected_callback_;
    std::function<void(void)> nfc_disconn_callback_;

    void NfcTaskLoop();
};  

#endif // SPI_TASK_H