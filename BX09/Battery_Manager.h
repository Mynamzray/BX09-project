#pragma once
#include <Arduino.h>
#include "Web_Manager.h" // 🟢 引入 Web_Manager，讓電池可以發送 WebSocket 廣播

namespace UI {
    void updateBattery(int percentage, bool isCharging);
}

// ==========================================
// [模組 5] 電池監控器 (Web Dashboard 串接版)
// ==========================================
namespace Battery_Manager {
    const int BAT_ADC_PIN = 4; 
    unsigned long lastCheckTime = 0;
    const unsigned long checkInterval = 2000; 

    int voltageHistory[10] = {0};
    int historyIndex = 0;

    int lastPercentage = -1;
    bool isCharging = false;

    int webReportedPercentage = -1;
    bool webReportedCharging = false;

    const float CALIBRATION_FACTOR = 1.04; 

    void init() {
        pinMode(BAT_ADC_PIN, INPUT);
        for(int i = 0; i < 10; i++) {
            voltageHistory[i] = (int)(analogReadMilliVolts(BAT_ADC_PIN) * 2 * CALIBRATION_FACTOR);
            delay(10);
        }
        Serial.println("🔋 [系統] 電池監控模組 已啟動 (WebSocket 串接完畢)");
    }

    void handle() {
        if (millis() - lastCheckTime > checkInterval) {
            lastCheckTime = millis();

            voltageHistory[historyIndex] = (int)(analogReadMilliVolts(BAT_ADC_PIN) * 2 * CALIBRATION_FACTOR);
            historyIndex = (historyIndex + 1) % 10;

            uint32_t avg_mV = 0;
            for(int i = 0; i < 10; i++) {
                avg_mV += voltageHistory[i];
            }
            avg_mV /= 10;

            // 🟢 修正 1：改回 4050mV！抵銷 ESP32 巨大耗電量造成的物理壓降 (Voltage Sag)
            int currentPercentage = map(avg_mV, 3200, 4050, 0, 100);
            if (currentPercentage > 100) currentPercentage = 100;
            if (currentPercentage < 0) currentPercentage = 0;

            // 🟢 修正 2：更穩定的軟體充電偵測 (避免 84, 87, 85 之間來回閃爍)
            if (lastPercentage == -1) {
                lastPercentage = currentPercentage; 
            } else {
                // 電壓跳升 >= 2%，判定為插入 USB
                if (currentPercentage >= lastPercentage + 2) {
                    isCharging = true;
                    lastPercentage = currentPercentage;
                } 
                // 放電邏輯
                else if (currentPercentage < lastPercentage) {
                    // 拔掉 USB 瞬間，電壓必定會往下掉。若掉落超過 2%，解除充電狀態
                    if (isCharging && currentPercentage <= lastPercentage - 2) {
                        isCharging = false;
                    }
                    
                    if (!isCharging) {
                        lastPercentage = currentPercentage;
                    }
                }
            }

            // 🟢 修正 3：強制滿電防線。只要插著 USB 電壓頂到 4050 以上，死鎖在 100%
            if (avg_mV >= 4050) {
                lastPercentage = 100;
            }

            // 推送給硬體小螢幕
            UI::updateBattery(lastPercentage, isCharging);
            
            // ==========================================
            // 6. 網頁儀表板推播 (Web Dashboard Export via WebSocket)
            // ==========================================
            if (lastPercentage != webReportedPercentage || isCharging != webReportedCharging) {
                webReportedPercentage = lastPercentage;
                webReportedCharging = isCharging;

                // 🟢 呼叫我們剛剛在 Web_Manager 新增的廣播功能！
                Web_Manager::broadcastBattery(lastPercentage, isCharging);
                
                // 留個 Log 方便你除錯
                Serial.printf("🌐 [Web 推播] 電池狀態更新 -> %d%%, 充電中: %s\n", lastPercentage, isCharging ? "YES" : "NO");
            }
        }
    }
}