#pragma once
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

// ==========================================
// [模組 3] BLE 監聽器 (智慧嗅探與防斷線穩定版)
// ==========================================
namespace BLE_Manager {
    BLEClient* client = nullptr;
    BLEScan* pBLEScan = nullptr;
    BLEAddress* targetAddress = nullptr; 
    
    volatile bool isConnected = false;
    volatile bool isBeyInstalled = false;
    volatile bool isSystemEnabled = true;
    bool doConnect = false;
    bool isScanning = false; 

    int current_ui_state = 0;
    int last_ui_state = -1;

    void toggleBluetooth() {
        isSystemEnabled = !isSystemEnabled; 
        
        if (!isSystemEnabled) {
            Serial.println("🛑 [系統] 藍牙雷達已手動休眠");
            
            if (isScanning && pBLEScan != nullptr) {
                pBLEScan->stop();
                isScanning = false;
            }
            if (client != nullptr && isConnected) {
                client->disconnect();
            }
            
            isConnected = false;
            doConnect = false;
            isBeyInstalled = false;
            
            current_ui_state = 3; 
            if (current_ui_state != last_ui_state) {
                last_ui_state = current_ui_state;
                UI::updateStatus(current_ui_state);
                lv_timer_handler(); 
            }
        } else {
            Serial.println("📡 [系統] 藍牙雷達重新啟動");
            current_ui_state = 0; 
            last_ui_state = -1; 
        }
    }

static void notifyCallback(BLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
        if (length == 0) return;
        uint8_t header = pData[0];

        // 🟢 1. 處理系統狀態封包 (0xA0)
        if (header == 0xA0) {
            // [智慧嗅探器] 印出 A0 封包，方便隨時監控硬體狀態
            Serial.print("[BLE 系統狀態攔截] ");
            for (size_t i = 0; i < length; i++) {
                Serial.printf("%02X ", pData[i]); 
            }
            Serial.println(); 

            // [判斷安裝] -> pData[3] 是 0x04 (壓下開關)
            if (length >= 4 && pData[3] == 0x04) {
                Serial.println("\n[狀態] 🟢 陀螺已安裝");
                Physics::reset();  
                isBeyInstalled = true; 
                UI::updateStatus(2); // 切換到綠燈 (Ready)
            } 
            // 🟢 [判斷反悔/手動拔除] -> pData[3] 變成 0x00 (開關彈起)
            else if (length >= 4 && pData[3] == 0x00) {
                if (isBeyInstalled) { // 確保原本是裝著的才觸發
                    Serial.println("\n[狀態] 🟡 陀螺已手動拔除");
                    isBeyInstalled = false;
                    UI::updateStatus(1); // 切換回黃燈 (Waiting)
                }
            }
        }
        // 🟢 2. 處理靜止空載封包 (0x70) -> 雙重保險的拔除特徵
        else if (header == 0x70) {
            if (isBeyInstalled) { 
                Serial.println("\n[狀態] 🟡 陀螺已拔除 (系統靜止)");
                isBeyInstalled = false;
                UI::updateStatus(1); // 切換回黃燈 (Waiting)
            }
        }
        // 🔴 3. 處理真實發射轉速封包 (0x71 ~ 0x73) -> 算分引擎
        else if (header >= 0x71 && header <= 0x73) {
            for (int i = 1; i < (int)length - 1; i += 2) {
                uint16_t val = pData[i] | (pData[i+1] << 8);
                Physics::addData(val); 
            }

            if (header == 0x73) {
                if (Physics::calculate()) { 
                    Serial.println("\n=========================================");
                    Serial.printf("🔥 最高: %.0f | 👑 生涯最高: %.0f\n", Physics::peak_rpm, Physics::allTimePeak);
                    Serial.println("=========================================\n");
                    UI::readyToDraw = true; // 觸發 UI 畫面的 GO SHOOT 動畫
                }
            }
        }
    }

    class ClientCallback : public BLEClientCallbacks {
        void onDisconnect(BLEClient* pclient) {
            isConnected = false;
            isBeyInstalled = false; 
            Serial.println("!!! BX-09 斷開連線 ...");
        }
    };

    class AdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
        void onResult(BLEAdvertisedDevice advertisedDevice) {
            if (advertisedDevice.haveServiceUUID()) {
                String deviceUUID = advertisedDevice.getServiceUUID().toString().c_str();
                deviceUUID.toLowerCase();

                if (deviceUUID.indexOf("5c40000-f8eb-11ec-b939-0242ac120002") >= 0) {
                    Serial.println("\n✅ 發現 BX-09！鎖定目標，準備連線...");
                    BLEDevice::getScan()->stop();
                    isScanning = false;
                    
                    if (targetAddress != nullptr) delete targetAddress;
                    targetAddress = new BLEAddress(advertisedDevice.getAddress());
                    doConnect = true; 
                }
            }
        }
    };

    void init() {
        BLEDevice::init("ESP32_BEY_SNIFFER");
        pBLEScan = BLEDevice::getScan();
        pBLEScan->setAdvertisedDeviceCallbacks(new AdvertisedDeviceCallbacks());
        pBLEScan->setActiveScan(true); 
        pBLEScan->setInterval(100);
        pBLEScan->setWindow(99); 
    }

    void scanCompleteCB(BLEScanResults results) {
        pBLEScan->clearResults();
    }

    void connectTask() {
        if (!isSystemEnabled) return; 

        if (!isConnected) {
            current_ui_state = 0; 
        } else if (isConnected && !isBeyInstalled) {
            current_ui_state = 1; 
        } else if (isConnected && isBeyInstalled) {
            current_ui_state = 2; 
        }

        if (current_ui_state != last_ui_state) {
            last_ui_state = current_ui_state;
            UI::updateStatus(current_ui_state); 
            lv_timer_handler(); 
        }

        if (!isConnected && !doConnect && !isScanning) {
            isScanning = true;
            Serial.println(">>> 啟動雷達！開始搜尋... <<<");
            pBLEScan->clearResults(); 
            pBLEScan->start(0, scanCompleteCB, false); 
        }

        if (doConnect) {
            doConnect = false;
            
            // 🛑 連線穩定器：給無線電晶片 250ms 的時間，從「掃描模式」徹底切換到「連線模式」
            delay(250);
            
            if (client != nullptr) {
                delete client;
            }
            
            client = BLEDevice::createClient();
            client->setClientCallbacks(new ClientCallback());
            client->setMTU(517);
            Serial.println(">>> 嘗試與目標裝置建立連線... <<<");
            
            if (client->connect(*targetAddress)) {
                isConnected = true;
                Serial.println(">>> 藍牙連線成功！掛載資料監聽器... <<<");

                std::map<std::string, BLERemoteService*>* services = client->getServices();
                for (auto const& sPair : *services) {
                    auto chars = sPair.second->getCharacteristics();
                    for (auto const& cPair : *chars) {
                        if (cPair.second->canNotify()) {
                            cPair.second->registerForNotify(notifyCallback);
                        }
                    }
                }
                Serial.println(">>> 系統準備就緒！等待安裝陀螺 <<<");
            } else {
                Serial.println("❌ 連線失敗");
                isScanning = false;
                lv_obj_invalidate(lv_scr_act());
            }
        }
    }
}