// ==========================================
// 1. LVGL 核心與驅動 (⚠️ 必須放在絕對頂部)
// ==========================================
#include <lvgl.h>
#include "user_config.h"
#include "lvgl_port.h"
#include "lcd_bl_pwm_bsp.h"
#include <Preferences.h> // 🟢 引入 ESP32 專用的記憶庫
Preferences prefs;       // 宣告一個 Preferences 物件
uint16_t global_all_time_best = 0; // 用來在程式中跑的歷史最高轉速
// 🟢 新增：UI 專屬的斷電記憶歷史陣列 (最多存 8 筆)
uint16_t global_history[8] = {0}; 
int global_hist_count = 0;
// ==========================================
// 2. Arduino 與你的藍牙函式庫
// ==========================================
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

// ==========================================
// [模組 0] 全域硬體設定與常數
// ==========================================
#define BX09_MAC "da:c4:51:04:58:86"
// 這裡不需要原本的顏色定義了，因為 LVGL 有自己的調色盤 (lv_palette)

// ==========================================
// [模組 1] 物理運算器 (Physics Engine)
// 負責處理原始數據、過濾雜訊與反比例運算
// (⚠️ 完全保留你的原汁原味，沒有更動)
// ==========================================

namespace Physics {
    uint16_t rawProf[32] = {0};
    int count = 0;
    
    float peak_rpm = 0;
    float avg_rpm = 0;

    float allTimePeak = 0;     
    float history[8] = {0};    
    int historyCount = 0;      

    uint16_t SP[32] = {0};
    uint16_t size = 0;

    void reset() {
        count = 0;
        memset(rawProf, 0, sizeof(rawProf));
        memset(SP, 0, sizeof(SP)); 
        size = 0;                  
    }

    void addData(uint16_t val) {
        if (val == 0) return; 
        if (count < 32) {
            rawProf[count++] = val;
        }
    }

