#pragma once
#include <Arduino.h>

// 🟢 修改宣告，讓電池模組認識新的 isCharging 參數
namespace UI {
    void updateBattery(int percentage, bool isCharging);
}

// ==========================================
// [模組 5] 電池監控器 (防抖濾波與充電偵測版)
// ==========================================
namespace Battery_Manager {
    const int BAT_ADC_PIN = 4; 
    unsigned long lastCheckTime = 0;
    const unsigned long checkInterval = 2000; // 每 2 秒採樣一次

    int voltageHistory[10] = {0};
    int historyIndex = 0;

    int lastPercentage = -1;
    bool isCharging = false;

    void init() {
        pinMode(BAT_ADC_PIN, INPUT);
        
        // 開機時先連續讀取 10 次填滿陣列，避免開機電量從 0 開始亂跳
        for(int i = 0; i < 10; i++) {
            voltageHistory[i] = analogReadMilliVolts(BAT_ADC_PIN) * 2;
            delay(10);
        }
        Serial.println("🔋 [系統] 電池監控模組 (防抖濾波版) 已啟動");
    }

    void handle() {
        if (millis() - lastCheckTime > checkInterval) {
            lastCheckTime = millis();
            
            // 1. 讀取最新電壓並寫入歷史陣列
            uint32_t raw_mV = analogReadMilliVolts(BAT_ADC_PIN);
            voltageHistory[historyIndex] = raw_mV * 2;
            historyIndex = (historyIndex + 1) % 10;

            // 2. 計算「移動平均值」(過濾掉瞬間被抽載的電壓浮動)
            uint32_t avg_mV = 0;
            for(int i = 0; i < 10; i++) {
                avg_mV += voltageHistory[i];
            }
            avg_mV /= 10;

            // 3. 轉換為百分比 (🟢 修正：將滿電門檻下調至 4050mV)
            int currentPercentage = map(avg_mV, 3200, 4050, 0, 100);
            if (currentPercentage > 100) currentPercentage = 100;
            if (currentPercentage < 0) currentPercentage = 0;

            // 4. 單向閥門 (One-Way Valve) 與軟體充電偵測
            if (lastPercentage == -1) {
                lastPercentage = currentPercentage; 
            } else {
                // 如果電量「逆勢上漲」超過 2%，判定為插入 USB (電壓被強制灌高)
                if (currentPercentage >= lastPercentage + 2) {
                    isCharging = true;
                    lastPercentage = currentPercentage;
                } 
                // 正常的放電過程，電量只能往下掉
                else if (currentPercentage < lastPercentage) {
                    // 拔掉 USB 後，電壓會回落，解除充電狀態
                    if (avg_mV < 4000) { 
                        isCharging = false;
                    }
                    lastPercentage = currentPercentage;
                }
            }

            // 5. 滿電強制判定保險
            if (avg_mV >= 4050) {
                isCharging = true;
                lastPercentage = 100;
            }

            // 6. 通知 UI 更新 (傳遞百分比與充電狀態)
            UI::updateBattery(lastPercentage, isCharging);
        }
    }
}