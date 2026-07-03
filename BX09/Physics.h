#pragma once
#include <Arduino.h>

// ==========================================
// [模組 1] 物理運算器 (Physics Engine) - A/B 繪圖測試版
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
        uint16_t rawSP[32] = {0}; // 🟢 新增：用來備份完全未過濾的原始數據

        memset(SP, 0, sizeof(SP));
        size = 0;

        // ⚠️ 註解掉繪圖家看不懂的純文字
        // Serial.println("\n=================[ BX-09 RAW PROFILE DATA ]=================");
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

            elapsedTime += static_cast<uint16_t>(dt);
            T[size] = elapsedTime;
            SP[size] = sp;
            rawSP[size] = sp; // 🟢 把赤裸的原始數據存起來，稍後畫線用
            size += 1;
        }

        // ==========================================
        // 步驟 2-1: 單點高於 25000 RPM 直接丟棄
        for (int i = 0; i < size; i++) {
            if (SP[i] > 25000) {
                SP[i] = 0; 
                // ⚠️ 暫時關閉文字輸出
                // Serial.printf("⚠️ 觸發前輩規則 1: 點 %d 測得異常超高轉速，準備進行線性補差\n", i);
            }
        }

        // 步驟 2-2: 前後超過 6000 RPM，用「線性補差法」取代 (維持你的 6000 測試值)
        for (int pass = 0; pass < 2; pass++) { 
            for (int i = 1; i < size - 1; i++) {
                if (SP[i] == 0 || abs(SP[i] - SP[i-1]) > 6000 || abs(SP[i] - SP[i+1]) > 6000) { 
                    uint16_t interpolated = (SP[i-1] + SP[i+1]) / 2;
                    if (interpolated > 0 && interpolated < 25000) {
                        SP[i] = interpolated;
                    }
                }
            }
        }

        // 步驟 2-3: 邊界防護 (一併同步為 6000)
        if (size > 1) {
            if (abs(SP[0] - SP[1]) > 6000) SP[0] = SP[1];
            if (abs(SP[size-1] - SP[size-2]) > 6000) SP[size-1] = SP[size-2];
        }

        // 🟢 【繪圖家專用輸出】: 輸出格式為 "標籤1:數值1,標籤2:數值2"
        for(int i = 0; i < size; i++) {
            Serial.printf("Raw_Data:%d,Filtered_Data:%d\n", rawSP[i], SP[i]);
        }

        // ==========================================
        // 3. 清除突波後，尋找峰值與下降曲線
        // ==========================================
        uint16_t trueMax = 0;
        int peakIndex = 0;

        for (int i = 0; i < size; i++) {
            if (SP[i] < 1000 && i >= 3) {
                break; 
            }
            if (SP[i] > trueMax) {
                trueMax = SP[i];
                peakIndex = i;
            }
        }

        // ⚠️ 註解掉結算報告，避免干擾繪圖
        // Serial.println("\n--- [Step 2] Algorithm Decision Summary ---");
        // ... (省略文字列印) ...

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
        // ==========================================
        // [新增] CSV 自動化腳本專用輸出格式
        // ==========================================
        if (trueMax > 0) {
            Serial.println("===CSV_START===");
            for(int i = 0; i < size; i++) {
                // 格式：經過時間(ms), 原始轉速, 過濾轉速, 本次發射最高轉速
                Serial.printf("%d,%d,%d,%d\n", T[i], rawSP[i], SP[i], peak_rpm);
            }
            Serial.println("===CSV_END===");
        }

        // ==========================================
        // 釋放記憶體 (原本的代碼)
        // ==========================================
        count = 0;
        memset(rawProf, 0, sizeof(rawProf));
        return true;
    }
}