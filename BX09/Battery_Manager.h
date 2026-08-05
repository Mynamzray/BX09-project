#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <esp_sleep.h>
#include "lcd_bl_pwm_bsp.h"
#include "lvgl_port.h"

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
    const uint8_t ADC_SAMPLE_COUNT = 16;
    // Heuristic based on this board's displayed readings: approximately 4.2V
    // when full and unplugged, and 4.3-4.4V while USB charging.
    const uint8_t CHARGE_CONFIRM_SAMPLES = 3;
    const float CHARGE_START_MV = 4300.0f;
    const float CHARGE_STOP_MV = 4230.0f;

    // Charge-ramp: voltage during active charging is inflated by charge
    // current and isn't a reliable SoC proxy, so ramp toward 100% over an
    // estimated duration instead of jumping straight to the curve's reading.
    const unsigned long ESTIMATED_FULL_CHARGE_MS = 45UL * 60UL * 1000UL;  // ~45 min, tune to your battery
    unsigned long chargeStartMs = 0;
    int chargeStartPct = 0;
    bool wasCharging = false;

    float filtered_mV = 0; 
    int lastPercentage = -1;
    bool isCharging = false;

    // 硬體分壓電阻校正參數
    const float CALIBRATION_FACTOR = 1.00f; 

    struct BatteryPoint {
        uint16_t millivolts;
        uint8_t percentage;
    };

    // 1S Li-ion estimate under a light load. Terminal voltage rises while charging,
    // so this remains an estimate until a charger-status GPIO is available.
    static constexpr BatteryPoint BATTERY_CURVE[] = {
        {4000, 100}, {3900, 90}, {3840, 80}, {3790, 70},
        {3750, 60},  {3710, 50}, {3670, 40}, {3620, 30},
        {3560, 20},  {3460, 10}, {3300, 0},
    };

    int getBatteryPercentage(float millivolts) {
        constexpr size_t n = sizeof(BATTERY_CURVE) / sizeof(BATTERY_CURVE[0]);
        if (millivolts >= BATTERY_CURVE[0].millivolts) return BATTERY_CURVE[0].percentage;
        if (millivolts <= BATTERY_CURVE[n - 1].millivolts) return BATTERY_CURVE[n - 1].percentage;

        for (size_t i = 1; i < n; ++i) {
            const BatteryPoint &hi = BATTERY_CURVE[i - 1];
            const BatteryPoint &lo = BATTERY_CURVE[i];
            if (millivolts >= lo.millivolts) {
                float t = (millivolts - lo.millivolts) / (hi.millivolts - lo.millivolts);
                return (int)(lo.percentage + t * (hi.percentage - lo.percentage) + 0.5f);
            }
        }
        return 0;
    }

   // ⚡ 雙重充電偵測算法：絕對高壓門檻 (>=4170mV) + 16秒動態升壓斜率 (dV/dt >= +20mV) 偵測！
