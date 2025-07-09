#include "rc522.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstring>

#define TAG "Rc522"

// RC522寄存器地址
#define CommandReg     0x01
#define ComIEnReg      0x02
#define DivIEnReg      0x03
#define ComIrqReg      0x04
#define DivIrqReg      0x05
#define ErrorReg       0x06
#define Status1Reg     0x07
#define Status2Reg     0x08
#define FIFODataReg    0x09
#define FIFOLevelReg   0x0A
#define WaterLevelReg  0x0B
#define ControlReg     0x0C
#define BitFramingReg  0x0D
#define CollReg        0x0E
#define ModeReg        0x11
#define TxModeReg      0x12
#define RxModeReg      0x13
#define TxControlReg   0x14
#define TxAutoReg      0x15
#define TxSelReg       0x16
#define RxSelReg       0x17
#define RxThresholdReg 0x18
#define DemodReg       0x19
#define MfTxReg        0x1C
#define MfRxReg        0x1D
#define SerialSpeedReg 0x1F
#define CRCResultRegM  0x21
#define CRCResultRegL  0x22
#define ModWidthReg    0x24
#define RFCfgReg       0x26
#define GsNReg         0x27
#define CWGsPReg       0x28
#define ModGsPReg      0x29
#define TModeReg       0x2A
#define TPrescalerReg  0x2B
#define TReloadRegH    0x2C
#define TReloadRegL    0x2D
#define TCounterValRegH 0x2E
#define TCounterValRegL 0x2F
#define TestSel1Reg    0x31
#define TestSel2Reg    0x32
#define TestPinEnReg   0x33
#define TestPinValueReg 0x34
#define TestBusReg     0x35
#define AutoTestReg    0x36
#define VersionReg     0x37
#define AnalogTestReg  0x38
#define TestDAC1Reg    0x39
#define TestDAC2Reg    0x3A
#define TestADCReg     0x3B

// RC522命令
#define PCD_IDLE       0x00
#define PCD_AUTHENT    0x0E
#define PCD_RECEIVE    0x08
#define PCD_TRANSMIT   0x04
#define PCD_TRANSCEIVE 0x0C
#define PCD_RESETPHASE 0x0F
#define PCD_CALCCRC    0x03

// MIFARE命令
#define PICC_REQIDL    0x26
#define PICC_REQALL    0x52
#define PICC_ANTICOLL  0x93
#define PICC_SElECTTAG 0x93
#define PICC_AUTHENT1A 0x60
#define PICC_AUTHENT1B 0x61
#define PICC_READ      0x30
#define PICC_WRITE     0xA0
#define PICC_DECREMENT 0xC0
#define PICC_INCREMENT 0xC1
#define PICC_RESTORE   0xC2
#define PICC_TRANSFER  0xB0
#define PICC_HALT      0x50

Rc522::Rc522(spi_host_device_t spi_host, gpio_num_t miso, gpio_num_t mosi, 
             gpio_num_t sclk, gpio_num_t cs, gpio_num_t rst)
    : spi_host_(spi_host), miso_(miso), mosi_(mosi), sclk_(sclk), cs_(cs), rst_(rst),
      spi_device_(nullptr), card_present_(false) {
}

Rc522::~Rc522() {
    StopCardDetection();
    if (spi_device_) {
        spi_bus_remove_device(spi_device_);
    }
}

