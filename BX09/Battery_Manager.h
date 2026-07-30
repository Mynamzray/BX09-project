#pragma once
#include <Arduino.h>

namespace UI {
    void updateBattery(int percentage, float voltage, bool isCharging);
}

// ==========================================
// [模組 5] 電池監控器 (專業工程版：S 曲線 + 充電幻術計時器)
// ==========================================
namespace Battery_Manager {
    const int BAT_ADC_PIN = 4; 
    unsigned long lastCheckTime = 0;
    const unsigned long checkInterval = 2000; 

    float filtered_mV = 0; 
    int lastPercentage = -1;
    bool isCharging = false;

    // 假充電計時器
    unsigned long lastFakeChargeTime = 0;

    // 硬體分壓電阻校正參數
    const float CALIBRATION_FACTOR = 1.04; 
    
    // 負載補償 (螢幕吃掉的電壓)
    const float LOAD_COMPENSATION_MV = 150.0; 

    // 標準鋰電池放電 S 曲線
    struct VoltagePoint { float mV; float soc; };
    const VoltagePoint OCV_TABLE[] = {
        {3950, 100}, {3900, 95}, {3850, 90}, {3800, 85}, {3750, 80},
        {3700,  75}, {3650, 70}, {3600, 65}, {3560, 60}, {3520, 55},
        {3480,  50}, {3440, 45}, {3400, 40}, {3360, 35}, {3320, 30},
        {3280,  25}, {3240, 20}, {3200, 15}, {3150, 10}, {3100,  5},
        {3000,   0} // 3.3V 安全關機線
    };
    const int OCV_POINTS = sizeof(OCV_TABLE) / sizeof(OCV_TABLE[0]);

    float getTruePercentage(float current_mV) {
        if (current_mV >= OCV_TABLE[0].mV) return 100.0;
        if (current_mV <= OCV_TABLE[OCV_POINTS - 1].mV) return 0.0;

        for (int i = 0; i < OCV_POINTS - 1; i++) {
            if (current_mV <= OCV_TABLE[i].mV && current_mV >= OCV_TABLE[i + 1].mV) {
                float t = (current_mV - OCV_TABLE[i + 1].mV) / (OCV_TABLE[i].mV - OCV_TABLE[i + 1].mV);
                return OCV_TABLE[i + 1].soc + t * (OCV_TABLE[i].soc - OCV_TABLE[i + 1].soc);
            }
        }
        return 0.0;
    }

    void init() {
        pinMode(BAT_ADC_PIN, INPUT);
        uint32_t raw_sum = 0;
        for(int i = 0; i < 20; i++) {
            raw_sum += analogReadMilliVolts(BAT_ADC_PIN) * 2;
            delay(5);
        }
        filtered_mV = (raw_sum / 20.0) * CALIBRATION_FACTOR;
        Serial.println("🔋 [系統] 電池模組啟動 (標準 S-Curve + 充電幻術啟用)");
    }

    void handle() {
        if (millis() - lastCheckTime > checkInterval) {
            lastCheckTime = millis();

            uint32_t raw_sum = 0;
            for(int i = 0; i < 10; i++) {
                raw_sum += analogReadMilliVolts(BAT_ADC_PIN) * 2;
            }
            float current_mV = (raw_sum / 10.0) * CALIBRATION_FACTOR;
            if (filtered_mV == 0) filtered_mV = current_mV;

            // EMA 濾波器
            filtered_mV = (0.1 * current_mV) + (0.9 * filtered_mV);

            // 嚴格的 USB 插入/拔除偵測 (大於 4.2V 絕對是充電器灌進來的電)
            if (current_mV - filtered_mV > 150 || current_mV >= 4200) {
                if (!isCharging) {
                    isCharging = true;
                    lastFakeChargeTime = millis(); // 記錄插上 USB 的那一刻
                }
                filtered_mV = current_mV; 
            }
            else if (filtered_mV - current_mV > 150) {
                isCharging = false;
                filtered_mV = current_mV; 
            }

            // 🟢 核心修正：充電與放電的雙軌邏輯
            float compensated_mV = filtered_mV + (isCharging ? 0 : LOAD_COMPENSATION_MV);

            if (isCharging) {
                // 🪄【充電幻術】放棄使用 4.3V 查表，改用計時器！
                // 每 60000 毫秒 (60 秒) 人為灌水 1%
                if (millis() - lastFakeChargeTime > 60000) {
                    lastFakeChargeTime = millis();
                    if (lastPercentage < 100 && lastPercentage != -1) {
                        lastPercentage += 1; 
                    }
                }
            } else {
                // 🔋【真實放電】使用補償後的電壓去查 S 曲線
                int currentPercentage = (int)getTruePercentage(compensated_mV);

                if (lastPercentage == -1) {
                    lastPercentage = currentPercentage; 
                } 
                
                // 單向鎖死機制：放電時，數字只准下降或持平
                if (currentPercentage < lastPercentage) {
                    lastPercentage = currentPercentage;
                }
            }

            // 安全防線
            if (!isCharging && compensated_mV >= 4150) {
                lastPercentage = 100;
            }

            // 送給 UI 顯示的是實際測到的電壓
            float voltage_V = compensated_mV / 1000.0;
            UI::updateBattery(lastPercentage, voltage_V, isCharging);

            // ==========================================
            // 監控器：觀察幻術運作
            // ==========================================
            Serial.printf("🔋 [電池狀態] 原始: %.0fmV | 補償還原: %.0fmV => 最終電量: %d%% [%s]\n",
                          filtered_mV, compensated_mV, lastPercentage, isCharging ? "充電中 ⚡ (啟動定時幻術)" : "放電中");
        }
    }
}

// 現在，當你插上充電線時，右上角依然會顯示 `(4.3V)` 讓你知道電源有順利進來，但前面的 `%` 數就不會直接暴衝到 `100%` 了，而是會從你插上那一刻的電量（例如 `55%`）開始，每隔 1 分鐘優雅地爬升 `1%`。

// 這樣你的使用者體驗就會跟一般市售消費電子產品一模一樣了！