// Dual charge detection: absolute high-voltage threshold (>=4170mV) OR
// a rising trend over the last ~16s (8 samples x 2s) of at least +20mV.
bool updateChargingStatus(float current_mV) {
    static float voltageHistory[8] = {0};
    static uint8_t histIndex = 0;
    static uint8_t histCount = 0;
    static uint8_t chargeConfirmCount = 0;
    static uint8_t dischargeConfirmCount = 0;

    float baseline_mV = (histCount >= 8) ? voltageHistory[histIndex] : current_mV;
    voltageHistory[histIndex] = current_mV;
    histIndex = (histIndex + 1) % 8;
    if (histCount < 8) histCount++;

    float delta_mV = current_mV - baseline_mV;

    bool isRisingTrend = (histCount >= 8 && delta_mV >= 20.0f);
    bool isAbsoluteChargeVolts = (current_mV >= 4170.0f);

    if (isRisingTrend || isAbsoluteChargeVolts) {
        if (chargeConfirmCount < 2) chargeConfirmCount++;
        dischargeConfirmCount = 0;
    }
    else if (delta_mV <= -15.0f || (!isRisingTrend && current_mV < 4100.0f)) {
        if (dischargeConfirmCount < 2) dischargeConfirmCount++;
        chargeConfirmCount = 0;
    } else {
        chargeConfirmCount = 0;
        dischargeConfirmCount = 0;
    }

    if (chargeConfirmCount >= 2) return true;
    if (dischargeConfirmCount >= 2) return false;
    return isCharging;
}

    void init() {
        pinMode(BAT_ADC_PIN, INPUT);
        
        uint32_t raw_sum = 0;
        for (int i = 0; i < ADC_SAMPLE_COUNT; i++) {
            raw_sum += analogReadMilliVolts(BAT_ADC_PIN) * 2;
        }
        filtered_mV = (raw_sum / (float)ADC_SAMPLE_COUNT) * CALIBRATION_FACTOR;
        
        int boot_pct = getBatteryPercentage(filtered_mV);
        
        // 從 Flash 記憶庫讀取上次存下的電量
        Preferences prefs;
        prefs.begin("bx09_store", true);
        int savedPct = prefs.getInt("bat_lvl", -1);
        prefs.end();

        if (savedPct >= 0 && savedPct <= 100) {
            // 🚨 智慧解鎖機制：如果真實物理電量比記憶體高超過 5%，代表更換過電池或關機充電了！
            if (boot_pct > savedPct + 5) {
                lastPercentage = boot_pct;
                Serial.printf("🔋 [系統] 偵測到關機充電或更換電池，捨棄舊紀錄 %d%%，強制更新為 %d%%\n", savedPct, lastPercentage);
            } else {
                lastPercentage = savedPct;
                Serial.printf("🔋 [系統] 從 Flash 記憶庫成功復原上次電量: %d%%\n", lastPercentage);
            }
        } else {
            lastPercentage = boot_pct;
            Serial.println("🔋 [系統] 電池模組首次啟動 (無 NVS 紀錄)");
        }
    }

    void handle() {
        if (millis() - lastCheckTime > checkInterval) {
            lastCheckTime = millis();

            uint32_t raw_sum = 0;
            for (int i = 0; i < ADC_SAMPLE_COUNT; i++) {
                raw_sum += analogReadMilliVolts(BAT_ADC_PIN) * 2;
            }
            float current_mV = (raw_sum / (float)ADC_SAMPLE_COUNT) * CALIBRATION_FACTOR;
            if (filtered_mV == 0) filtered_mV = current_mV;

            // Favor stability; each update represents two seconds of samples.
            filtered_mV = (0.08f * current_mV) + (0.92f * filtered_mV);

             isCharging = updateChargingStatus(current_mV);

            if (isCharging && !wasCharging) {
                // Charging just started — remember where to ramp from.
                chargeStartMs = millis();
                chargeStartPct = (lastPercentage < 0) ? getBatteryPercentage(filtered_mV) : lastPercentage;
            }
            wasCharging = isCharging;

            if (isCharging) {
                float rampFraction = (float)(millis() - chargeStartMs) / (float)ESTIMATED_FULL_CHARGE_MS;
                if (rampFraction > 1.0f) rampFraction = 1.0f;
                int rampedPct = chargeStartPct + (int)(rampFraction * (100 - chargeStartPct));
                if (rampedPct > lastPercentage || lastPercentage < 0) {
                    lastPercentage = rampedPct;
                }
            } else {
                int currentPercentage = getBatteryPercentage(filtered_mV);
                if (lastPercentage < 0) {
                    lastPercentage = currentPercentage;
                } else if (currentPercentage < lastPercentage) {
                    // Keep discharging display monotonic even if voltage rebounds briefly.
                    lastPercentage = currentPercentage;
                }
            }

            // 🚨 低電量強制關機防護：未接 USB 且電壓 <= 2.95V (2950mV) 進入 Deep Sleep
            if (!isCharging && filtered_mV <= 2950) {
                lastPercentage = 0;
                // Both UI:: calls touch LVGL objects — must hold the render mutex
                // or they race the dedicated LVGL task and corrupt its widget tree.
                if (example_lvgl_lock(100)) {
                    UI::updateBattery(0, filtered_mV / 1000.0, false);
                    UI::showLowBatteryScreen();
                    example_lvgl_unlock();
                }
                delay(3000);

                Serial.println("🚨 [警告] 電池電壓過低 (<=2.95V)！關機進入 Deep Sleep 以保護鋰電池！");

                // 關閉螢幕背光
                lcd_bl_pwm_bsp_init(LCD_PWM_MODE_0);

                // 進入 Deep Sleep 深度睡眠
                esp_deep_sleep_start();
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

        float voltage_V = filtered_mV / 1000.0;
        if (example_lvgl_lock(100)) {
            UI::updateBattery(lastPercentage, voltage_V, isCharging);
            example_lvgl_unlock();
        }

// --- 只在電量%或電壓明顯變化時列印，避免 ADC 抖動洗版 ---
        static int lastPrintedPct = -1;
        static float lastPrintedVoltage = -1.0;
        static bool lastPrintedCharging = false;

        bool pctChanged = (lastPercentage != lastPrintedPct);
        bool chargeStateChanged = (isCharging != lastPrintedCharging);
        bool voltageChanged = (fabs(voltage_V - lastPrintedVoltage) >= 0.02);  // 電壓變化 >= 20mV 才算

        if (pctChanged || chargeStateChanged || voltageChanged) {
            Serial.printf("🔋 [電池狀態] 原始: %.0fmV -> 最終電量: %d%% [%s]\n",
                filtered_mV, lastPercentage, isCharging ? "充電中 ⚡" : "放電中");
            lastPrintedPct = lastPercentage;
            lastPrintedVoltage = voltage_V;
            lastPrintedCharging = isCharging;
        }
    }
}
}