bool Rc522::Init() {
    // 配置SPI总线
    spi_bus_config_t bus_config = {
        .mosi_io_num = mosi_,
        .miso_io_num = miso_,
        .sclk_io_num = sclk_,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 32,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(spi_host_, &bus_config, SPI_DMA_CH_AUTO));
    
    // 配置SPI设备
    spi_device_interface_config_t dev_config = {
        .command_bits = 0,
        .address_bits = 0,
        .dummy_bits = 0,
        .mode = 0,
        .duty_cycle_pos = 0,
        .cs_ena_pretrans = 0,
        .cs_ena_posttrans = 0,
        .clock_speed_hz = 4 * 1000 * 1000, // 4MHz
        .input_delay_ns = 0,
        .spics_io_num = cs_,
        .flags = 0,
        .queue_size = 7,
        .pre_cb = nullptr,
        .post_cb = nullptr,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(spi_host_, &dev_config, &spi_device_));
    
    // 配置RST引脚
    gpio_config_t rst_config = {
        .pin_bit_mask = (1ULL << rst_),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&rst_config);
    
    // 复位RC522
    gpio_set_level(rst_, 0);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(rst_, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    
    // 初始化RC522
    WriteRegister(TModeReg, 0x8D);
    WriteRegister(TPrescalerReg, 0x3E);
    WriteRegister(TReloadRegL, 30);
    WriteRegister(TReloadRegH, 0);
    WriteRegister(TxAutoReg, 0x40);
    WriteRegister(ModeReg, 0x3D);
    
    // 开启天线
    SetRegisterBits(TxControlReg, 0x03);
    
    ESP_LOGI(TAG, "RC522 initialized successfully");
    return true;
}

void Rc522::WriteRegister(uint8_t reg, uint8_t value) {
    uint8_t data[2] = {static_cast<uint8_t>((static_cast<uint16_t>(reg) << 1) & 0x7E), value};
    spi_transaction_t trans = {
        .length = 16,
        .tx_buffer = data,
    };
    spi_device_transmit(spi_device_, &trans);
}

uint8_t Rc522::ReadRegister(uint8_t reg) {
    uint8_t tx_data[2] = {static_cast<uint8_t>(((static_cast<uint16_t>(reg) << 1) & 0x7E) | 0x80), 0};
    uint8_t rx_data[2];
    spi_transaction_t trans = {
        .length = 16,
        .tx_buffer = tx_data,
        .rx_buffer = rx_data,
    };
    spi_device_transmit(spi_device_, &trans);
    return rx_data[1];
}

void Rc522::SetRegisterBits(uint8_t reg, uint8_t mask) {
    uint8_t value = ReadRegister(reg);
    WriteRegister(reg, value | mask);
}

void Rc522::ClearRegisterBits(uint8_t reg, uint8_t mask) {
    uint8_t value = ReadRegister(reg);
    WriteRegister(reg, value & ~mask);
}

bool Rc522::IsCardPresent() {
    uint8_t status = ReadRegister(ComIrqReg);
    return (status & 0x20) != 0;
}

bool Rc522::ReadCardUid(uint8_t* uid, uint8_t* uid_len) {
    if (!IsCardPresent()) {
        return false;
    }
    
    // 防冲突
    if (!Anticoll(uid)) {
        return false;
    }
    
    *uid_len = 4;
    return true;
}

bool Rc522::Anticoll(uint8_t* uid) {
    uint8_t command = PICC_ANTICOLL;
    //uint8_t data[4] = {command, 0x20, 0, 0};
    
    WriteRegister(BitFramingReg, 0x00);
    WriteRegister(FIFODataReg, command);
    WriteRegister(FIFODataReg, 0x20);
    
    WriteRegister(CommandReg, PCD_TRANSCEIVE);
    SetRegisterBits(FIFOLevelReg, 0x80);
    
    uint8_t wait_irq = 0x30;
    uint8_t status = 0;
    uint8_t irq_en = 0x00;
    uint8_t last_bits = 0;
    uint8_t n = 0;
    
    uint32_t i;
    for (i = 2000; i > 0; i--) {
        irq_en = ReadRegister(ComIEnReg);
        WriteRegister(ComIEnReg, irq_en | 0x20);
        last_bits = ReadRegister(ControlReg) & 0x07;
        if (last_bits != 0) {
            n = last_bits;
        }
        
        WriteRegister(CommandReg, PCD_TRANSCEIVE);
        
        if (last_bits != 0) {
            WriteRegister(FIFOLevelReg, 0x80 | n);
        }
        
        i = 2000;
        do {
            status = ReadRegister(ComIrqReg);
            if ((i != 0) && ((status & 0x01) || (status & wait_irq))) {
                break;
            }
            i--;
        } while (i);
        
        if (i != 0 && !(ReadRegister(ErrorReg) & 0x1B)) {
            if (status & irq_en & 0x20) {
                break;
            }
            
            if (status & 0x01) {
                break;
            }
        }
    }
    
    if (i != 0) {
        uint8_t length = ReadRegister(FIFOLevelReg);
        if (length == 4) {
            for (uint8_t j = 0; j < 4; j++) {
                uid[j] = ReadRegister(FIFODataReg);
            }
            return true;
        }
    }
    
    return false;
}

bool Rc522::ReadCardData(uint8_t block_addr, uint8_t* data) {
    // 简化实现，实际需要认证等步骤
    return false;
}

bool Rc522::WriteCardData(uint8_t block_addr, const uint8_t* data) {
    // 简化实现，实际需要认证等步骤
    return false;
}

void Rc522::SetCardDetectedCallback(std::function<void(const std::string&)> callback) {
    card_detected_callback_ = callback;
}

void Rc522::SetCardRemovedCallback(std::function<void()> callback) {
    card_removed_callback_ = callback;
}

void Rc522::StartCardDetection() {
    xTaskCreate(CardDetectionTaskWrapper, "rc522_detect", 4096, this, 5, nullptr);
}

void Rc522::StopCardDetection() {
    // 停止检测任务
}

void Rc522::CardDetectionTaskWrapper(void* param) {
    Rc522* rc522 = static_cast<Rc522*>(param);
    rc522->CardDetectionTask();
}

void Rc522::CardDetectionTask() {
    while (true) {
        bool current_card_present = IsCardPresent();
        
        if (current_card_present && !card_present_) {
            // 卡片被检测到
            uint8_t uid[4];
            uint8_t uid_len;
            if (ReadCardUid(uid, &uid_len)) {
                std::string card_uid;
                for (int i = 0; i < uid_len; i++) {
                    char hex[3];
                    snprintf(hex, sizeof(hex), "%02X", uid[i]);
                    card_uid += hex;
                }
                last_card_uid_ = card_uid;
                card_present_ = true;
                
                if (card_detected_callback_) {
                    card_detected_callback_(card_uid);
                }
                ESP_LOGI(TAG, "Card detected: %s", card_uid.c_str());
            }
        } else if (!current_card_present && card_present_) {
            // 卡片被移除
            card_present_ = false;
            last_card_uid_.clear();
            
            if (card_removed_callback_) {
                card_removed_callback_();
            }
            ESP_LOGI(TAG, "Card removed");
        }
        
        vTaskDelay(pdMS_TO_TICKS(100)); // 100ms检测间隔
    }
} 