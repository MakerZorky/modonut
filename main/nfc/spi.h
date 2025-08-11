#include "driver/gpio.h"
#include <cstring>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "driver/spi_master.h"
#include "esp_system.h"
#include "boards/modo-board/config.h"

// 初始化SPI硬件
bool spiInit();

// SPI读写方法
uint8_t SPIRead(uint8_t addr);
void SPIWrite(uint8_t addr, uint8_t data);
void SPIReadSequence(uint8_t length, uint8_t addr, uint8_t* data);
void SPIWriteSequence(uint8_t length, uint8_t addr, const uint8_t* data);

// 硬件控制方法
esp_err_t HardPowerdown(bool enable);
esp_err_t HardReset();