#ifndef BLE_MANAGER_H
#define BLE_MANAGER_H

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <map>
#include <vector>

// 1. 先宣告 namespace 與外部變數，讓下方的 Callbacks 能認得它們
namespace BLE_Manager {
    extern NimBLEClient* client;
    extern NimBLEScan* pBLEScan;
    extern NimBLEAddress* targetAddress;
    
    extern volatile bool isConnected;
    extern volatile bool isBeyInstalled;
    extern volatile bool isSystemEnabled;
    extern bool doConnect;
    extern bool isScanning;
    
    extern int current_ui_state;
    extern int last_ui_state;

    void disconnectClient();
    void toggleBluetooth();
    void notifyCallback(NimBLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify);
    void connectTask();
    void init();
}

// 2. 宣告 Callbacks (實作放在 .cpp，避免重複定義)
class ClientCallback : public NimBLEClientCallbacks {
    void onDisconnect(NimBLEClient* pClient, int reason) override;
};

// class AdvertisedDeviceCallbacks : public NimBLEAdvertisedDeviceCallbacks
    class AdvertisedDeviceCallbacks : public NimBLEScanCallbacks {
     void onResult(const NimBLEAdvertisedDevice* advertisedDevice) override;
 };

#endif