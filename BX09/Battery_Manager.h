#pragma once
#include <Arduino.h>
#include "Web_Manager.h" 

namespace UI {
    void updateBattery(int percentage, bool isCharging);
}

// ==========================================
// [模組 5] 電池監控器 (EMA 平滑抗雜訊版)
// ==========================================
namespace Battery_Manager {
    const int BAT_ADC_PIN = 4; 
    unsigned long lastCheckTime = 0;
    const unsigned long checkInterval = 2000; 

    // 🟢 核心：EMA 濾波後的平滑電壓
    float filtered_mV = 0; 
    
    int lastPercentage = -1;
    bool isCharging = false;

    int webReportedPercentage = -1;
    bool webReportedCharging = false;

    const float CALIBRATION_FACTOR = 1.04; 

    void init() {
        pinMode(BAT_ADC_PIN, INPUT);
        
        // 預先填充濾波器，取得初始電壓
        uint32_t raw_sum = 0;
        for(int i = 0; i < 20; i++) {
            raw_sum += analogReadMilliVolts(BAT_ADC_PIN) * 2;
            delay(5);
        }
        filtered_mV = (raw_sum / 20.0) * CALIBRATION_FACTOR;
        
        Serial.println("🔋 [系統] 電池監控模組 已啟動 (抗雜訊 EMA 濾波啟用)");
    }

    void handle() {
        if (millis() - lastCheckTime > checkInterval) {
            lastCheckTime = millis();

            // 1. 抓取當下平均值 (先消除極高頻雜訊)
            uint32_t raw_sum = 0;
            for(int i = 0; i < 10; i++) {
                raw_sum += analogReadMilliVolts(BAT_ADC_PIN) * 2;
            }
            float current_mV = (raw_sum / 10.0) * CALIBRATION_FACTOR;

            if (filtered_mV == 0) filtered_mV = current_mV;

            // 2. 指數移動平均 (EMA) 濾波器：極度平滑化曲線
            // 新數據只佔 10% 權重，歷史佔 90%，完全消滅 10~20mV 的跳動
            filtered_mV = (0.1 * current_mV) + (0.9 * filtered_mV);

            // 3. 轉換為百分比 (使用你設定的 3700mV 上限)
            int currentPercentage = map((int)filtered_mV, 3200, 3700, 0, 100);
            if (currentPercentage > 100) currentPercentage = 100;
            if (currentPercentage < 0) currentPercentage = 0;

            // 4. 絕對抗閃爍邏輯 (Anti-Bounce UX)
            if (lastPercentage == -1) {
                lastPercentage = currentPercentage; 
            } 

            // 🟢 嚴格的 USB 插入偵測：電壓必須"瞬間暴增"超過 150mV 才算充電！
            // 這樣可以完全杜絕 ESP32 晶片運算造成的 ADC 輕微波動
            if (current_mV - filtered_mV > 150 || current_mV > 4000) {
                isCharging = true;
                filtered_mV = current_mV; // 放棄濾波，快速追上真實充電電壓
            }
            // 偵測 USB 拔除：電壓瞬間暴跌超過 150mV
            else if (filtered_mV - current_mV > 150) {
                isCharging = false;
                filtered_mV = current_mV; // 放棄濾波，快速跌落
            }

            // 5. 單向鎖死機制 (電池不可能憑空生電)
            if (isCharging) {
                // 充電時：數字只准上升或持平
                if (currentPercentage > lastPercentage) {
                    lastPercentage = currentPercentage;
                }
            } else {
                // 放電時：數字只准下降或持平，絕對不准因為雜訊而回升！
                if (currentPercentage < lastPercentage) {
                    lastPercentage = currentPercentage;
                }
            }

            // 強制滿電防線更新：頂到 3700mV 以上死鎖 100%
            if (filtered_mV >= 3700) {
                lastPercentage = 100;
            }

            // 推送給硬體小螢幕
            UI::updateBattery(lastPercentage, isCharging);
            
            // 6. 網頁儀表板推播 (Web Dashboard Export via WebSocket)
            // 為了不洗頻，我們只在數值「真的有改變時」才推播給網頁
            if (lastPercentage != webReportedPercentage || isCharging != webReportedCharging) {
                webReportedPercentage = lastPercentage;
                webReportedCharging = isCharging;

                float voltage = filtered_mV / 1000.0;
                Web_Manager::broadcastBattery(lastPercentage, voltage, isCharging);
                
                Serial.printf("🌐 [Web 推播] 電池狀態更新 -> %d%%, 電壓: %.2fV, 充電中: %s\n", lastPercentage, voltage, isCharging ? "YES" : "NO");
            }
        }
    }
}