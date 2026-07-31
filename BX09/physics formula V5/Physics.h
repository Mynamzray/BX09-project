#pragma once
#include <Arduino.h>
#include "Web_Manager.h"

// ==========================================
// [模組 1] 物理運算器 (Physics Engine) - 裸測版 (無濾波)
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
        // 1. 基本解碼 (Raw Decoding)
        // ==========================================
        for (int i = 0; i < count; i += 1) { 
            auto nRefs = rawProf[i];
            if (nRefs == 0) continue; 

            auto dt = static_cast<double>(nRefs) / 125.0;
            auto sp = static_cast<uint16_t>(60000.0 / dt);

            elapsedTime += static_cast<uint16_t>(dt);
            T[size] = elapsedTime;
            rawSP[size] = sp; 
            size += 1;
        }

        if (size == 0) {
            reset();
            return false;
        }

        // ==========================================
        // 2. 暫時關閉濾波器：直接抓取原始最高點 (Raw Peak Test)
        // ==========================================
        uint16_t trueMax = 0;
        
        for (int i = 0; i < size; i++) {
            SP[i] = rawSP[i]; // 不做任何過濾，直接把原始數據餵給顯示陣列
            
            // 剔除絕對不可能的破表亂碼 (例如 15000 以上的光學錯亂)
            // 這樣 WebUI 座標軸才不會被撐爆到 35000 RPM
            if (rawSP[i] < 15000 && rawSP[i] > trueMax) {
                trueMax = rawSP[i];
            }
        }

        // ==========================================
        // 3. 儲存結果與更新歷史狀態
        // ==========================================
        if (trueMax > 0) {
            peak_rpm = trueMax;
            
            float sum_sp = 0;
            int valid_points = 0;
            for (int i = 0; i < size; i++) {
                if (SP[i] > 0 && SP[i] < 15000) { // 算平均時也排除破表亂碼
                    sum_sp += SP[i];
                    valid_points++;
                }
            }
            avg_rpm = valid_points > 0 ? (sum_sp / valid_points) : 0;
            
            for (int i = 7; i > 0; i--) {
                history[i] = history[i-1];
            }
            history[0] = peak_rpm; 
            if (historyCount < 8) historyCount++;
            
            if (peak_rpm > allTimePeak) {
                allTimePeak = peak_rpm;
            }
        }
        
        // 🟢 找出絕對原始最大峰值 (包含所有雜訊) 傳給網頁
        uint16_t rawPeak = 0;
        for (int i = 0; i < size; i++) {
            if (rawSP[i] > rawPeak) {
                rawPeak = rawSP[i];
            }
        }

        // ==========================================
        // 4. 輸出乾淨的 CSV 資料流
        // ==========================================
        if (trueMax > 0) {
            Web_Manager::broadcastLaunch(T, rawSP, SP, size, trueMax, avg_rpm, rawPeak);
            Serial.println("===CSV_START===");
            for(int i = 0; i < size; i++) {
                // 輸出格式：時間, 原始轉速(裸數據), 顯示轉速(裸數據), 最終結算峰值
                Serial.printf("%d,%d,%d,%d\n", T[i], rawSP[i], SP[i], (int)peak_rpm);
            }
            Serial.println("===CSV_END===");
        }

        count = 0;
        memset(rawProf, 0, sizeof(rawProf));
        return true;
    }
}