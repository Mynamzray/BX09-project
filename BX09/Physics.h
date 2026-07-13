#pragma once
#include <Arduino.h>
#include "Web_Manager.h" 

// ==========================================
// [模組 1] 物理運算器 (Physics Engine) - 山峰後處理濾波版
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
        // 步驟 1: 基本解碼 (完全還原所有原始數據)
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

        if (size < 3) {
            count = 0;
            memset(rawProf, 0, sizeof(rawProf));
            return false;
        }

        // ==========================================
        // 步驟 2: 尋找山峰 (Peak) 與次高點 (Second Best)
        // ==========================================
        int peak_idx = -1;
        uint16_t peak_val = 0;
        uint16_t second_best = 0;

        for (int i = 0; i < size; i++) {
            uint16_t current = rawSP[i];
            
            // 基礎極限防護：排除大於 18000 的完全物理不可能雜訊
            if (current > 18000) continue; 

            if (current > peak_val) {
                second_best = peak_val;
                peak_val = current;
                peak_idx = i;
            } else if (current > second_best) {
                second_best = current;
            }
        }

        // 找出剔除前的「絕對原始最大峰值 (Raw Peak)」
        uint16_t rawPeak = 0;
        for (int i = 0; i < size; i++) {
            if (rawSP[i] > rawPeak) {
                rawPeak = rawSP[i];
            }
        }

        // 先把所有原始數據複製給 SP，準備進行山峰理髮
        for(int i = 0; i < size; i++) {
            SP[i] = rawSP[i];
        }

        // ==========================================
        // 步驟 3: 實作山峰後處理 2000 Delta 規則
        // ==========================================
        if (peak_idx > 0 && peak_idx < size - 1) {
            uint16_t prev_val = rawSP[peak_idx - 1];
            uint16_t next_val = rawSP[peak_idx + 1];

            // 判斷是否為孤立的「避雷針」尖峰 (確保減法不會變成負數而造成溢位)
            bool is_left_steep  = (peak_val > prev_val) && ((peak_val - prev_val) > 2000);
            bool is_right_steep = (peak_val > next_val) && ((peak_val - next_val) > 2000);
            bool is_way_higher  = (peak_val > second_best) && ((peak_val - second_best) > 2000);

            if ((is_left_steep && is_right_steep) || is_way_higher) {
                Serial.printf("⛰️ [山峰濾波器] 偵測到孤立雜訊尖峰: %d RPM (索引: %d)\n", peak_val, peak_idx);
                Serial.printf("   👉 前點: %d | 後點: %d | 次高: %d\n", prev_val, next_val, second_best);
                Serial.printf("   👉 處置：溫柔削平，將最高值修正為次高值: %d RPM\n", second_best);
                
                // 溫柔削平！將雜訊點替換為次高點
                SP[peak_idx] = second_best;
                peak_val = second_best; // 修正結算峰值
            }
        } else if (peak_idx == 0 || peak_idx == size - 1) {
            // 如果最高點剛好在最邊緣 (發射瞬間或結束瞬間)
            bool is_way_higher = (peak_val > second_best) && ((peak_val - second_best) > 2000);
            if (is_way_higher) {
                Serial.printf("⛰️ [山峰濾波器] 偵測到邊緣雜訊尖峰: %d RPM (索引: %d)\n", peak_val, peak_idx);
                Serial.printf("   👉 處置：溫柔削平，將最高值修正為次高值: %d RPM\n", second_best);
                
                SP[peak_idx] = second_best;
                peak_val = second_best;
            }
        }

        // 把殘留的大於 20000 的物理界外雜訊也削平，確保圖表美觀
        for(int i = 0; i < size; i++) {
             if (SP[i] > 20000) SP[i] = peak_val;
        }

        uint16_t trueMax = peak_val;

        // ==========================================
        // 步驟 4: 儲存結果並釋放記憶體
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
        // 步驟 5: CSV 與 Web 網頁串流輸出
        // ==========================================
        if (trueMax > 0) {
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