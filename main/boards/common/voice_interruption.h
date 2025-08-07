#ifndef VOICE_INTERRUPTION_H
#define VOICE_INTERRUPTION_H

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>

#include <functional>
#include <atomic>
#include <string>
#include <esp_log.h>
#include <driver/uart.h>
#include <driver/gpio.h>

// UART configuration – modify if your board uses different pins/port
#define VOICE_UART_NUM       UART_NUM_1
#define VOICE_UART_BAUDRATE  9600

class VoiceInterruption {
public:
    VoiceInterruption(gpio_num_t TXD_num, gpio_num_t RXD_num);
    ~VoiceInterruption();

    /* Start FreeRTOS task */
    void start();

    /* Register callback: first parameter is UART byte, second is expected wake word string */
    void OnVoiceDetected(std::function<void(uint8_t)> callback);

    /* Control polling activity */
    void StartDetection();
    void StopDetection();
    bool IsDetectionRunning();

private:
    void VoiceTaskLoop();

private:
    TaskHandle_t voice_task_handle_{nullptr};
    EventGroupHandle_t event_group_{nullptr};

    std::function<void(uint8_t)> voice_detected_callback_;

    bool detected_{false};
};

#endif // VOICE_INTERRUPTION_TASK_H
