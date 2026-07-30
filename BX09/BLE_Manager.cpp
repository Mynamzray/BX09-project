#include "BLE_Manager.h"
#include "Web_Manager.h"

// STREAMING_CHUNK: 宣告 UI 命名空間與新函式
namespace UI {
    extern volatile bool readyToDraw;
    void updateStatus(int state);
    // 🟢 補上宣告，讓編譯器認識它
    void updateOfficialData(uint16_t origSP, uint16_t* history, int histCount); 
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

    // 🟢 新增：歷史成績封包快取 (B0 ~ B6，共 7 個封包)
    static uint8_t historyPackets[7][20];

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
                UI::updateStatus(current_ui_state);
                Web_Manager::broadcastStatus(isConnected, isBeyInstalled);
            }
        } else {
            Serial.println("📡 [系統] 藍牙雷達重新啟動");
            current_ui_state = 0; 
            last_ui_state = -1;
        }
    }

    // STREAMING_CHUNK: 解析藍牙封包核心邏輯
    void notifyCallback(NimBLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
        if (length == 0) return;
        uint8_t header = pData[0];

        if (header == 0xA0) {
            Serial.print("[BLE 系統狀態攔截] ");
            for (size_t i = 0; i < length; i++) { Serial.printf("%02X ", pData[i]); }
            Serial.println(); 

            // 🟢 攔截官方解答 (origSP) 的探測程式碼...
            if (length >= 8) { 
                // ... (保留你原本的 guess_1, guess_2 等等) ...
            }

            // 🟢 完美解析狀態：使用 Bitwise AND (&) 來獨立拆解陀螺與按鈕狀態！
            if (length >= 4) {
                uint8_t stateByte = pData[3];
                
                // 1. 拆解陀螺狀態 (只要 Bit 2 是 1 就是有扣上，不管是 0x04 還是 0x14)
                bool isAttachedNow = (stateByte & 0x04) > 0;
                
                // 2. 拆解按鈕雙擊狀態 (只要 Bit 4 是 1 就是模式 ON)
                bool isButtonModeOn = (stateByte & 0x10) > 0;

                // 處理陀螺插拔
                if (isAttachedNow && !isBeyInstalled) {
                    Serial.println("\n[狀態] 🟢 陀螺已安裝");
                    Physics::reset();  
                    isBeyInstalled = true; 
                } 
                else if (!isAttachedNow && isBeyInstalled) {
                    Serial.println("\n[狀態] 🟡 陀螺已手動拔除");
                    isBeyInstalled = false; 
                }

                // 處理實體按鈕雙擊事件 (可作為遙控器使用)
               static bool lastButtonMode = false;
                if (isButtonModeOn != lastButtonMode) {
                    lastButtonMode = isButtonModeOn;
                    Serial.printf("\n🕹️ [遙控器] 偵測到發射器實體按鈕雙擊！目前隱藏模式: %s\n", isButtonModeOn ? "ON (0x10)" : "OFF (0x00)");
                    
                    // 🟢 遙控功能：雙擊時，強制從 ESP32 記憶體快取中重繪歷史成績單！
                    // 從 B6 封包快取讀取最新指標 n
                    uint16_t n = historyPackets[6][11] | (historyPackets[6][12] << 8);
                    
                    if (n >= 1 && n <= 50) {
                        Serial.println("🔄 [遙控指令] 正在將歷史成績單推送到螢幕上...");
                        
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

                        // 🟢 觸發 UI 和網頁更新
                        Web_Manager::broadcastOfficialHistory(origSP, officialHistory, histCount);
                        UI::updateOfficialData(origSP, officialHistory, histCount);
                    } else {
                        Serial.println("⚠️ [遙控指令] 記憶體目前沒有歷史紀錄快取，無法繪製。請先發射一次！");
                    }
                }
            }
        }
        // 🟢 完美移植 Atlas 解析邏輯：攔截歷史紀錄封包 (B0 ~ B6)
        else if (header >= 0xB0 && header <= 0xB6) {
            int packet_index = header - 0xB0; // 轉換成 0~6 的陣列索引
            
            // 將收到的封包備份到我們的 2D 陣列中
            memset(historyPackets[packet_index], 0, 20);
            memcpy(historyPackets[packet_index], pData, length < 20 ? length : 20);

            // 藍牙通常是循序傳送的，當我們收到最後一包 B6 時，代表拼圖集齊了！
            if (header == 0xB6) {
                // B6 封包的 offset 11 與 12 記錄了「目前是最新 50 發裡面的第幾發 (n)」
                uint16_t n = historyPackets[6][11] | (historyPackets[6][12] << 8);

                // 防呆：發射次數 n 應該落在 1 ~ 50 之間
                if (n >= 1 && n <= 50) {
                    // 💡 安全的數學定位演算法 (修復了日本大神的 n=8 崩潰 Bug)
                    int idx = n - 1;                           // 將 1~50 轉換為 0~49 的陣列索引
                    int target_packet = idx / 8;               // 除以 8 決定在哪個封包 (B0~B6)
                    int byte_offset = 1 + (idx % 8) * 2;       // 計算在該封包內的起始 Byte (加 1 是為了跳過 Header)

                    // 把找到的兩個 Byte 組合起來，就是完美的官方分數！
                    uint16_t origSP = historyPackets[target_packet][byte_offset] | 
                                      (historyPackets[target_packet][byte_offset + 1] << 8);

                    Serial.println("\n==================================================");
                    Serial.printf("🏆 [官方成績攔截] BX-09 歷史總表讀取成功！\n");
                    Serial.printf("   👉 最新發射排序: 第 %d 發\n", n);
                    Serial.printf("   👉 官方晶片最終判定分數 (origSP): %d RPM\n", origSP);
                    Serial.println("==================================================\n");
                    // ==========================================
                    // 🟢 駭客級深潛：印出內部完整的 1~50 發歷史紀錄
                    // ==========================================
                    Serial.println("--------------------------------------------------");
                    Serial.println("📜 BX-09 內部快取：最近 50 發成績清單 (由新到舊)");
                    
                    for (int i = 0; i < 50; i++) {
                        // 倒推計算：從最新的 n 開始往回減
                        int curr_n = n - i;
                        
                        // 處理循環記憶體 (如果減到 0 以下，代表繞到陣列尾端了)
                        if (curr_n <= 0) {
                            curr_n += 50; 
                        }
                        
                        // 重新計算這個格子 (curr_n) 在封包裡的位置
                        int slot_idx = curr_n - 1;
                        int tp = slot_idx / 8;
                        int offset = 1 + (slot_idx % 8) * 2;
                        
                        // 讀取該格子的分數
                        uint16_t sp = historyPackets[tp][offset] | (historyPackets[tp][offset + 1] << 8);
                        
                        // 印出結果 (過濾掉 0 或是未初始化的亂碼 65535)
                        if (sp > 0 && sp < 20000) {
                            Serial.printf("   倒數第 %02d 發 [Slot %02d]: %5d RPM\n", i + 1, curr_n, sp);
                        } else {
                            Serial.printf("   倒數第 %02d 發 [Slot %02d]:  ---- (無資料)\n", i + 1, curr_n);
                        }
                    }
                    Serial.println("==================================================\n");

                    // 🟢 提取官方歷史陣列 (往前倒推最新 8 筆) 準備傳給網頁與螢幕
                    uint16_t officialHistory[8];
                    uint8_t histCount = 0;
                    
                    for (int i = 0; i < 8; i++) {
                        int curr_n = n - i;
                        
                        // 處理 1~50 的循環記憶體 (如果剛跨過 50，上一筆會是 50)
                        if (curr_n <= 0) curr_n += 50; 
                        
                        int curr_idx = curr_n - 1;
                        int tp = curr_idx / 8;
                        int offset = 1 + (curr_idx % 8) * 2;
                        
                        uint16_t sp = historyPackets[tp][offset] | (historyPackets[tp][offset + 1] << 8);
                        
                        // 剔除空數據 (0) 或是硬體未初始化的亂碼
                        if (sp > 0 && sp < 20000) { 
                            officialHistory[histCount++] = sp;
                        }
                    }

                    // 🟢 透過 WebSocket 傳送官方分數與歷史給 WebUI
                    Web_Manager::broadcastOfficialHistory(origSP, officialHistory, histCount);
                    
                    // 🟢 透過 LVGL 傳送官方分數與歷史給實體 LCD 螢幕
                    UI::updateOfficialData(origSP, officialHistory, histCount);
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
            // 讀取最純粹的物理轉速數據
            for (int i = 1; i < (int)length - 1; i += 2) {
                uint16_t val = pData[i] | (pData[i+1] << 8);
                Physics::addData(val); 
            }
            if (header == 0x73) {
                if (Physics::calculate()) { 
                    UI::readyToDraw = true; 
                }
            }
        }
    }

    // STREAMING_CHUNK: 設定 BLE 掃描與連線
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
        NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    }

    void connectTask() {
        if (!isSystemEnabled) return;

        if (!isConnected) current_ui_state = 0; 
        else if (!isBeyInstalled) current_ui_state = 1; 
        else current_ui_state = 2;

        if (current_ui_state != last_ui_state) {
            last_ui_state = current_ui_state;
            UI::updateStatus(current_ui_state);
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
Serial.println(">>> 藍牙連線成功！優化通訊頻寬 (7.5ms - 15ms) <<<");
                client->updateConnParams(6, 12, 0, 100);
                NimBLEDevice::setPower(ESP_PWR_LVL_P9);
                
                Serial.println(">>> 掛載資料監聽器與記憶體探測... <<<");
                
                auto services = client->getServices(true);
                if (!services.empty()) {
                    for (auto* svc : services) {
                        auto chars = svc->getCharacteristics(true); 
                        if (!chars.empty()) {                       
                            for (auto* chr : chars) {               
                                // 1. 註冊通知接收
                                if (chr->canNotify()) {
                                    chr->subscribe(true, notifyCallback, false);
                                }
                                
                                // 🟢 2. 駭客探測器：連線瞬間，強制讀取所有可讀取的特徵值！
                                if (chr->canRead()) {
                                    std::string val = chr->readValue();
                                    if (val.length() > 0) {
                                        Serial.printf("🔍 [連線探測] 讀取記憶體 %s -> ", chr->getUUID().toString().c_str());
                                        for(int i = 0; i < val.length(); i++) {
                                            Serial.printf("%02X ", (uint8_t)val[i]);
                                        }
                                        Serial.println();
                                        
                                        // 💡 如果運氣好，讀出來的剛好是 B0~B6 封包，我們直接手動把它塞給解碼器！
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
}
// STREAMING_CHUNK: 藍牙回呼函式實作
void ClientCallback::onDisconnect(NimBLEClient* pClient, int reason) {
    BLE_Manager::isConnected = false;
    BLE_Manager::isBeyInstalled = false;
    BLE_Manager::current_ui_state = 0;
    Serial.printf("!!! BX-09 斷開連線 (Reason: %d) ...\n", reason);
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
