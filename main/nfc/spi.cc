#include <spi.h>

#define SPI_TAG "SPI"

bool initialized_ = false;
spi_device_handle_t spi_device_ = nullptr;

bool spiInit() {
    gpio_pullup_en(FM175XX_MISO);    // 启用内部上拉
    // 配置NPD引脚
    gpio_config_t npd_config = {
        .pin_bit_mask = (1ULL << FM175XX_NPD),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&npd_config);
    gpio_set_level(FM175XX_NPD, 1); // 初始化为高电平(未掉电)

    spi_bus_config_t bus_config_;
    // 配置SPI总线
    bus_config_.miso_io_num = FM175XX_MISO;
    bus_config_.mosi_io_num = FM175XX_MOSI;
    bus_config_.sclk_io_num = FM175XX_SCK;
    bus_config_.quadwp_io_num = -1;
    bus_config_.quadhd_io_num = -1;
    bus_config_.max_transfer_sz = 64;
    bus_config_.flags = SPICOMMON_BUSFLAG_MASTER| SPICOMMON_BUSFLAG_GPIO_PINS; // 显式主机模式
    bus_config_.isr_cpu_id = ESP_INTR_CPU_AFFINITY_AUTO;
    bus_config_.intr_flags = ESP_INTR_FLAG_SHARED;

    esp_err_t ret = spi_bus_initialize(SPI2_HOST, &bus_config_, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(SPI_TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
        return ret;
    }

    // 配置SPI设备
    spi_device_interface_config_t dev_config_;
    dev_config_.command_bits = 8;   // 8位命令
    dev_config_.address_bits = 0;
    dev_config_.dummy_bits = 0;
    dev_config_.clock_source = SPI_CLK_SRC_DEFAULT;
    dev_config_.clock_speed_hz = 1 * 1000 * 1000; // 1 MHz 测试
    dev_config_.mode = 0;                  // SPI Mode 0
    dev_config_.spics_io_num = FM175XX_CS;
    dev_config_.flags = 0;
    dev_config_.queue_size = 7;
    dev_config_.sample_point = SPI_SAMPLING_POINT_PHASE_0; //采样点1不支持
    //必要的！
    dev_config_.pre_cb = nullptr;
    dev_config_.post_cb = nullptr;

    ret = spi_bus_add_device(SPI2_HOST, &dev_config_, &spi_device_);
    if (ret != ESP_OK) {
        ESP_LOGE(SPI_TAG, "Failed to add SPI device: %s", esp_err_to_name(ret));
        return ret;
    }

    initialized_ = true;
    ESP_LOGI(SPI_TAG, "SPI initialized successfully");
    
    return true;
}

uint8_t SPIRead(uint8_t addr)
{
    if (!initialized_ || !spi_device_) return 0;
    //ESP-IDF 的 SPI 主机驱动规定：传给 spi_device_polling_transmit() 的 spi_transaction_t 结构体 地址必须是 4-byte 对齐
    spi_transaction_t t __attribute__((aligned(4))) = {}; 
    t.cmd = (addr << 1) | 0x80;      // 读命令 (最高位=1)
    t.length   = 8;      
    t.rxlength = 8;      
    t.flags  = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA;       
    t.tx_data[0] = 0x00; 
    
    esp_err_t err = spi_device_polling_transmit(spi_device_, &t);
    if (err != ESP_OK) {
        ESP_LOGE(SPI_TAG, "SPI read error: %s", esp_err_to_name(err));
        return 0;
    }
    // ESP_LOGI(SPI_TAG, "Address: 0x%02X, Received: 0x%02X",addr, t.rx_data[0]);         
    return t.rx_data[0];  
    /********************************************************************* */
}

void SPIWrite(uint8_t addr, uint8_t data) {
    if (!initialized_ || !spi_device_) return;
    //ESP-IDF 的 SPI 主机驱动规定：传给 spi_device_polling_transmit() 的 spi_transaction_t 结构体 地址必须是 4-byte 对齐
    spi_transaction_t t __attribute__((aligned(4))) = {};
    t.cmd = (addr << 1) & 0x7E;             
    t.length = 8;               // 发送8位数据
    t.flags = SPI_TRANS_USE_TXDATA;
    t.tx_data[0] = data;        // 要写入的数据
    
    esp_err_t err = spi_device_polling_transmit(spi_device_, &t);
    if (err != ESP_OK) {
        ESP_LOGE(SPI_TAG, "SPI write error: %s", esp_err_to_name(err));
    }
    /********************************************************************************** */
    //测试是不是值都写进去了
    // SPIRead(addr);
}

esp_err_t HardPowerdown(bool enable) {
    gpio_set_level(FM175XX_NPD, enable ? 0 : 1);
    vTaskDelay(pdMS_TO_TICKS(50)); // 等待稳定
    return ESP_OK;
}

esp_err_t HardReset() {
    gpio_set_level(FM175XX_NPD, 0); // 拉低NPD引脚
    vTaskDelay(pdMS_TO_TICKS(100));  // 保持5ms
    gpio_set_level(FM175XX_NPD, 1); // 释放NPD引脚
    vTaskDelay(pdMS_TO_TICKS(200)); // 等待复位完成

    //软件复位
    SPIWrite(0x01, 0x0F); // 发送软件复位命令
    vTaskDelay(pdMS_TO_TICKS(50));
    return ESP_OK;
}

// uint8_t SPIRead(uint8_t addr)
// {
    // if (!initialized_ || !spi_device_) return 0;

    // spi_transaction_t t = {};
    // t.flags  = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA;
    // t.length = 16;
    // t.tx_data[0] = (addr << 1) | 0x80;             // 第 1 字节：命令/地址 + 读标志
    // t.tx_data[1] = 0x00;                           // 第 2 字节：Dummy（只为产生时钟）
    // ESP_LOGI(SPI_TAG, "send: 0x%02X ", t.tx_data[0]);
    
    // spi_transaction_t t = {};
    // uint8_t tx_buf[4] = { (uint8_t)((addr << 1) | 0x80), 0x00, 0x00, 0x00 };  // 命令 + 3 dummy
    // uint8_t rx_buf[4] = {0x00, 0x00, 0x00, 0x00};
    // t.length = 32;
    // t.tx_buffer = tx_buf;
    // t.rx_buffer = rx_buf;
    // ESP_LOGI(SPI_TAG, "send: 0x%02X ", tx_buf[0]);

    // esp_err_t ret = spi_device_polling_transmit(spi_device_, &t);
    // if (ret != ESP_OK) {
    //     ESP_LOGE(SPI_TAG, "SPI read failed (addr: 0x%02X): %s", addr, esp_err_to_name(ret));
    //     return ret;
    // }
    // // ESP_LOGI(SPI_TAG, "Received: 0x%02X ", t.rx_data[1]);
    // ESP_LOGI(SPI_TAG, "Received: 0x%02X 0x%02X 0x%02X 0x%02X  ", rx_buf[0], rx_buf[1], rx_buf[2], rx_buf[3]);
    // // return t.rx_data[1];             
    // return rx_buf[1];  

    /*************************************************************** */
    // // 获取总线锁（可指定超时）
    // spi_device_acquire_bus(spi_device_, portMAX_DELAY);
    // // ---------- ① 发送命令 ----------
    // spi_transaction_t t1 = {};
    // t1.flags  = SPI_TRANS_USE_TXDATA | SPI_TRANS_CS_KEEP_ACTIVE;
    // t1.length = 8;                         // 只发 1 字节
    // t1.tx_data[0] = (addr << 1) | 0x80;    // 0x82
    // spi_device_polling_transmit(spi_device_, &t1);

    // // ets_delay_us(10);     

    // // ---------- ② 发送 Dummy 并接收 ----------
    // spi_transaction_t t2 = {};
    // t2.flags  = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA | SPI_TRANS_CS_KEEP_ACTIVE;
    // t2.length = 8;                         // Dummy 1 字节
    // t2.tx_data[0] = 0x00;
    // spi_device_polling_transmit(spi_device_, &t2);

    // t2.flags  = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA | SPI_TRANS_CS_KEEP_ACTIVE;
    // spi_device_polling_transmit(spi_device_, &t2);

    // t2.flags  = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA | SPI_TRANS_CS_KEEP_ACTIVE;
    // spi_device_polling_transmit(spi_device_, &t2);

    // t2.flags  = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA | SPI_TRANS_CS_KEEP_ACTIVE;
    // spi_device_polling_transmit(spi_device_, &t2);

    // t2.flags  = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA;
    // spi_device_polling_transmit(spi_device_, &t2);

    // /* 释放总线锁 */
    // spi_device_release_bus(spi_device_);
    // ESP_LOGI(SPI_TAG, "Received: 0x%02X ", t2.rx_data[0]);
    // // CS 已在第二段结束后自动拉高
    // return t2.rx_data[0];
    /*********************************************************************** */
//}

// void SPIWrite(uint8_t addr, uint8_t data) {
//     if (!initialized_ || !spi_device_) return;

    // spi_transaction_t t = {};
    // t.flags  = SPI_TRANS_USE_TXDATA;
    // t.length = 16;
    // t.tx_data[0] = static_cast<uint8_t>((addr << 1) & 0x7E); // W
    // t.tx_data[1] = data;

    // spi_transaction_t t = {};
    // uint8_t tx_buf[4] = { (uint8_t)((addr << 1) & 0x7E), data, 0x00, 0x00};  
    // t.length = 32;
    // t.tx_buffer = tx_buf;
    // ESP_LOGI(SPI_TAG, "send: 0x%02X 0x%02X ", tx_buf[0], tx_buf[1]);
    
    // esp_err_t err = spi_device_polling_transmit(spi_device_, &t);
    // if (err != ESP_OK) {
    //     ESP_LOGE(SPI_TAG, "SPI write failed (addr: 0x%02X): %s", addr, esp_err_to_name(err));
    // }
// }