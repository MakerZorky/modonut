#ifndef WIFI_BOARD_H
#define WIFI_BOARD_H

#include "board.h"

#define AP_CONNECT 0
#define BLE_CONNECT 1

class WifiBoard : public Board {
protected:
    bool wifi_config_mode_ = false;

    WifiBoard();
    void EnterWifiConfigMode();
    virtual std::string GetBoardJson() override;

public:
    virtual std::string GetBoardType() override;
    virtual void StartNetwork() override;
    virtual Http* CreateHttp() override;
    virtual WebSocket* CreateWebSocket() override;
    virtual Mqtt* CreateMqtt() override;
    virtual Udp* CreateUdp() override;
    virtual void SetPowerSaveMode(bool enabled) override;
    virtual void ResetWifiConfiguration();
};

#endif // WIFI_BOARD_H
