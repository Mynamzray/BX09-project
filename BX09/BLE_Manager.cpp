#include "BLE_Manager.h"
#include "Web_Manager.h" // 🟢 任務一核心：引入 Web_Manager 標頭檔以進行跨模組通訊

namespace UI {
    extern volatile bool readyToDraw;
    void updateStatus(int state);
}

namespace Physics {
    void reset();
    void addData(uint16_t val);
    bool calculate();
}

namespace BLE_Manager {
    NimBLEClient* client = nullptr;
    NimBLEScan* pBLEScan = nullptr;
    NimBLEAddress* targetAddress = nullptr;
    
    volatile bool isConnected = false;
    volatile bool isBeyInstalled = false;
    volatile bool isSystemEnabled = true;
    bool doConnect = false;
    bool isScanning = false;
    
    int current_ui_state = 0;
    int last_ui_state = -1;

    void disconnectClient() {
        if (client != nullptr && client->isConnected()) {
            client->disconnect();
            isConnected = false;
        }
    }

    void toggleBluetooth() {
        isSystemEnabled = !isSystemEnabled; 
        if (!isSystemEnabled) {
            Serial.println("🛑 [系統] 藍牙雷達已手動休眠");
            if (isScanning && pBLEScan != nullptr) pBLEScan->stop();
            disconnectClient();
            
            isConnected = false;
            doConnect = false;
            isBeyInstalled = false;
            current_ui_state = 3; 
            if (current_ui_state != last_ui_state) {
                last_ui_state = current_ui_state;
                UI::updateStatus(current_ui_state);
                
                // 🟢 任務一：手動關閉系統藍牙時，同步重置網頁端的狀態燈
                Web_Manager::broadcastStatus(isConnected, isBeyInstalled);
            }
        } else {
            Serial.println("📡 [系統] 藍牙雷達重新啟動");
            current_ui_state = 0; 
            last_ui_state = -1;
        }
    }

    void notifyCallback(NimBLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
        if (length == 0) return;
        uint8_t header = pData[0];

        if (header == 0xA0) {
            Serial.print("[BLE 系統狀態攔截] ");
            for (size_t i = 0; i < length; i++) { Serial.printf("%02X ", pData[i]); }
            Serial.println(); 

            if (length >= 4 && pData[3] == 0x04) {
                Serial.println("\n[狀態] 🟢 陀螺已安裝");
                Physics::reset();  
                isBeyInstalled = true; // 狀態改變會由下方的 connectTask 自動捕捉並發送給網頁
            } 
            else if (length >= 4 && pData[3] == 0x00) {
                if (isBeyInstalled) { 
                    Serial.println("\n[狀態] 🟡 陀螺已手動拔除");
                    isBeyInstalled = false; 
                }
            }
        }
        else if (header == 0x70) {
            if (isBeyInstalled) { 
                Serial.println("\n[狀態] 🟡 陀螺已拔除 (收到系統靜止碼 0x70)");
                isBeyInstalled = false; 
            }
        }
        else if (header >= 0x71 && header <= 0x73) {
            for (int i = 1; i < (int)length - 1; i += 2) {
                uint16_t val = pData[i] | (pData[i+1] << 8);
                Physics::addData(val); 
            }
            if (header == 0x73) {
                if (Physics::calculate()) { 
                    UI::readyToDraw = true; 
                    // 備註：Physics::calculate 內部在成功後，會主動調用 Web_Manager::broadcastLaunch 送出圖表數據
                }
            }
        }
    }

    void scanCompleteCB(NimBLEScanResults results) {
        isScanning = false;
        pBLEScan->clearResults(); 
    }

    void init() {
        NimBLEDevice::init("ESP32_BEY_SNIFFER");
        pBLEScan = NimBLEDevice::getScan();
        pBLEScan->setScanCallbacks(new AdvertisedDeviceCallbacks());
        pBLEScan->setActiveScan(true);
        
        // 🚨 絕對不動 setWindow(50)，嚴格維持藍牙探測窗口與發射器封包的精準同步！
        pBLEScan->setInterval(100);
        pBLEScan->setWindow(50); 
    }

    void connectTask() {
        if (!isSystemEnabled) return;

        // UI 狀態機切換
        if (!isConnected) current_ui_state = 0; 
        else if (!isBeyInstalled) current_ui_state = 1; 
        else current_ui_state = 2;

        if (current_ui_state != last_ui_state) {
            last_ui_state = current_ui_state;
            UI::updateStatus(current_ui_state);
            
            // 🟢 任務一核心：當藍牙狀態發生改變，立刻推播給所有網頁用戶端！
            Web_Manager::broadcastStatus(isConnected, isBeyInstalled);
        }

        if (!isConnected && !doConnect && !isScanning) {
            isScanning = true;
            Serial.println(">>> 啟動雷達！開始搜尋... <<<");
            pBLEScan->start(0, scanCompleteCB, false);
        }

        if (doConnect) {
            doConnect = false;
            client = NimBLEDevice::createClient();
            client->setClientCallbacks(new ClientCallback());
            
            Serial.println(">>> 嘗試與目標裝置建立連線... <<<");
            if (client->connect(*targetAddress)) {
                isConnected = true;
                Serial.println(">>> 藍牙連線成功！掛載資料監聽器... <<<");
                
                auto services = client->getServices(true);
                if (!services.empty()) {
                    for (auto* svc : services) {
                        auto chars = svc->getCharacteristics(true); 
                        if (!chars.empty()) {                       
                            for (auto* chr : chars) {               
                                if (chr->canNotify()) {
                                    chr->subscribe(true, notifyCallback, false);
                                }
                            }
                        }
                    }
                }
            } else {
                Serial.println("❌ 連線失敗");
                isScanning = false;
            }
        }
    }
}

// === 回調函式具體實作 ===
void ClientCallback::onDisconnect(NimBLEClient* pClient, int reason) {
    BLE_Manager::isConnected = false;
    BLE_Manager::isBeyInstalled = false;
    BLE_Manager::current_ui_state = 0;
    Serial.printf("!!! BX-09 斷開連線 (Reason: %d) ...\n", reason);
    
    // 🟢 任務一：斷線時觸發一次廣播，強制將所有已開啟的網頁UI刷回紅色的斷線警告狀態
    Web_Manager::broadcastStatus(false, false);
}

void AdvertisedDeviceCallbacks::onResult(const NimBLEAdvertisedDevice* advertisedDevice) {
    if (advertisedDevice->haveServiceUUID()) {
        if (advertisedDevice->getServiceUUID().toString().find("5c40000-f8eb-11ec-b939-0242ac120002") != std::string::npos) {
            Serial.println("\n✅ 發現 BX-09！鎖定目標...");
            NimBLEDevice::getScan()->stop();
            BLE_Manager::isScanning = false;
            
            if (BLE_Manager::targetAddress != nullptr) {
                delete BLE_Manager::targetAddress;
            }
            BLE_Manager::targetAddress = new NimBLEAddress(advertisedDevice->getAddress());
            BLE_Manager::doConnect = true;
        }
    }
}