#include <Arduino_GFX_Library.h>
#include <BLEDevice.h>
#include <BLEUtils.h>

// ==========================================
// [模組 0] 全域硬體設定與常數
// ==========================================
#define BX09_MAC "da:c4:51:04:58:86"
#define LCD_BL 14  // 螢幕背光引腳
// Standard 16-bit Color Definitions
#define BLACK    0x0000
#define BLUE     0x001F
#define RED      0xF800
#define GREEN    0x07E0
#define CYAN     0x07FF
#define MAGENTA  0xF81F
#define YELLOW   0xFFE0
#define WHITE    0xFFFF
// ==========================================
// [模組 1] 物理運算器 (Physics Engine)
// 負責處理原始數據、過濾雜訊與反比例運算
// ==========================================
namespace Physics {
    const uint32_t CALIBRATION_K = 7500000; 
    uint16_t buffer[30]; 
    int count = 0;
    float peak_rpm = 0;
    float avg_rpm = 0;

    // --- 新增：歷史紀錄與最高紀錄狀態 ---
    float allTimePeak = 0;     // 生涯最高紀錄
    float history[8] = {0};    // 儲存最近8次的陣列
    int historyCount = 0;      // 目前有幾筆歷史紀錄

    void reset() {
        count = 0;
        peak_rpm = 0;
        avg_rpm = 0;
    }

    void addData(uint16_t val) {
        if (val > 500 && count < 30) {
            buffer[count++] = val;
        }
    }

    bool calculate() {
        if (count == 0) return false;

        uint16_t min_raw = 65535;
        uint32_t sum_raw = 0;

        for (int j = 0; j < count; j++) {
            if (buffer[j] < min_raw) min_raw = buffer[j];
            sum_raw += buffer[j];
        }

        peak_rpm = (float)CALIBRATION_K / min_raw;
        avg_rpm = (float)CALIBRATION_K / ((float)sum_raw / count);

        // --- 新增：更新歷史紀錄 (將舊數據往後推) ---
        for (int i = 7; i > 0; i--) {
            history[i] = history[i-1];
        }
        history[0] = peak_rpm; // 將最新一次紀錄放在最上面 [0]
        if (historyCount < 8) historyCount++;

        // --- 新增：更新生涯最高轉速 ---
        if (peak_rpm > allTimePeak) {
            allTimePeak = peak_rpm;
        }

        return true;
    }
}
namespace Power {
    const int BAT_ADC_PIN = 1; // Waveshare 1.9" 的電池檢測引腳
    
    float getVoltage() {
        // 讀取 ADC (0-4095) 並轉換為實際電壓
        int raw = analogRead(BAT_ADC_PIN);
        // 1.21 是典型的校準常數，根據你的板子精確度可能需要微調
        float voltage = (raw / 4095.0) * 3.3 * 2.0 * 1.03; 
        return voltage;
    }

    int getPercentage() {
        float v = getVoltage();
        // 簡單的線性映射：4.2V=100%, 3.4V=0%
        int percent = (v - 3.4) / (4.2 - 3.4) * 100;
        if (percent > 100) percent = 100;
        if (percent < 0) percent = 0;
        return percent;
    }
}
// ==========================================
// [模組2] UI 渲染器 - 修正版
// ==========================================
namespace UI {
    // 根據 Hello World 範例修正引腳：DC=7, CS=6, WR=8, RD=9
    Arduino_DataBus *bus = new Arduino_HWSPI(11 /* DC */, 12 /* CS */, 10 /* SCK */, 13 /* MOSI */);

    // 根據 Hello World 修正：RST=5, BL=15
    // 注意：Waveshare 1.9" 通常需要開啟 IPS (true) 並且設置正確的 Offset
    Arduino_GFX *gfx = new Arduino_ST7789(bus, 9 /* RST */,1 /*rotation*/,0/*IPS屏*/,170/*w*/,320/*h*/,35/*起始列偏移（左边）*/,0/*起始行偏移（上边）*/,35/*结束列偏移（右边）*/,0/*结束行偏移（下边）*/);


    const int BACKLIGHT_PIN = 15; 

    // 自定義一些不刺眼的顏色
    #define COLOR_DARKGREY 0x39E7
    #define COLOR_LIGHTGREY 0xC618
    #define COLOR_ORANGE 0xFDA0

    void init() {
        pinMode(BACKLIGHT_PIN, OUTPUT);
        digitalWrite(BACKLIGHT_PIN, HIGH); 
        if (!gfx->begin()) Serial.println("UI: GFX 初始化失敗！");
        gfx->fillScreen(BLACK);
    }

    void showSearching() {
        gfx->fillScreen(BLACK);
        gfx->setTextColor(WHITE);
        gfx->setTextSize(2);
        gfx->setCursor(20, 70);
        gfx->println("Searching BX-09...");
    }

    void showReady() {
        gfx->fillScreen(BLACK);
        // 如果你有加入 Power 模組，可以在這裡顯示電量
        gfx->setTextColor(GREEN);
        gfx->setTextSize(3);
        gfx->setCursor(20, 60);
        gfx->println("READY TO LAUNCH");
    }

