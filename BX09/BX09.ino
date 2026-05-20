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
    uint16_t rawProf[32] = {0};
    int count = 0;
    
    float peak_rpm = 0;
    float avg_rpm = 0;

    float allTimePeak = 0;     
    float history[8] = {0};    
    int historyCount = 0;      

    // 👉 1. 將轉速陣列和圈數提升到這裡，讓 UI 可以讀取！
    uint16_t SP[32] = {0};
    uint16_t size = 0;

    void reset() {
        count = 0;
        memset(rawProf, 0, sizeof(rawProf));
        memset(SP, 0, sizeof(SP)); // 👉 記得清空
        size = 0;                  // 👉 記得清空
    }

    void addData(uint16_t val) {
        if (val == 0) return; // 拒絕空包彈
        if (count < 32) {
            rawProf[count++] = val;
        }
    }

bool calculate() {
    // ... 略 ...
    uint16_t T[32] = {0};
    uint16_t elapsedTime = 0;

    // 🟢 不能有 uint16_t，要直接對全域變數清空：
    memset(SP, 0, sizeof(SP));
    size = 0;

        Serial.println("\n=================[ BX-09 RAW PROFILE DATA ]=================");
        // 🟢 正確的重置語法：
    memset(T, 0, sizeof(T)); // 陣列用 memset 函式清空
    elapsedTime = 0;         // 單一變數直接 = 0 即可

    // (下面這段重複清空 SP 的動作可以保留或刪除，建議留著最上面的就好)
    memset(SP, 0, sizeof(SP));
    size = 0;

        // 1. 基本解碼
        for (int i = 0; i < count; i += 1) { 
            auto nRefs = rawProf[i];
            if (nRefs == 0) continue; 

            auto dt = static_cast<double>(nRefs) / 125.0;
            auto sp = static_cast<uint16_t>(60000.0 / dt);

            if (sp > 20000) continue;

            elapsedTime += static_cast<uint16_t>(dt);
            T[size] = elapsedTime;
            SP[size] = sp;
            size += 1;
        }


        // -------------------------------------------------------------------------
        // 2. 【防護 1：單點突波消除 (Glitch Filter)】
        // -------------------------------------------------------------------------
        // 如果轉速在 1 幀內瞬間飆升超過前後的 1.5 倍，判定為感測器讀取錯誤，將其削平
        for (int i = 1; i < size - 1; i++) {
            if (SP[i] > SP[i-1] * 1.5 && SP[i] > SP[i+1] * 1.5) {
                uint16_t smoothed = (SP[i-1] + SP[i+1]) / 2;
                Serial.printf("⚠️ [Glitch Filter] Turn %02d Spike (%d RPM) smoothed to %d RPM.\n", i + 1, SP[i], smoothed);
                SP[i] = smoothed;
            }
        }

        // -------------------------------------------------------------------------
        // 3. 【防護 2：尋找最高點與防回捲 (Peak Scan & Recoil Cutoff)】
        // -------------------------------------------------------------------------
        uint16_t trueMax = 0;
        int peakIndex = 0;
        String stopReason = "END_OF_DATA";

        for (int i = 0; i < size; i++) {
            // 如果轉速跌落谷底 (<1000) 且已經過了前幾圈，代表拉線動作已結束，齒輪開始空轉或回捲
            // 直接中斷掃描，拒絕後面的任何假高點
            if (SP[i] < 1000 && i >= 3) {
                stopReason = "RECOIL_CUTOFF (Speed < 1000)";
                break; 
            }
            
            if (SP[i] > trueMax) {
                trueMax = SP[i];
                peakIndex = i;
            }
        }

        // -------------------------------------------------------------------------
        // 4. 印出最終分析報告
        // -------------------------------------------------------------------------
        Serial.println("\n--- [Step 2] Algorithm Decision Summary ---");
        Serial.printf("Stop Reason     : %s\n", stopReason.c_str());
        Serial.printf("Final True Peak : %d RPM (Found at Turn %d)\n", trueMax, peakIndex + 1);
        Serial.println("============================================================\n");

        // -------------------------------------------------------------------------
        // 5. 儲存結果並釋放記憶體
        // -------------------------------------------------------------------------
        if (trueMax > 0) {
            peak_rpm = trueMax;
            
            float sum_sp = 0;
            for (int i = 0; i < size; i++) sum_sp += SP[i];
            avg_rpm = size > 0 ? (sum_sp / size) : 0;

            for (int i = 7; i > 0; i--) {
                history[i] = history[i-1];
            }
            history[0] = peak_rpm; 
            if (historyCount < 8) historyCount++;

            if (peak_rpm > allTimePeak) {
                allTimePeak = peak_rpm;
            }
        }

        count = 0;
        memset(rawProf, 0, sizeof(rawProf));

        return true;
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
    Arduino_GFX *gfx = new Arduino_ST7789(bus, 9 /* RST */,1 /*rotation*/,1/*IPS屏*/,170/*w*/,320/*h*/,35/*起始列偏移（左边）*/,0/*起始行偏移（上边）*/,35/*结束列偏移（右边）*/,0/*结束行偏移（下边）*/);


   // 1. 獨立顏色定義 (RGB565)
    #define GFX_BLACK     0x0000
    #define GFX_WHITE     0xFFFF
    #define GFX_GREEN     0x07E0
    #define GFX_YELLOW    0xFFE0
    #define GFX_CYAN      0x07FF
    #define GFX_DARKGREY  0x4208
    #define GFX_LIGHTGREY 0xC618
    #define GFX_ORANGE    0xFD20
    #define GFX_RED       0xF800

    // 2. 螢幕佈局常數
    const int SCREEN_W = 320;
    const int SCREEN_H = 170;
    const int LEFT_PANEL_W = 110;  
    const int CHART_X_START = 125; 
    const int CHART_Y_START = 145; 
    const int CHART_W = 180;       
    const int CHART_H = 120;       

    // 👉 新增：記憶目前的藍牙狀態
    bool currentBleState = false; 
    volatile bool readyToDraw = false;
    // 提前宣告，讓 init() 可以認識它
    void updateStatus(bool isConnected);
    void showResults(uint16_t currentPeak, uint16_t allTimePeak, float history[], int histCount);

    // 3. 唯一的初始化功能
    void init() {
        if (gfx) {
            gfx->begin();
            gfx->setRotation(1); 
            gfx->fillScreen(GFX_BLACK);
        }
        // 👉 開機馬上畫出「初始狀態」的儀表板 (讀取 Physics 的預設 0 值)
        showResults(Physics::peak_rpm, Physics::allTimePeak, Physics::history, Physics::historyCount);
        
        // 預設為未連線 (亮紅燈)
        updateStatus(false);
    }

    // 4. 藍牙狀態燈
    void updateStatus(bool isConnected) {
        currentBleState = isConnected; // 記住狀態
        if (!gfx) return;
        gfx->fillCircle(310, 10, 4, isConnected ? GFX_GREEN : GFX_RED);
    }

    // 5. 核心折線圖與看板繪製邏輯
    void drawDashboard(uint16_t currentPeak, uint16_t allTimePeak, float history[], int histCount, uint16_t turnData[], int turnCount) {
        if (!gfx) return;
        
        gfx->fillScreen(GFX_BLACK);

        // ==========================================
        // 左邊面板：數據區
        // ==========================================
        gfx->drawLine(LEFT_PANEL_W, 0, LEFT_PANEL_W, SCREEN_H, GFX_DARKGREY); 

        gfx->setTextColor(GFX_GREEN); gfx->setTextSize(1);
        gfx->setCursor(5, 5); gfx->print("Current RPM");
        gfx->setTextColor(GFX_WHITE); gfx->setTextSize(2); 
        gfx->setCursor(5, 18); gfx->print(currentPeak);

        gfx->setTextColor(GFX_YELLOW); gfx->setTextSize(1);
        gfx->setCursor(5, 42); gfx->print("All-Time Best");
        gfx->setTextColor(GFX_WHITE); gfx->setTextSize(2);
        gfx->setCursor(5, 55); gfx->print(allTimePeak);

        gfx->setTextColor(GFX_CYAN); gfx->setTextSize(1);
        gfx->setCursor(5, 82); gfx->print("Recent Hist.");
        
        gfx->setTextColor(GFX_LIGHTGREY); gfx->setTextSize(1); 
        int histToShow = (histCount > 5) ? 5 : histCount;
        for (int i = 0; i < histToShow; i++) {
            gfx->setCursor(5, 98 + (i * 14));
            gfx->print(i + 1); gfx->print(". "); gfx->print((int)history[i]);
        }

// ==========================================
        // 右邊面板：轉速折線圖
        // ==========================================
        gfx->drawLine(CHART_X_START, CHART_Y_START, CHART_X_START + CHART_W, CHART_Y_START, GFX_WHITE); 
        gfx->drawLine(CHART_X_START, CHART_Y_START, CHART_X_START, CHART_Y_START - CHART_H, GFX_WHITE); 

        if (turnCount >= 2) { 
            uint16_t maxRpmInChart = 0;
            for (int i = 0; i < turnCount; i++) {
                if (turnData[i] > maxRpmInChart) maxRpmInChart = turnData[i];
            }
            maxRpmInChart = maxRpmInChart * 1.1; 
            if (maxRpmInChart < 1000) maxRpmInChart = 1000; 

            int prevX = 0, prevY = 0;
            for (int i = 0; i < turnCount; i++) {
                int x = CHART_X_START + (i * (CHART_W / (turnCount - 1)));
                int y = map(turnData[i], 0, maxRpmInChart, CHART_Y_START, CHART_Y_START - CHART_H);

                // 🟢 橘色折線依然「每點都連」，確保轉速曲線是完整的
                if (i > 0) {
                    gfx->drawLine(prevX, prevY, x, y, GFX_ORANGE); 
                }

                // 🟢 【關鍵修改】：只有觸發 T1, T6, T12... 的點，才畫紅色圓點與印數字
                if (i == 0 || (i + 1) % 6 == 0) {
                    
                    // 👉 把紅色圓點移到這裡！只有數字旁邊才會出現波波
                    gfx->fillCircle(x, y, 3, GFX_RED);

                    gfx->setTextColor(GFX_WHITE); gfx->setTextSize(1);
                    
                    // 1. 印出點位上方的轉速數字
                    if (y < CHART_Y_START - CHART_H + 15) {
                         gfx->setCursor(x - 12, y + 6); 
                    } else {
                         gfx->setCursor(x - 12, y - 12);
                    }
                    gfx->print(turnData[i]);

                    // 2. 印出底部的 T 標籤
                    gfx->setCursor(x - 10, CHART_Y_START + 6);
                    gfx->printf("T%d", i + 1);
                }
                
                prevX = x; prevY = y;
            }
            
        } else if (turnCount == 0) {
            // 👉 新增：開機時或還沒發射時，在圖表區顯示提示
            gfx->setTextColor(GFX_DARKGREY); gfx->setTextSize(1);
            gfx->setCursor(CHART_X_START + 30, CHART_Y_START - (CHART_H / 2));
            gfx->print("Waiting for Launch...");
        }

        // 👉 最關鍵：畫完所有東西後，把藍牙狀態燈「補」回來！
        updateStatus(currentBleState);
    }

    // 6. BLE_Manager 呼叫的接口
    void showResults(uint16_t currentPeak, uint16_t allTimePeak, float history[], int histCount) {
        drawDashboard(currentPeak, allTimePeak, history, histCount, Physics::SP, Physics::size);
    }
    // 👉 新增：讓主程式 loop 呼叫的檢查更新函式
    void handleUpdate() {
        if (readyToDraw) {
            readyToDraw = false; // 立刻放下旗標
            // 在這裡執行真正耗時的繪圖
            showResults(Physics::peak_rpm, Physics::allTimePeak, Physics::history, Physics::historyCount);
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
                UI::updateStatus(true);  // 點亮綠燈  // 呼叫 UI 模組更新畫面
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
                    
                    // 🛑 移除原本的直接繪圖，改為舉起旗標，釋放藍牙執行緒！
                    UI::readyToDraw = true; 
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
            UI::updateStatus(false);
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
                UI::updateStatus(true);
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
    // 讓藍牙連線工作繼續執行
    BLE_Manager::connectTask();
    
    // 👉 每一輪循環都檢查有沒有新的發射結果需要繪製
    // 這樣繪圖就會在主執行緒進行，完全不會卡到藍牙接收！
    UI::handleUpdate();

    delay(10); // 微調留給系統的呼吸時間
}
