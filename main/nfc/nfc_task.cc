#include "nfc_task.h"

#define NFC_TAG "NfcTask"

NfcTask::NfcTask(){
    event_group_ = xEventGroupCreate();
}

NfcTask::~NfcTask() {
    if (nfc_task_handle_) {
        vTaskDelete(nfc_task_handle_);
    }
    if (event_group_) {
        vEventGroupDelete(event_group_);
    }
    spi_bus_free(SPI2_HOST);
}

bool NfcTask::nfcInit() {
    return true;
}

void NfcTask::start() {
    xTaskCreate([](void* arg) {
        static_cast<NfcTask*>(arg)->NfcTaskLoop();
    }, "nfc_task", stack_size_, this, 8, &nfc_task_handle_);
}

void NfcTask::StartDetection() {
    xEventGroupSetBits(event_group_, SPI_RUNNING_EVENT);
}

void NfcTask::StopDetection() {
    xEventGroupClearBits(event_group_, SPI_RUNNING_EVENT);
}

bool NfcTask::IsDetectionRunning() {
    return (xEventGroupGetBits(event_group_) & SPI_RUNNING_EVENT) != 0;
}

void NfcTask::OnReady(std::function<bool()> callback){
    on_ready_ = callback;
}

void NfcTask::OnNfcStateChange(std::function<void(bool)> callback) {
    spi_state_change_callback_ = callback;
}

void NfcTask::OnNfcWakeDetected(std::function<void(const std::string& wake_word)> callback) {
    nfc_wake_detected_callback_ = callback;
}

void NfcTask::OnNfcDisCon(std::function<void(void)> callback){
    nfc_disconn_callback_ = callback;
}

void NfcTask::NfcTaskLoop() {
    if (!spiInit()) {
        ESP_LOGE(NFC_TAG, "SPI initialization failed, task exiting");
        vTaskDelete(nullptr);
        return;
    }

    //硬件复位
    uint8_t ret = HardReset();
    if(ret!=ESP_OK){
        ESP_LOGE(NFC_TAG, "HardReset failed!");
    }
    uint8_t reset_value=Read_Reg(CommandReg);
    if(reset_value!=0x20){
        ESP_LOGE(NFC_TAG, "Reset failed!");
    }

    // unsigned char set_RF = Set_Rf(3);   //选择TX1，TX2输出
    // ESP_LOGI(NFC_TAG, "Set_Rf: %d , 0=OK",set_RF);	
    
    // unsigned char Pcd_ConfigISOType_value = Pcd_ConfigISOType(0);//选择TYPE A模式		
    // ESP_LOGI(NFC_TAG, "Pcd_ConfigISOType_value: %d , 0=OK",Pcd_ConfigISOType_value);

    unsigned char rece_buff[6] = {0};
    unsigned char write_buff[4] = {0};
    write_buff[3] = 0x02;
    unsigned char rece_length = 4;
    uint8_t success;
    while(1)
    {		
        // 等待检测事件
        xEventGroupWaitBits(event_group_, SPI_RUNNING_EVENT, pdFALSE, pdTRUE, portMAX_DELAY);

        // PCD_WRITE_CARD(write_buff, 4, rece_buff, &rece_length);

        // success = PCD_READ_CARD(rece_buff, &rece_length);
        // if (success == OK) {
        //     ESP_LOGI(NFC_TAG, "Card detected");
        //     if(nfc_wake_detected_callback_){
        //         //ESP_LOGI(NFC_TAG, "rece_buff %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X", rece_buff[0], rece_buff[1], rece_buff[2], rece_buff[3], rece_buff[4], rece_buff[5], rece_buff[6], rece_buff[7], rece_buff[8], rece_buff[9], rece_buff[10], rece_buff[11], rece_buff[12], rece_buff[13], rece_buff[14], rece_buff[15]);
        //         std::string dataStr = std::to_string(rece_buff[9]);
        //         nfc_wake_detected_callback_(dataStr);
        //     }
        //     detected_ = true;
        // }else if((success != OK) && (detected_ == true)){
        //     if(nfc_disconn_callback_){
        //         nfc_disconn_callback_();
        //     }
        //     detected_ = false;
        // }

        vTaskDelay(pdMS_TO_TICKS(500)); 		
    }
}