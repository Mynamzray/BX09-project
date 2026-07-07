#pragma once
#include <Arduino.h>
#include "Web_Manager.h" 

// ==========================================
// [模組 1] 物理運算器 (Physics Engine) - V4 官方 Delta 濾波版
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
    uint16_t rawSP[32] = {0}; 
    uint16_t size = 0;

    void reset() {
        count = 0;
        memset(rawProf, 0, sizeof(rawProf));
        memset(SP, 0, sizeof(SP)); 
        memset(rawSP, 0, sizeof(rawSP)); 
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
        memset(rawSP, 0, sizeof(rawSP));
        size = 0;
        memset(T, 0, sizeof(T)); 
        elapsedTime = 0;         

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
            rawSP[size] = sp; 
            size += 1;
        }

        // ==========================================
        // 步驟 2: 原版平滑曲線處理 (保留給 WebBased Chart 畫圖用)
        // ==========================================
        for (int i = 0; i < size; i++) {
            if (SP[i] > 25000) {
                SP[i] = 0; 
            }
        }

        for (int pass = 0; pass < 2; pass++) { 
            for (int i = 1; i < size - 1; i++) {
                if (SP[i] == 0 || abs((int32_t)SP[i] - (int32_t)SP[i-1]) > 6000 || abs((int32_t)SP[i] - (int32_t)SP[i+1]) > 6000) { 
                    uint16_t interpolated = (SP[i-1] + SP[i+1]) / 2;
                    if (interpolated > 0 && interpolated < 25000) {
                        SP[i] = interpolated;
                    }
                }
            }
        }

        if (size > 1) {
            if (abs((int32_t)SP[0] - (int32_t)SP[1]) > 6000) SP[0] = SP[1];
            if (abs((int32_t)SP[size-1] - (int32_t)SP[size-2]) > 6000) SP[size-1] = SP[size-2];
        }

        // ==========================================
        // 步驟 3: [全新核心] 官方 2000 RPM Delta Rule (精準擷取 Peak)
        // ==========================================
        uint16_t trueMax = 0;
        uint16_t calcSP[32] = {0};
        int calcSize = size;
        
        for(int i = 0; i < size; i++) {
            calcSP[i] = rawSP[i];
        }

        while (calcSize > 0) {
            uint16_t currentPeak = 0;
            int peakIndex = -1;

            for (int i = 0; i < calcSize; i++) {
                if (calcSP[i] > currentPeak) {
                    currentPeak = calcSP[i];
                    peakIndex = i;
                }
            }

            if (peakIndex == -1 || currentPeak == 0) break;

            bool isNoise = false;
            
            if (calcSize == 1) {
                trueMax = currentPeak;
                break;
            } else if (peakIndex == 0) {
                if (abs((int32_t)currentPeak - (int32_t)calcSP[1]) > 2000) isNoise = true;
            } else if (peakIndex == calcSize - 1) {
                if (abs((int32_t)currentPeak - (int32_t)calcSP[calcSize - 2]) > 2000) isNoise = true;
            } else {
                if (abs((int32_t)currentPeak - (int32_t)calcSP[peakIndex - 1]) > 2000 && 
                    abs((int32_t)currentPeak - (int32_t)calcSP[peakIndex + 1]) > 2000) {
                    isNoise = true;
                }
            }

            if (isNoise) {
                for (int i = peakIndex; i < calcSize - 1; i++) {
                    calcSP[i] = calcSP[i + 1];
                }
                calcSize--;
            } else {
                trueMax = currentPeak;
                break;
            }
        }

        // 🟢 新增：找出剔除前的「絕對原始最大峰值 (Raw Peak)」
        uint16_t rawPeak = 0;
        for (int i = 0; i < size; i++) {
            if (rawSP[i] > rawPeak) {
                rawPeak = rawSP[i];
            }
        }

        // ==========================================
        // 4. 儲存結果並釋放記憶體
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
        // 5. CSV 與 Web 網頁串流輸出
        // ==========================================
        if (trueMax > 0) {
            // 🟢 關鍵修復：把 Web_Manager 廣播加回來！(包含 rawPeak 參數)
            Web_Manager::broadcastLaunch(T, rawSP, SP, size, trueMax, avg_rpm, rawPeak);

            Serial.println("===CSV_START===");
            for(int i = 0; i < size; i++) {
                Serial.printf("%d,%d,%d,%d\n", T[i], rawSP[i], SP[i], (int)peak_rpm);
            }
            Serial.println("===CSV_END===");
        }

        count = 0;
        memset(rawProf, 0, sizeof(rawProf));
        return true;
    }
}