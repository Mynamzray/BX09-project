#pragma once
#include <Arduino.h>


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