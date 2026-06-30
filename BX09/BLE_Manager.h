#pragma once
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>



// ==========================================
// [模組 3] BLE 監聽器 (防崩潰與三段燈號最終版)
// ==========================================
namespace BLE_Manager {
    BLEClient* client = nullptr;
    BLEScan* pBLEScan = nullptr;
    BLEAddress* targetAddress = nullptr; 
    // 🟢 加上 volatile 關鍵字，防止雙核心快取不同步
    volatile bool isConnected = false;
    volatile bool isBeyInstalled = false;
    // 🟢 新增：藍牙系統的「總開關」旗標，預設為開啟
    volatile bool isSystemEnabled = true;
    //bool isConnected = false;
    bool doConnect = false;
    bool isScanning = false; 

    // 分離 UI 狀態與陀螺狀態
    //bool isBeyInstalled = false;
    int current_ui_state = 0;
    int last_ui_state = -1;
    // 🟢 新增：一鍵切換藍牙狀態的專屬函式
    void toggleBluetooth() {
        isSystemEnabled = !isSystemEnabled; // 狀態反轉
        
        if (!isSystemEnabled) {
            Serial.println("🛑 [系統] 藍牙雷達已手動休眠");
            
            // 1. 強制停止掃描
            if (isScanning && pBLEScan != nullptr) {
                pBLEScan->stop();
                isScanning = false;
            }
            
            // 2. 如果連線中，強制斷開
            if (client != nullptr && isConnected) {
                client->disconnect();
            }
            
            // 3. 狀態歸零
            isConnected = false;
            doConnect = false;
            isBeyInstalled = false;
            
            // 4. 通知 UI 顯示第四種狀態 (休眠模式)
            current_ui_state = 3; 
            if (current_ui_state != last_ui_state) {
                last_ui_state = current_ui_state;
                UI::updateStatus(current_ui_state);
                lv_timer_handler(); // 強制立刻刷新畫面
            }
            
        } else {
            Serial.println("📡 [系統] 藍牙雷達重新啟動");
            // 將狀態設為 0 (紅燈)，主迴圈的 connectTask 會自動把它接手並重新啟動雷達
            current_ui_state = 0; 
            last_ui_state = -1; // 強制下次 UI 更新
        }
    }
    // 接收數據 (絕對禁止在這裡直接呼叫 UI 繪圖！)
    static void notifyCallback(BLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
        if (length == 0) return;
        uint8_t header = pData[0];

        if (header == 0xA0) {
            if (length >= 4 && pData[3] == 0x04) {
                Serial.println("\n[狀態] 🟢 陀螺已安裝");
                Physics::reset();  
                isBeyInstalled = true; // 只寫紙條，不碰方向盤
            } 
        }
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
                    UI::readyToDraw = true; // 寫紙條，由 Loop 處理繪圖
                }
            }
        }
    }

    class ClientCallback : public BLEClientCallbacks {
        void onDisconnect(BLEClient* pclient) {
            isConnected = false;
            isBeyInstalled = false; // 斷線時，連帶把陀螺狀態歸零
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
    // 🟢 1. 新增：掃描完成的回呼函式 (讓雷達進入背景非阻塞模式)
    void scanCompleteCB(BLEScanResults results) {
        // 雷達掃描結束會來到這裡，我們清空快取即可，不干擾主迴圈
        pBLEScan->clearResults();
    }
void connectTask() {
    // 🛑 新增：如果總電源被關閉，直接退出這個任務，讓 ESP32 徹底放空休息
        if (!isSystemEnabled) {
            return; 
        }
        // 🟢 1. 優先處理狀態機與 UI 同步 (搬到最上面！)
        if (!isConnected) {
            current_ui_state = 0; // 🔴 紅燈：斷線
        } else if (isConnected && !isBeyInstalled) {
            current_ui_state = 1; // 🟡 黃燈：已連線，等待安裝陀螺
        } else if (isConnected && isBeyInstalled) {
            current_ui_state = 2; // 🟢 綠燈：陀螺已安裝，準備就緒
        }

        // 如果狀態有改變，才去通知 LVGL 換燈號與文字
        if (current_ui_state != last_ui_state) {
            last_ui_state = current_ui_state;
            UI::updateStatus(current_ui_state); 
            
            // 🛑 核心防呆：強制螢幕立刻刷新！
            // (因為等一下雷達掃描會讓主迴圈暫停，必須在暫停前把紅燈畫出來)
            lv_timer_handler(); 
        }

        // 🟢 2. 斷線後安全重啟雷達機制 (黑洞區塊)
        if (!isConnected && !doConnect && !isScanning) {
            isScanning = true;
            Serial.println(">>> 啟動雷達！開始搜尋... <<<");
            pBLEScan->clearResults(); 
            // 這裡會進入「阻塞掃描模式」，直到掃到裝置才會解除暫停
            pBLEScan->start(0, scanCompleteCB, false); 
        }

        // 🟢 3. 連線機制
        if (doConnect) {
            doConnect = false;
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
                // 順手補上這行，連線失敗時讓下一個迴圈能重新掃描
                isScanning = false;
                lv_obj_invalidate(lv_scr_act());
            }
        }
    }
}