    bool calculate() {
        uint16_t T[32] = {0};
        uint16_t elapsedTime = 0;

        memset(SP, 0, sizeof(SP));
        size = 0;

        Serial.println("\n=================[ BX-09 RAW PROFILE DATA ]=================");
        memset(T, 0, sizeof(T)); 
        elapsedTime = 0;         

        memset(SP, 0, sizeof(SP));
        size = 0;

        // ==========================================
        // 1. 基本解碼
        // ==========================================
        for (int i = 0; i < count; i += 1) { 
            auto nRefs = rawProf[i];
            if (nRefs == 0) continue; 

            auto dt = static_cast<double>(nRefs) / 125.0;
            auto sp = static_cast<uint16_t>(60000.0 / dt);

            // 🟢 【重要修正】：拔除直接 continue 的機制，把極端值保留在陣列原位，
            // 這樣前輩的「線性補差法」才能正確抓到前後時間點的數值！
            elapsedTime += static_cast<uint16_t>(dt);
            T[size] = elapsedTime;
            SP[size] = sp;
            size += 1;
        }

        // ==========================================
        // 2. 前輩的實戰演算法 (Sempai's Filter)
        // ==========================================
        // 步驟 2-1: 單點高於 25000 RPM 直接丟棄 (標記為 0，稍後補差)
        for (int i = 0; i < size; i++) {
            if (SP[i] > 25000) {
                SP[i] = 0; 
                Serial.printf("⚠️ 觸發前輩規則 1: 點 %d 測得異常超高轉速，準備進行線性補差\n", i);
            }
        }

        // 步驟 2-2: 前後超過 5000 RPM，用「線性補差法」取代
        for (int pass = 0; pass < 2; pass++) { 
            for (int i = 1; i < size - 1; i++) {
                if (SP[i] == 0 || abs(SP[i] - SP[i-1]) > 5000 || abs(SP[i] - SP[i+1]) > 5000) {
                    uint16_t interpolated = (SP[i-1] + SP[i+1]) / 2;
                    if (interpolated > 0 && interpolated < 25000) {
                        SP[i] = interpolated;
                    }
                }
            }
        }

        // 步驟 2-3: 邊界防護 
        if (size > 1) {
            if (abs(SP[0] - SP[1]) > 5000) SP[0] = SP[1];
            if (abs(SP[size-1] - SP[size-2]) > 5000) SP[size-1] = SP[size-2];
        }

        // ==========================================
        // 3. 清除突波後，尋找峰值與下降曲線
        // ==========================================
        uint16_t trueMax = 0;
        int peakIndex = 0;
        String stopReason = "END_OF_DATA";

        for (int i = 0; i < size; i++) {
            if (SP[i] < 1000 && i >= 3) {
                stopReason = "RECOIL_CUTOFF (Speed < 1000)";
                break; 
            }
            if (SP[i] > trueMax) {
                trueMax = SP[i];
                peakIndex = i;
            }
        }

        // ==========================================
        // 4. 印出最終分析報告
        // ==========================================
        Serial.println("\n--- [Step 2] Algorithm Decision Summary ---");
        Serial.printf("Stop Reason     : %s\n", stopReason.c_str());
        Serial.printf("Final True Peak : %d RPM (Found at Turn %d)\n", trueMax, peakIndex + 1);
        Serial.println("============================================================\n");

        // ==========================================
        // 5. 儲存結果並釋放記憶體
        // ==========================================
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
// [模組 2] LVGL 渲染器 (橫向超寬儀表板版)
// ==========================================
// 🟢 強行宣告 LVGL 內建字體，繞過 lv_conf.h 的捉迷藏
// extern const lv_font_t lv_font_montserrat_24;
// extern const lv_font_t lv_font_montserrat_14;
namespace UI {
    volatile bool readyToDraw = false;

    // LVGL 物件指標
    lv_obj_t * scr;
    lv_obj_t * label_status;
    lv_obj_t * led_status;
    lv_obj_t * label_current_rpm;
    lv_obj_t * label_all_time_rpm;
    lv_obj_t * label_history;
    lv_obj_t * chart;
    lv_chart_series_t * chart_series;

    // 1. 初始化介面
void init() {
        lvgl_port_init();
        lcd_bl_pwm_bsp_init(LCD_PWM_MODE_255);

        lv_obj_t * scr = lv_scr_act();
        
        // 🎨 1. 絕對純黑主題
        lv_obj_set_style_bg_color(scr, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_text_color(scr, lv_color_white(), LV_PART_MAIN);

        // 📊 2. 左欄：實時數據區 (X 座標 30)
        led_status = lv_led_create(scr);
        lv_obj_align(led_status, LV_ALIGN_TOP_LEFT, 30, 30);
        lv_obj_set_size(led_status, 15, 15);
        lv_led_set_color(led_status, lv_palette_main(LV_PALETTE_RED));
        lv_led_on(led_status);

        label_status = lv_label_create(scr);
        lv_label_set_text(label_status, "BLE DISCONNECTED");
        lv_obj_align(label_status, LV_ALIGN_TOP_LEFT, 55, 28);
        lv_obj_set_style_text_color(label_status, lv_palette_main(LV_PALETTE_GREY), 0);
        lv_obj_set_style_text_font(label_status, &lv_font_montserrat_24, 0);

        // 實時轉速
        lv_obj_t * lbl_cur_title = lv_label_create(scr);
        lv_label_set_text(lbl_cur_title, "CURRENT RPM");
        lv_obj_align(lbl_cur_title, LV_ALIGN_TOP_LEFT, 30, 80);
        lv_obj_set_style_text_color(lbl_cur_title, lv_palette_main(LV_PALETTE_RED), 0);
        lv_obj_set_style_text_font(lbl_cur_title, &lv_font_montserrat_24, 0);

        label_current_rpm = lv_label_create(scr);
        lv_label_set_text(label_current_rpm, "0");
        lv_obj_align(label_current_rpm, LV_ALIGN_TOP_LEFT, 30, 110);
        // 🟢 直接套用穩定的 24 號字體，拔除所有 zoom 相關設定！
        lv_obj_set_style_text_font(label_current_rpm, &lv_font_montserrat_48, 0); 

        // 歷史最高
        lv_obj_t * lbl_best_title = lv_label_create(scr);
        lv_label_set_text(lbl_best_title, "ALL-TIME BEST");
        lv_obj_align(lbl_best_title, LV_ALIGN_TOP_LEFT, 30, 200);
        lv_obj_set_style_text_color(lbl_best_title, lv_palette_main(LV_PALETTE_AMBER), 0);
        lv_obj_set_style_text_font(lbl_best_title, &lv_font_montserrat_24, 0);

        label_all_time_rpm = lv_label_create(scr);
        // 🟢 【新增】開機時，打開名為 "bx09_store" 的空間（true 代表唯讀模式）
        prefs.begin("bx09_store", true);
        // 讀取名為 "best_rpm" 的數字，如果裡面從來沒存過東西，預設值給 0
        global_all_time_best = prefs.getUInt("best_rpm", 0); 
        global_hist_count = prefs.getInt("hist_cnt", 0);
        if (global_hist_count > 0) {
            prefs.getBytes("hist_arr", global_history, sizeof(global_history));
        }
        prefs.end(); // 關閉空間
        lv_label_set_text_fmt(label_all_time_rpm, "%d RPM", global_all_time_best);
        lv_obj_align(label_all_time_rpm, LV_ALIGN_TOP_LEFT, 30, 230);
        // 🟢 拔除 zoom，套用 24 號字體
        lv_obj_set_style_text_font(label_all_time_rpm, &lv_font_montserrat_36, 0);

        // 📝 3. 中欄：歷史紀錄區 (X 座標 350)
        lv_obj_t * lbl_hist_title = lv_label_create(scr);
        lv_label_set_text(lbl_hist_title, "Recent History");
        lv_obj_align(lbl_hist_title, LV_ALIGN_TOP_LEFT, 350, 30);
        lv_obj_set_style_text_color(lbl_hist_title, lv_palette_main(LV_PALETTE_CYAN), 0);
        lv_obj_set_style_text_font(lbl_hist_title, &lv_font_montserrat_24, 0);

        label_history = lv_label_create(scr);
        String histText = "";
        for (int i = 0; i < global_hist_count; i++) {
            histText += String(i + 1) + ". " + String(global_history[i]) + " RPM\n";
        }
        // 如果完全沒紀錄，給予預設介面
        if (global_hist_count == 0) {
            histText = "1. -\n2. -\n3. -\n4. -\n5. -\n6. -\n7. -\n8. -";
        }

        // 🟢 正確修改：把組裝好的 histText 變數印出來！
        lv_label_set_text(label_history, histText.c_str()); 
        
        lv_obj_align(label_history, LV_ALIGN_TOP_LEFT, 350, 65);
        lv_obj_set_style_text_line_space(label_history, 8, 0);
        lv_obj_set_style_text_font(label_history, &lv_font_montserrat_24, 0);

        // 📈 4. 右欄：圖表 (X 座標 550)
        chart = lv_chart_create(scr);
        lv_obj_set_size(chart, 250, 240); 
        lv_obj_align(chart, LV_ALIGN_TOP_LEFT, 550, 40);
        
        lv_obj_set_style_bg_opa(chart, 0, LV_PART_MAIN);     
        lv_obj_set_style_border_color(chart, lv_color_hex(0x333333), LV_PART_MAIN); 
        
        lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
        chart_series = lv_chart_add_series(chart, lv_color_hex(0xFF0000), LV_CHART_AXIS_PRIMARY_Y);
        lv_obj_set_style_line_width(chart, 3, LV_PART_ITEMS);
        lv_obj_set_style_width(chart, 0, LV_PART_INDICATOR);
        lv_obj_set_style_height(chart, 0, LV_PART_INDICATOR);
    }

    // 2. 更新藍牙狀態燈
    void updateStatus(bool isConnected) {
        if (isConnected) {
            lv_led_set_color(led_status, lv_palette_main(LV_PALETTE_GREEN));
            lv_label_set_text(label_status, "BEYBLADE READY");
        } else {
            lv_led_set_color(led_status, lv_palette_main(LV_PALETTE_RED));
            lv_label_set_text(label_status, "BLE DISCONNECTED");
        }
    }

// 3. 繪製結果
    void showResults(uint16_t currentPeak, uint16_t allTimePeak, float history[], int histCount, uint16_t turnData[], int turnCount) {
        
        bool needSaveBest = false;
        
        // 1. 檢查並更新歷史最高紀錄
        if (allTimePeak > global_all_time_best) {
            global_all_time_best = allTimePeak;
            needSaveBest = true;
        }

        // 2. 🟢 處理最新歷史紀錄：將陣列資料全部往後推一格，騰出第 0 格
        for (int i = 7; i > 0; i--) {
            global_history[i] = global_history[i - 1];
        }
        // 把最新的一次發射塞到最前面
        global_history[0] = currentPeak; 
        
        if (global_hist_count < 8) {
            global_hist_count++;
        }

        // 3. 🟢 將新資料寫入 ESP32 的 Flash 硬碟中
        prefs.begin("bx09_store", false); // 讀寫模式
        if (needSaveBest) {
            prefs.putUInt("best_rpm", global_all_time_best);
        }
        prefs.putInt("hist_cnt", global_hist_count); // 存數量
        prefs.putBytes("hist_arr", global_history, sizeof(global_history)); // 存陣列
        prefs.end();

        // 4. 更新左側 UI
        lv_label_set_text_fmt(label_current_rpm, "%d RPM", currentPeak);
        lv_label_set_text_fmt(label_all_time_rpm, "%d RPM", global_all_time_best);

        // 5. 更新中間歷史紀錄 UI
        String histText = "";
        for (int i = 0; i < global_hist_count; i++) {
            histText += String(i + 1) + ". " + String(global_history[i]) + " RPM\n";
        }
        lv_label_set_text(label_history, histText.c_str());

        // 6. 更新右側圖表 (維持原樣)
        if (turnCount > 0) {
            uint16_t maxRpmInChart = 0;
            for (int i = 0; i < turnCount; i++) {
                if (turnData[i] > maxRpmInChart) maxRpmInChart = turnData[i];
            }
            maxRpmInChart = maxRpmInChart * 1.15;
            if (maxRpmInChart < 1000) maxRpmInChart = 1000;

            lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, maxRpmInChart);
            lv_chart_set_point_count(chart, turnCount);
            
            for (int i = 0; i < turnCount; i++) {
                lv_chart_set_value_by_id(chart, chart_series, i, turnData[i]);
            }
            lv_chart_refresh(chart);
        }
    }

    // 4. 安全檢查
    void handleUpdate() {
        if (readyToDraw) {
            readyToDraw = false; 
            showResults(Physics::peak_rpm, Physics::allTimePeak, Physics::history, Physics::historyCount, Physics::SP, Physics::size);
        }
    }
}
// ==========================================
// [模組 3] BLE 監聽器 (BLE Manager)
// ==========================================
namespace BLE_Manager {
    BLEClient* client = nullptr;
    bool isConnected = false;

    static void notifyCallback(BLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
        if (length == 0) return;
        uint8_t header = pData[0];

        if (header == 0xA0) {
            if (length >= 4 && pData[3] == 0x04) {
                Serial.println("\n[狀態] 🟢 陀螺已安裝");
                Physics::reset();  
                UI::updateStatus(true);  
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
                    
                    // 舉起旗標，讓 LVGL 在 Loop 裡面安全繪圖！
                    UI::readyToDraw = true; 
                }
            }
        }
    }

    class ClientCallback : public BLEClientCallbacks {
        void onDisconnect(BLEClient* pclient) {
            isConnected = false;
            Serial.println("!!! BX-09 斷開連線 ...");
            // 注意：這裡不能直接呼叫 LVGL UI，必須留給 Loop 去處理狀態燈
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
                delay(3000); 
            }
        }
    }
}

// ==========================================
// 主程式入口 (Main Setup & Loop)
// ==========================================
void setup() {
    Serial.begin(115200);
    // 讓 Serial Monitor 有時間連上
    delay(2000); 

    Serial.println(">>> BX-09 OS (LVGL Version) 啟動 <<<");

    UI::init();
    BLE_Manager::init();
}

void loop() {
    // 1. 維持藍牙連線
    BLE_Manager::connectTask();
    
    // 2. 檢查是否有新封包需要更新畫面
    UI::handleUpdate();

    // 3. LVGL 心跳包 (絕對不能刪掉！)
    lv_timer_handler(); 
    delay(5); 
} 