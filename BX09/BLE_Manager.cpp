#include "BLE_Manager.h"
// #include "UI.h"
// #include "Physics.h"

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
    // 變數實際定義區
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

    // 補回開關藍牙邏輯
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
            }
        } else {
            Serial.println("📡 [系統] 藍牙雷達重新啟動");
            current_ui_state = 0; 
            last_ui_state = -1;
        }
    }

    // 補回 BX-09 封包解析邏輯
    void notifyCallback(NimBLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
        if (length == 0) return;
        uint8_t header = pData[0];

        if (header == 0xA0) {
            if (length >= 4 && pData[3] == 0x04) {
                Serial.println("\n[狀態] 🟢 陀螺已安裝");
                Physics::reset();  
                isBeyInstalled = true;
            } 
        }
        else if (header >= 0x71 && header <= 0x73) {
            for (int i = 1; i < (int)length - 1; i += 2) {
                uint16_t val = pData[i] | (pData[i+1] << 8);
                Physics::addData(val); 
            }
            if (header == 0x73) {
                if (Physics::calculate()) { UI::readyToDraw = true; }
            }
        }
    }

    void scanCompleteCB(NimBLEScanResults results) {
        isScanning = false;
        pBLEScan->clearResults(); // 防記憶體洩漏
    }

    void init() {
        NimBLEDevice::init("ESP32_BEY_SNIFFER");
        pBLEScan = NimBLEDevice::getScan();
        // pBLEScan->setAdvertisedDeviceCallbacks(new AdvertisedDeviceCallbacks());
        pBLEScan->setScanCallbacks(new AdvertisedDeviceCallbacks());
        pBLEScan->setActiveScan(true);
        pBLEScan->setInterval(100);
        pBLEScan->setWindow(99); 
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
                
                // 1. 取得 Services 容器 (你這裡已經改對了)
                auto services = client->getServices(true);
                if (!services.empty()) {
                    for (auto* svc : services) {
                        
                        // 2. 取得 Characteristics 容器 (🚨 這次修正這裡！)
                        auto chars = svc->getCharacteristics(true); // 移除了 auto* 的星號
                        if (!chars.empty()) {                       // 移除了 if (chars) 改用 .empty()
                            for (auto* chr : chars) {               // 移除了 *chars 的星號
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
// === 回調函式具體實作 (綁定到 BLE_Manager 變數) ===

void ClientCallback::onDisconnect(NimBLEClient* pClient, int reason) {
    BLE_Manager::isConnected = false;
    BLE_Manager::isBeyInstalled = false;
    BLE_Manager::current_ui_state = 0;
    Serial.printf("!!! BX-09 斷開連線 (Reason: %d) ...\n", reason);
}

void AdvertisedDeviceCallbacks::onResult(const NimBLEAdvertisedDevice* advertisedDevice) {
    if (advertisedDevice->haveServiceUUID()) {
        if (advertisedDevice->getServiceUUID().toString().find("5c40000-f8eb-11ec-b939-0242ac120002") != std::string::npos) {
            Serial.println("\n✅ 發現 BX-09！鎖定目標...");
            NimBLEDevice::getScan()->stop();
            BLE_Manager::isScanning = false;
            
            // 🚨 修正記憶體洩漏：覆寫前先 delete 舊指標
            if (BLE_Manager::targetAddress != nullptr) {
                delete BLE_Manager::targetAddress;
            }
            BLE_Manager::targetAddress = new NimBLEAddress(advertisedDevice->getAddress());
            BLE_Manager::doConnect = true;
        }
    }
}