#ifndef NFC_TASK_H
#define NFC_TASK_H

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>

#include <mutex>
#include <list>
#include <condition_variable>
#include <atomic>
#include <sstream>
#include <string>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <esp_log.h>

#define PN532_SCK GPIO_NUM_41
#define PN532_SS GPIO_NUM_42
#define PN532_MOSI GPIO_NUM_40
#define PN532_MISO GPIO_NUM_39

#define LOG_EN 0

class NfcTask {
public:
    NfcTask(uint32_t stack_size_ = 4096 * 2);
    ~NfcTask();

    void start();               // 开始NFC线程
    bool nfcInit();             // NFC初始化
    void OnReady(std::function<bool()> callback);
    void OnNfcWakeDetected(std::function<void(const std::string& wake_word)> callback);    // 检测到NFC后的逻辑操作
    void OnNfcStateChange(std::function<void(bool speaking)> callback);                    // Nfc状态指示
    void OnNfcDisCon(std::function<void(void)> callback);

    void StartDetection();      // 设置事件标志组，开始等待NFC唤醒
    void StopDetection();       // 清楚事件标志组，暂停NFC唤醒
    bool IsDetectionRunning();  // NFC是否在工作

private:
    void NfcTaskLoop();     // 线程主函数

private:
    uint32_t stack_size = 0;
    TaskHandle_t nfc_task_handle_ = nullptr;
    EventGroupHandle_t event_group_;    // NFC事件标志组
    bool detected = false;

    std::function<void(const std::string& wake_word)> nfc_wake_detected_callback_;
    std::function<void(bool speaking)> nfc_state_change_callback_;
    std::function<bool(void)> on_ready_ = nullptr;  // NFC识别回调函数
    std::function<void(void)> nfc_disconn_callback_;
};

#endif