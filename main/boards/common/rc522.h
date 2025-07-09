#ifndef __RC522_H__
#define __RC522_H__

#include <driver/spi_master.h>
#include <driver/gpio.h>
#include <string>
#include <functional>

class Rc522 {
public:
    Rc522(spi_host_device_t spi_host, gpio_num_t miso, gpio_num_t mosi, 
          gpio_num_t sclk, gpio_num_t cs, gpio_num_t rst);
    ~Rc522();

    // 初始化RC522
    bool Init();
    
    // 检测卡片
    bool IsCardPresent();
    
    // 读取卡片UID
    bool ReadCardUid(uint8_t* uid, uint8_t* uid_len);
    
    // 读取卡片数据
    bool ReadCardData(uint8_t block_addr, uint8_t* data);
    
    // 写入卡片数据
    bool WriteCardData(uint8_t block_addr, const uint8_t* data);
    
    // 设置卡片检测回调
    void SetCardDetectedCallback(std::function<void(const std::string&)> callback);
    
    // 设置卡片移除回调
    void SetCardRemovedCallback(std::function<void()> callback);
    
    // 开始卡片检测任务
    void StartCardDetection();
    
    // 停止卡片检测任务
    void StopCardDetection();

private:
    spi_host_device_t spi_host_;
    gpio_num_t miso_;
    gpio_num_t mosi_;
    gpio_num_t sclk_;
    gpio_num_t cs_;
    gpio_num_t rst_;
    
    spi_device_handle_t spi_device_;
    std::string last_card_uid_;
    bool card_present_;
    
    std::function<void(const std::string&)> card_detected_callback_;
    std::function<void()> card_removed_callback_;
    
    // 内部方法
    void WriteRegister(uint8_t reg, uint8_t value);
    uint8_t ReadRegister(uint8_t reg);
    void SetRegisterBits(uint8_t reg, uint8_t mask);
    void ClearRegisterBits(uint8_t reg, uint8_t mask);
    bool Anticoll(uint8_t* uid);
    bool Select(uint8_t* uid);
    bool Authenticate(uint8_t command, uint8_t block_addr, uint8_t* key, uint8_t* uid);
    void CardDetectionTask();
    
    static void CardDetectionTaskWrapper(void* param);
};

#endif // __RC522_H__ 