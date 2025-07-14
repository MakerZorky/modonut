#include "nfc_tasks.h"

extern "C" {
#include "pn532.h"
}

static pn532_t nfc;
uint8_t success;
uint8_t uid[4] = {0, 0, 0, 0}; // Buffer to store the returned UID
uint8_t uidLength = 4;                     // Length of the UID (4 or 7 bytes depending on ISO14443A card type)

#define DETECTION_RUNNING_EVENT 1

#define TAG "NfcTask"

NfcTask::NfcTask(uint32_t stack_size_){
    stack_size = stack_size_;
    event_group_ = xEventGroupCreate();
}

NfcTask::~NfcTask(){
    if (nfc_task_handle_ != nullptr) {
        vTaskDelete(nfc_task_handle_);
    }
    vEventGroupDelete(event_group_);
}

void NfcTask::start(){
    xTaskCreate([](void* arg) {
        NfcTask* task = (NfcTask*)arg;
        task->NfcTaskLoop();
    }, "nfc_task", stack_size, this, 1, &nfc_task_handle_);
}

bool NfcTask::nfcInit(){
    pn532_spi_init(&nfc, PN532_SCK, PN532_MISO, PN532_MOSI, PN532_SS);
    pn532_begin(&nfc);
    uint32_t versiondata = pn532_getFirmwareVersion(&nfc);
    if (!versiondata)
    {
        ESP_LOGW(TAG, "Didn't find PN53x board");
        while (1)
        {
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
        return false;
    }
    ESP_LOGI(TAG, "Found chip PN5 %lx", (versiondata >> 24) & 0xFF);
    ESP_LOGI(TAG, "Firmware ver. %lu.%lu", (versiondata >> 16) & 0xFF, (versiondata >> 8) & 0xFF);
    pn532_SAMConfig(&nfc);
    //ESP_LOGI(TAG, "Waiting for an ISO14443A Card ...");
    return true;
}

void NfcTask::OnReady(std::function<bool()> callback){
    on_ready_ = callback;
}

void NfcTask::OnNfcWakeDetected(std::function<void(const std::string& wake_word)> callback){
    nfc_wake_detected_callback_ = callback;
}

void NfcTask::OnNfcStateChange(std::function<void(bool speaking)> callback){
    nfc_state_change_callback_ = callback;
}

void NfcTask::OnNfcDisCon(std::function<void(void)> callback){
    nfc_disconn_callback_ = callback;
}

void NfcTask::StartDetection(){
    xEventGroupSetBits(event_group_, DETECTION_RUNNING_EVENT);
}

void NfcTask::StopDetection(){
    xEventGroupClearBits(event_group_, DETECTION_RUNNING_EVENT);
}

bool NfcTask::IsDetectionRunning(){
    return xEventGroupGetBits(event_group_) & DETECTION_RUNNING_EVENT;
}

void NfcTask::NfcTaskLoop(){
    while (1)
    {
        xEventGroupWaitBits(event_group_, DETECTION_RUNNING_EVENT, pdFALSE, pdTRUE, portMAX_DELAY);
        success = pn532_readPassiveTargetID(&nfc, PN532_MIFARE_ISO14443A, uid, &uidLength, 100);

        if (success)
        {
            // Display some basic information about the card
            //ESP_LOGI(TAG, "Found an ISO14443A card");
            //ESP_LOGI(TAG, "UID Length: %d bytes", uidLength);
            //ESP_LOGI(TAG, "UID Value:");
            //esp_log_buffer_hexdump_internal(TAG, uid, uidLength, ESP_LOG_INFO);      
        }
        else
        {
            //ESP_LOGI(TAG, "Timed out waiting for a card");
        }

#if LOG_EN
        ESP_LOGI(TAG, "SUCCESS: %d", success); 
#endif

        if (success && !detected)
        {
            detected = true;
#if LOG_EN
            ESP_LOGI(TAG, "Found an ISO14443A card"); 
#endif
            if(nfc_state_change_callback_){
                nfc_state_change_callback_(true);
            }

            if (nfc_wake_detected_callback_) {
                ESP_LOGI(TAG, "nfc_wake_detected_callback_"); 
                // 认证区块
                uint8_t keyData[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}; // 默认密钥A
                if (pn532_mifareclassic_AuthenticateBlock(&nfc, uid, uidLength, 2, 0, keyData) == 1) {
                    ESP_LOGI(TAG, "Authenticate success");

                    uint8_t data[16] = {0};
                    // data[15] = 107;  
                    // // 写入区块
                    // if (pn532_mifareclassic_WriteDataBlock(&nfc, 2, data) == 1) {
                    //     ESP_LOGI(TAG, "WriteDataBlock = %d" ,data[15]);
                    //     ESP_LOGI(TAG, "WriteDataBlock success");
                    // // 成功写入
                    // }
                    // 读取区块
                    if (pn532_mifareclassic_ReadDataBlock(&nfc, 2, data) == 1) {
                        std::string dataStr = std::to_string(data[15]);
                        nfc_wake_detected_callback_(dataStr);
                        ESP_LOGI(TAG, "NFC_%d_ID detected",data[15]);
                    } 
                } else {
                    ESP_LOGE(TAG, "Authentication failed");
                }
            }     
        }
        else if(!success && detected)   // 这里第一次没识别到卡应该触发的
        {
            detected = false;
            if(nfc_disconn_callback_)
                nfc_disconn_callback_();
        }
        vTaskDelay(200);
    }
}
