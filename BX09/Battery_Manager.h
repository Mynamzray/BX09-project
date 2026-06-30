#pragma once
#include <Arduino.h>

// 🟢 預先宣告 UI 模組中的函式，讓電池模組認識它
namespace UI {
    void updateBattery(int percentage);
}

// ==========================================
// [模組 5] 電池監控器 (Battery Manager)
// ==========================================
namespace Battery_Manager {
    const int BAT_ADC_PIN = 4; // Waveshare 的電池偵測腳位
    unsigned long lastCheckTime = 0;
    const unsigned long checkInterval = 5000; // 每 5 秒檢查一次

    void init() {
        pinMode(BAT_ADC_PIN, INPUT);
        Serial.println("🔋 [系統] 電池監控模組已啟動 (GPIO 4)");
    }

    void handle() {
        if (millis() - lastCheckTime > checkInterval) {
            lastCheckTime = millis();
            
            // 1. 讀取真實毫伏特 (mV) 並還原分壓前數值
            uint32_t raw_mV = analogReadMilliVolts(BAT_ADC_PIN);
            uint32_t bat_mV = raw_mV * 2; 

            // 2. 轉換為百分比 (3200mV 沒電, 4200mV 滿電)
            int percentage = map(bat_mV, 3200, 4200, 0, 100);
            
            // 3. 邊界防護
            if (percentage > 100) percentage = 100;
            if (percentage < 0) percentage = 0;

            // 4. 通知 UI 更新
            UI::updateBattery(percentage);
        }
    }
}