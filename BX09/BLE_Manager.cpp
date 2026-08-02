#include "BLE_Manager.h"
#include "Web_Manager.h"
#include "lvgl_port.h"
#include <Preferences.h>
#include <WiFi.h>

namespace UI {
    extern volatile bool readyToDraw;
    void updateStatus(int state);
    void updateOfficialData(uint16_t origSP, uint16_t* history, int histCount); 
    void updateChartCurve(uint16_t* turnData, int turnCount);
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

    static uint8_t historyPackets[7][20];

    uint16_t liveCurveBuffer[32];
    int liveCurveCount = 0;

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
            if (isScanning && pBLEScan != nullptr) {
                pBLEScan->stop();
                pBLEScan->clearResults(); 
            }
            disconnectClient();
            
            isConnected = false;
            doConnect = false;
            isScanning = false; 
            isBeyInstalled = false;
            current_ui_state = 3; 
            if (current_ui_state != last_ui_state) {
                last_ui_state = current_ui_state;
                if (example_lvgl_lock(100)) {
                    UI::updateStatus(current_ui_state);
                    example_lvgl_unlock();
                }
                
                #if ENABLE_WEB_DASHBOARD
                Preferences tempPrefs;
                tempPrefs.begin("bx09_store", true);
                bool web_on = tempPrefs.getBool("web_on", false);
                tempPrefs.end();
                if (web_on) {
                    Web_Manager::broadcastStatus(isConnected, isBeyInstalled);
                }
                #endif
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

            if (length >= 4) {
                uint8_t stateByte = pData[3];
                bool isAttachedNow = (stateByte & 0x04) > 0;
                bool isButtonModeOn = (stateByte & 0x10) > 0;

                if (isAttachedNow && !isBeyInstalled) {
                    Serial.println("\n[狀態] 🟢 陀螺已安裝");
                    isBeyInstalled = true; 
                    liveCurveCount = 0;
                } 
                else if (!isAttachedNow && isBeyInstalled) {
                    Serial.println("\n[狀態] 🟡 陀螺已手動拔除");
                    isBeyInstalled = false; 
                }

                static bool lastButtonMode = false;
                if (isButtonModeOn != lastButtonMode) {
                    lastButtonMode = isButtonModeOn;
                    Serial.printf("\n🕹️ [遙控器] 偵測到發射器實體按鈕雙擊！目前隱藏模式: %s\n", isButtonModeOn ? "ON (0x10)" : "OFF (0x00)");
                    
                    Preferences tempPrefs;
                    tempPrefs.begin("bx09_store", false);
                    bool web_on = tempPrefs.getBool("web_on", false);
                    web_on = !web_on; 
                    tempPrefs.putBool("web_on", web_on);
                    tempPrefs.end();

                    if (web_on) {
                        Serial.println("🌐 [遙控指令] >>> 啟動 Wi-Fi AP 模式 <<<");
                        WiFi.mode(WIFI_AP);
                        #if ENABLE_WEB_DASHBOARD
                        Web_Manager::init();
                        #endif
                    } else {
                        Serial.println("🛡️ [遙控指令] >>> 徹底殺死 Wi-Fi 晶片，進入極致競技模式 <<<");
                        WiFi.disconnect(true);
                        WiFi.mode(WIFI_OFF); 
                    }

                    uint16_t n = historyPackets[6][11] | (historyPackets[6][12] << 8);
                    if (n >= 1 && n <= 50) {
                        int idx = n - 1;                           
                        int target_packet = idx / 8;               
                        int byte_offset = 1 + (idx % 8) * 2;       

                        uint16_t origSP = historyPackets[target_packet][byte_offset] | 
                                          (historyPackets[target_packet][byte_offset + 1] << 8);

                        uint16_t officialHistory[8];
                        uint8_t histCount = 0;
                        for (int i = 0; i < 8; i++) {
                            int curr_n = n - i;
                            if (curr_n <= 0) curr_n += 50; 
                            int slot_idx = curr_n - 1;
                            int tp = slot_idx / 8;
                            int offset = 1 + (slot_idx % 8) * 2;
                            uint16_t sp = historyPackets[tp][offset] | (historyPackets[tp][offset + 1] << 8);
                            if (sp > 0 && sp < 20000) { 
                                officialHistory[histCount++] = sp;
                            }
                        }
                        
                        if (example_lvgl_lock(100)) {
                            UI::updateOfficialData(origSP, officialHistory, histCount);
                            example_lvgl_unlock();
                        }

                        #if ENABLE_WEB_DASHBOARD
                        if (web_on) {
                            Web_Manager::broadcastOfficialHistory(origSP, officialHistory, histCount);
                        }
                        #endif
                    }
                }
            }
        }
        else if (header >= 0xB0 && header <= 0xB6) {
            int packet_index = header - 0xB0; 
            
            memset(historyPackets[packet_index], 0, 20);
            memcpy(historyPackets[packet_index], pData, length < 20 ? length : 20);

            if (header == 0xB6) {
                uint16_t n = historyPackets[6][11] | (historyPackets[6][12] << 8);

                if (n >= 1 && n <= 50) {
                    int idx = n - 1;                           
                    int target_packet = idx / 8;               
                    int byte_offset = 1 + (idx % 8) * 2;       

                    uint16_t origSP = historyPackets[target_packet][byte_offset] | 
                                      (historyPackets[target_packet][byte_offset + 1] << 8);

                    uint16_t officialHistory[8];
                    uint8_t histCount = 0;
                    
                    for (int i = 0; i < 8; i++) {
                        int curr_n = n - i;
                        if (curr_n <= 0) curr_n += 50; 
                        
                        int curr_idx = curr_n - 1;
                        int tp = curr_idx / 8;
                        int offset = 1 + (curr_idx % 8) * 2;
                        
                        uint16_t sp = historyPackets[tp][offset] | (historyPackets[tp][offset + 1] << 8);
                        if (sp > 0 && sp < 20000) { 
                            officialHistory[histCount++] = sp;
                        }
                    }

                    #if ENABLE_WEB_DASHBOARD
                    Web_Manager::broadcastOfficialHistory(origSP, officialHistory, histCount);
                    #endif

                    if (example_lvgl_lock(100)) {
                        UI::updateOfficialData(origSP, officialHistory, histCount);
                        example_lvgl_unlock();
                    }
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
                uint16_t rawTick = pData[i] | (pData[i+1] << 8);
                if (rawTick > 0 && liveCurveCount < 32) {
                    uint32_t rpm = 7500000UL / rawTick;
                    if (rpm > 500 && rpm < 18000) {
                        liveCurveBuffer[liveCurveCount++] = (uint16_t)rpm;
                    }
                }
            }

            if (header == 0x73) {
                UI::readyToDraw = true; 
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
        pBLEScan->setInterval(100);
        pBLEScan->setWindow(50); 
        NimBLEDevice::setPower(ESP_PWR_LVL_P3);
    }

    void connectTask() {
        if (!isSystemEnabled) return;

        if (!isConnected) current_ui_state = 0; 
        else if (!isBeyInstalled) current_ui_state = 1; 
        else current_ui_state = 2;

        if (current_ui_state != last_ui_state) {
            last_ui_state = current_ui_state;
            if (example_lvgl_lock(100)) {
                UI::updateStatus(current_ui_state);
                example_lvgl_unlock();
            }
            
            #if ENABLE_WEB_DASHBOARD
            Preferences tempPrefs;
            tempPrefs.begin("bx09_store", true);
            bool web_on = tempPrefs.getBool("web_on", false);
            tempPrefs.end();
            if (web_on) {
                Web_Manager::broadcastStatus(isConnected, isBeyInstalled);
            }
            #endif
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
                Serial.println(">>> 藍牙連線成功！優化通訊頻寬 (7.5ms - 15ms) <<<");
                client->updateConnParams(6, 12, 0, 100);
                NimBLEDevice::setPower(ESP_PWR_LVL_P9);
                
                auto services = client->getServices(true);
                if (!services.empty()) {
                    for (auto* svc : services) {
                        auto chars = svc->getCharacteristics(true); 
                        if (!chars.empty()) {                       
                            for (auto* chr : chars) {               
                                if (chr->canNotify()) {
                                    chr->subscribe(true, notifyCallback, false);
                                }
                                
                                if (chr->canRead()) {
                                    std::string val = chr->readValue();
                                    if (val.length() > 0) {
                                        uint8_t header = (uint8_t)val[0];
                                        if (header >= 0xB0 && header <= 0xB6) {
                                            notifyCallback(chr, (uint8_t*)val.data(), val.length(), false);
                                        }
                                    }
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
} // namespace BLE_Manager

void ClientCallback::onDisconnect(NimBLEClient* pClient, int reason) {
    BLE_Manager::isConnected = false;
    BLE_Manager::isBeyInstalled = false;
    BLE_Manager::current_ui_state = 0;
    Serial.printf("!!! BX-09 斷開連線 (Reason: %d) ...\n", reason);
    
    #if ENABLE_WEB_DASHBOARD
    Preferences tempPrefs;
    tempPrefs.begin("bx09_store", true);
    bool web_on = tempPrefs.getBool("web_on", false);
    tempPrefs.end();
    if (web_on) {
        Web_Manager::broadcastStatus(false, false);
    }
    #endif
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