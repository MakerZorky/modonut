#ifndef I2C_DEVICE_H
#define I2C_DEVICE_H

#include <driver/i2c_master.h>

class I2cDevice {
public:
    I2cDevice(i2c_master_bus_handle_t i2c_bus, uint8_t addr);

protected:
    i2c_master_dev_handle_t i2c_device_;

    esp_err_t WriteReg(uint8_t reg, uint8_t value);
    int ReadReg(uint8_t reg);  // Returns -1 on error, otherwise the register value
    esp_err_t ReadRegs(uint8_t reg, uint8_t* buffer, size_t length);
};

#endif // I2C_DEVICE_H
