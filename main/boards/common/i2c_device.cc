#include "i2c_device.h"

#include <esp_log.h>
#include <stdexcept>

#define TAG "I2cDevice"


I2cDevice::I2cDevice(i2c_master_bus_handle_t i2c_bus, uint8_t addr) {
    i2c_device_config_t i2c_device_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = 400 * 1000,
        .scl_wait_us = 0,
        .flags = {
            .disable_ack_check = 0,
        },
    };
    esp_err_t ret = i2c_master_bus_add_device(i2c_bus, &i2c_device_cfg, &i2c_device_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add I2C device: addr=0x%02x, error=%s", addr, esp_err_to_name(ret));
        throw std::runtime_error("I2C device initialization failed");
    }
    assert(i2c_device_ != NULL);
}

esp_err_t I2cDevice::WriteReg(uint8_t reg, uint8_t value) {
    uint8_t buffer[2] = {reg, value};
    esp_err_t ret = i2c_master_transmit(i2c_device_, buffer, 2, 100);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "WriteReg failed: reg=0x%02x, value=0x%02x, error=%s", 
                 reg, value, esp_err_to_name(ret));
    }
    return ret;
}

int I2cDevice::ReadReg(uint8_t reg) {
    uint8_t buffer[1];
    esp_err_t ret = i2c_master_transmit_receive(i2c_device_, &reg, 1, buffer, 1, 100);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "ReadReg failed: reg=0x%02x, error=%s", 
                 reg, esp_err_to_name(ret));
        return -1;
    }
    return buffer[0];
}

esp_err_t I2cDevice::ReadRegs(uint8_t reg, uint8_t* buffer, size_t length) {
    esp_err_t ret = i2c_master_transmit_receive(i2c_device_, &reg, 1, buffer, length, 100);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "ReadRegs failed: reg=0x%02x, length=%d, error=%s", 
                 reg, length, esp_err_to_name(ret));
    }
    return ret;
}