#pragma once
#include <Arduino.h>
#include "lcd_bl_pwm_bsp.h" // 引入背光控制以進行緊急關機

namespace UI {
    void updateBattery(int percentage, float voltage, bool isCharging);
}

// ==========================================
// [模組 5] 電池監控器 (實測調校 S-Curve + 低壓強制過放保護)
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

    // 🟢 實測調校 OCV 放電 S 曲線
    // 將 3.1V (3100mV) 調整為 10%，2.95V (2950mV) 為 0%
    struct VoltagePoint { float mV; float soc; };
    const VoltagePoint OCV_TABLE[] = {
        {3950, 100}, 
        {3900,  95},
        {3850,  90},
        {3800,  85}, 
        {3750,  80}, 
        {3700,  75}, 
        {3650,  70}, 
        {3600,  65},
        {3560,  60},
        {3520,  55},
        {3480,  50},
        {3440,  45},
        {3400,  40},
        {3360,  35},
        {3300,  30},
        {3240,  20},
        {3100,  10}, // 🟢 實測優化：3.1V 保持約 10% 電量
        {2950,   0}  // 🟢 2.95V 電池安全關機點 (0%)
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
        Serial.println("🔋 [系統] 電池模組啟動 (實測 S-Curve + 低壓深睡保護)");
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

            // 嚴格的 USB 插入/拔除偵測 (電壓 >= 4.1V / 4100mV 判定為充電中)
            if (current_mV - filtered_mV > 150 || current_mV >= 4100) {
                if (!isCharging) {
                    isCharging = true;
                    lastFakeChargeTime = millis();
                }
                filtered_mV = current_mV; 
            }
            else if (filtered_mV - current_mV > 150) {
                isCharging = false;
                filtered_mV = current_mV; 
            }

            float compensated_mV = filtered_mV + (isCharging ? 0 : LOAD_COMPENSATION_MV);

            if (isCharging) {
                // 充電計時器：每 60 秒增加 1%
                if (millis() - lastFakeChargeTime > 60000) {
                    lastFakeChargeTime = millis();
                    if (lastPercentage < 100 && lastPercentage != -1) {
                        lastPercentage += 1; 
                    }
                }
            } else {
                // 真實放電查表
                int currentPercentage = (int)getTruePercentage(compensated_mV);

                if (lastPercentage == -1) {
                    lastPercentage = currentPercentage; 
                } 
                
                // 單向鎖死機制
                if (currentPercentage < lastPercentage) {
                    lastPercentage = currentPercentage;
                }

                // 🛑 🟢 【低電量強制關機防護】
                // 未接 USB 且電壓低於 2.95V (2950mV) 時，關閉背光並進入 Deep Sleep 避免鋰電池過放損壞！
                if (compensated_mV <= 2950) {
                    lastPercentage = 0;
                    UI::updateBattery(0, compensated_mV / 1000.0, false);
                    Serial.println("🚨 [電池過放保護] 電壓低於 2.95V！強制關閉螢幕背光並進入 Deep Sleep...");
                    delay(300);
                    
                    // 關閉 LCD 背光
                    lcd_bl_pwm_bsp_init(0); 
                    
                    // 進入 Deep Sleep 模式 (超低功耗休眠)
                    esp_deep_sleep_start();
                }
            }

            // 安全防線
            if (!isCharging && compensated_mV >= 4100) {
                lastPercentage = 100;
            }

            float voltage_V = compensated_mV / 1000.0;
            UI::updateBattery(lastPercentage, voltage_V, isCharging);

            Serial.printf("🔋 [電池狀態] 原始: %.0fmV | 補償還原: %.0fmV => 最終電量: %d%% [%s]\n",
                          filtered_mV, compensated_mV, lastPercentage, isCharging ? "充電中 ⚡" : "放電中");
        }
    }
}