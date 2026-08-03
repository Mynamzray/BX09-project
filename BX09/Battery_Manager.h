#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <esp_sleep.h>
#include "lcd_bl_pwm_bsp.h"

namespace UI {
    void updateBattery(int percentage, float voltage, bool isCharging);
    void showLowBatteryScreen();
}

// ==========================================
// [模組 5] 電池監控器 (NVS 記憶庫 + 絕對線性數學計算)
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
    const float CALIBRATION_FACTOR = 1.05; 
    
    // 🔋 負載補償核心 (Voltage Sag Compensation)
    const float LOAD_COMPENSATION_MV = 250.0; 

    // 🟢 絕對線性等距公式：3.9V = 100%, 2.95V = 0%
    float getTruePercentage(float current_mV) {
        if (current_mV >= 3900.0) return 100.0;
        if (current_mV <= 2950.0) return 0.0;

        // 數學映射：(當前電壓 - 最低電壓) / (總電壓區間) * 100
        return ((current_mV - 2950.0) / (3900.0 - 2950.0)) * 100.0;
    }

    void init() {
        pinMode(BAT_ADC_PIN, INPUT);
        
        uint32_t raw_sum = 0;
        for(int i = 0; i < 20; i++) {
            raw_sum += analogReadMilliVolts(BAT_ADC_PIN) * 2;
            delay(5);
        }
        filtered_mV = (raw_sum / 20.0) * CALIBRATION_FACTOR;
        
        // 🟢 計算開機瞬間的「真實物理電量」
        float boot_mV = filtered_mV + LOAD_COMPENSATION_MV;
        float boot_pct = getTruePercentage(boot_mV);
        
        // 從 Flash 記憶庫讀取上次存下的電量
        Preferences prefs;
        prefs.begin("bx09_store", true);
        int savedPct = prefs.getInt("bat_lvl", -1);
        prefs.end();

        if (savedPct >= 0 && savedPct <= 100) {
            // 🚨 智慧解鎖機制：如果真實物理電量比記憶體高超過 5%，代表更換過電池或關機充電了！
            if (boot_pct > savedPct + 5.0) {
                lastPercentage = (int)boot_pct;
                Serial.printf("🔋 [系統] 偵測到關機充電或更換電池，捨棄舊紀錄 %d%%，強制更新為 %d%%\n", savedPct, lastPercentage);
            } else {
                lastPercentage = savedPct;
                Serial.printf("🔋 [系統] 從 Flash 記憶庫成功復原上次電量: %d%%\n", lastPercentage);
            }
        } else {
            lastPercentage = (int)boot_pct;
            Serial.println("🔋 [系統] 電池模組首次啟動 (無 NVS 紀錄)");
        }
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

            // 嚴格的充電偵測 (大於 4.1V 絕對是插著 USB)
            if (current_mV >= 4100) {
                if (!isCharging) {
                    isCharging = true;
                    lastFakeChargeTime = millis();
                    filtered_mV = current_mV; 
                }
            } 
            // 小於 4.0V 絕對是拔除 USB
            else if (current_mV <= 4000) {
                if (isCharging) {
                    isCharging = false;
                    filtered_mV = current_mV; 
                }
            }

            // 負載補償
            float compensated_mV = filtered_mV + (isCharging ? 0 : LOAD_COMPENSATION_MV);

            if (isCharging) {
                // 充電中：每 60 秒人為爬升 1%
                if (millis() - lastFakeChargeTime > 60000) {
                    lastFakeChargeTime = millis();
                    if (lastPercentage < 100 && lastPercentage != -1) {
                        lastPercentage += 1; 
                    }
                }
            } else {
                // 放電中：等距線性公式計算
                int currentPercentage = (int)getTruePercentage(compensated_mV);
                
                // 單向鎖死機制 (防止放電期間電量跳動)
                if (currentPercentage < lastPercentage) {
                    lastPercentage = currentPercentage;
                }

                // 🚨 低電量強制關機防護：未接 USB 且電壓 <= 2.95V (2950mV) 進入 Deep Sleep
                if (compensated_mV <= 2950) {
                    lastPercentage = 0;
                    UI::updateBattery(0, compensated_mV / 1000.0, false);
                    
                    // 觸發大紅電池低電量畫面，並停留 3 秒
                    UI::showLowBatteryScreen();
                    delay(3000);

                    Serial.println("🚨 [警告] 電池電壓過低 (<=2.95V)！關機進入 Deep Sleep 以保護鋰電池！");
                    
                    // 關閉螢幕背光
                    lcd_bl_pwm_bsp_init(LCD_PWM_MODE_0);
                    
                    // 進入 Deep Sleep 深度睡眠
                    esp_deep_sleep_start();
                }
            }
        // 定期將最新的電量狀態存入 NVS 記憶庫
        static int lastSavedPct = -1;
        if (lastPercentage != lastSavedPct && lastPercentage >= 0) {
            lastSavedPct = lastPercentage;
            Preferences prefs;
            prefs.begin("bx09_store", false);
            prefs.putInt("bat_lvl", lastPercentage);
            prefs.end();
        }

        float voltage_V = compensated_mV / 1000.0;
        UI::updateBattery(lastPercentage, voltage_V, isCharging);

// --- 只在電量%或電壓明顯變化時列印，避免 ADC 抖動洗版 ---
        static int lastPrintedPct = -1;
        static float lastPrintedVoltage = -1.0;
        static bool lastPrintedCharging = false;

        bool pctChanged = (lastPercentage != lastPrintedPct);
        bool chargeStateChanged = (isCharging != lastPrintedCharging);
        bool voltageChanged = (fabs(voltage_V - lastPrintedVoltage) >= 0.02);  // 電壓變化 >= 20mV 才算

        if (pctChanged || chargeStateChanged || voltageChanged) {
            Serial.printf("🔋 [電池狀態] 原始: %.0fmV | 補償還原: %.0fmV -> 最終電量: %d%% [%s]\n",
                filtered_mV, compensated_mV, lastPercentage, isCharging ? "充電中 ⚡" : "放電中");
            lastPrintedPct = lastPercentage;
            lastPrintedVoltage = voltage_V;
            lastPrintedCharging = isCharging;
        }
    }
}
}