    // --- 修改：接收最高轉速與歷史紀錄陣列 ---
    void showResults(float currentPeak, float allTimeMax, float* historyArray, int hCount) {
        gfx->fillScreen(BLACK);
        
        // ==========================================
        // 左半邊排版 (X座標 0~170)
        // ==========================================
        gfx->setTextColor(YELLOW);
        gfx->setTextSize(2);
        gfx->setCursor(10, 20);
        gfx->println("MAX POWER");
        
        gfx->setTextColor(WHITE);
        // 字體從 7 縮小到 6，避免四位數 RPM 擠到右邊的線
        gfx->setTextSize(6); 
        gfx->setCursor(10, 50);
        gfx->printf("%.0f", currentPeak);
        
        // 將原本的 AVG 替換為最高紀錄 BEST
        gfx->setTextSize(2);
        gfx->setCursor(10, 130);
        gfx->setTextColor(CYAN);
        gfx->printf("BEST: %.0f", allTimeMax); 

        // ==========================================
        // 右半邊排版 (X座標 180~320)
        // ==========================================
        // 畫一條垂直分隔線
        gfx->drawLine(180, 10, 180, 160, COLOR_DARKGREY); 
        
        gfx->setTextColor(COLOR_ORANGE);
        gfx->setTextSize(1);
        gfx->setCursor(190, 15);
        gfx->println("RECENT LAUNCHES");

        gfx->setTextSize(2);
        for (int i = 0; i < hCount; i++) {
            int y_pos = 35 + (i * 16); // 計算每行的高度 (間距 16px)
            
            // 最新的一次用綠色高亮，以前的用灰色
            if (i == 0) {
                gfx->setTextColor(GREEN); 
            } else {
                gfx->setTextColor(COLOR_LIGHTGREY); 
            }
            
            gfx->setCursor(190, y_pos);
            gfx->printf("%d. %.0f", i + 1, historyArray[i]);
        }
    }
}
// ==========================================
// [模組 3] BLE 監聽器 (BLE Manager)
// 負責連線、斷線重連、以及接收 Notify 數據
// ==========================================
namespace BLE_Manager {
    BLEClient* client = nullptr;
    bool isConnected = false;

    // 藍牙接收回調函數
    static void notifyCallback(BLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
        if (length == 0) return;
        uint8_t header = pData[0];

        // 狀態判斷
        if (header == 0xA0) {
            if (length >= 4 && pData[3] == 0x04) {
                Serial.println("\n[狀態] 🟢 陀螺已安裝");
                Physics::reset();  // 呼叫物理模組重置數據
                UI::showReady();   // 呼叫 UI 模組更新畫面
            } 
        }
        // 威力曲線數據接收
        else if (header >= 0x71 && header <= 0x73) {
            for (int i = 1; i < (int)length - 1; i += 2) {
                uint16_t val = pData[i] | (pData[i+1] << 8);
                Physics::addData(val); // 交給物理模組處理
            }

            // 結算封包
            // ... (前面的 BLE 代碼不變)
            // 結算封包
            if (header == 0x73) {
                if (Physics::calculate()) { 
                    Serial.println("\n=========================================");
                    Serial.printf("🔥 最高: %.0f | 👑 生涯最高: %.0f\n", Physics::peak_rpm, Physics::allTimePeak);
                    Serial.println("=========================================\n");
                    
                    // --- 修改：傳遞當前轉速、最高紀錄、歷史陣列給 UI ---
                    UI::showResults(Physics::peak_rpm, Physics::allTimePeak, Physics::history, Physics::historyCount); 
                }
            }
        // ... (後面的 BLE 代碼不變)
        }
    }

    class ClientCallback : public BLEClientCallbacks {
        void onDisconnect(BLEClient* pclient) {
            isConnected = false;
            Serial.println("!!! BX-09 斷開連線 ...");
        }
    };

    void init() {
        BLEDevice::init("ESP32_BEY_SNIFFER");
    }

    void connectTask() {
        if (!isConnected) {
            UI::showSearching();
            if (client != nullptr) delete client;
            
            client = BLEDevice::createClient();
            client->setClientCallbacks(new ClientCallback());

            if (client->connect(BLEAddress(BX09_MAC))) {
                isConnected = true;
                Serial.println(">>> 藍牙連線成功！掛載監聽器... <<<");

                std::map<std::string, BLERemoteService*>* services = client->getServices();
                for (auto const& sPair : *services) {
                    auto chars = sPair.second->getCharacteristics();
                    for (auto const& cPair : *chars) {
                        if (cPair.second->canNotify()) {
                            cPair.second->registerForNotify(notifyCallback);
                        }
                    }
                }
                Serial.println(">>> 準備就緒！請安裝陀螺 <<<");
                UI::showReady();
            } else {
                delay(3000); // 失敗則等待 3 秒再試
            }
        }
    }
}

// ==========================================
// 主程式入口 (Main Setup & Loop)
// ==========================================
void setup() {
    Serial.begin(115200);
    unsigned long start = millis();
    while (!Serial && millis() - start < 3000); 

    Serial.println(">>> BX-09 OS 啟動 <<<");

    // 初始化各個模組
    UI::init();
    BLE_Manager::init();
}

void loop() {
    // 主迴圈只負責維持藍牙連線狀態，所有動作交由 Notify 回調驅動
    BLE_Manager::connectTask();
    delay(10); 
}


