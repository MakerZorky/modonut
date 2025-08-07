#include "voice_interruption.h"

#define DETECTION_RUNNING_EVENT 1

static const char *TAG = "VoiceInterruption";

VoiceInterruption::VoiceInterruption(gpio_num_t TXD_num, gpio_num_t RXD_num) {
    event_group_ = xEventGroupCreate();

    const uart_config_t uart_config = {
        .baud_rate = VOICE_UART_BAUDRATE,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };

    uart_driver_install(VOICE_UART_NUM, 256, 0, 0, nullptr, 0);
    uart_param_config(VOICE_UART_NUM, &uart_config);
    uart_set_pin(VOICE_UART_NUM, TXD_num, RXD_num, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

VoiceInterruption::~VoiceInterruption() {
    if (voice_task_handle_ != nullptr) {
        vTaskDelete(voice_task_handle_);
    }
    vEventGroupDelete(event_group_);
    uart_driver_delete(VOICE_UART_NUM);
}

void VoiceInterruption::start() {
    xTaskCreate(
        [](void *arg) {
            auto *task = static_cast<VoiceInterruption *>(arg);
            task->VoiceTaskLoop();
        },
        "voice_int_task",
        4096,
        this,
        1,
        &voice_task_handle_);
}

void VoiceInterruption::OnVoiceDetected(std::function<void(uint8_t)> callback) {
    voice_detected_callback_ = std::move(callback);
}

void VoiceInterruption::StartDetection() {
    xEventGroupSetBits(event_group_, DETECTION_RUNNING_EVENT);
}

void VoiceInterruption::StopDetection() {
    xEventGroupClearBits(event_group_, DETECTION_RUNNING_EVENT);
}

bool VoiceInterruption::IsDetectionRunning() {
    return (xEventGroupGetBits(event_group_) & DETECTION_RUNNING_EVENT);
}

void VoiceInterruption::VoiceTaskLoop() {
    char data;
    while (true) {
        // Wait until detection is enabled
        xEventGroupWaitBits(event_group_, DETECTION_RUNNING_EVENT, pdFALSE, pdTRUE, portMAX_DELAY);

        uint8_t raw;
        if (uart_read_bytes(VOICE_UART_NUM, &raw, 1, 20 / portTICK_PERIOD_MS) == 1) {
            ESP_LOGI(TAG, "RX = 0x%02X (%u)", raw, raw);
            if (voice_detected_callback_){
                voice_detected_callback_(raw);
            }
        }
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}
