#pragma once
#include <Arduino.h>

// ==========================================
// [模組 1] 物理運算器 (Physics Engine) - V5.0 最終版 (物理錨點慣性濾波器)
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
        // 2. 物理錨點慣性濾波器 (Stateful Physics Anchor Filter)
        // ==========================================
        uint16_t trueMax = 0;
        
        // A. 尋找第一個合理的起點 (過濾掉一開局的極端硬體錯亂)
        int start_idx = 0;
        while (start_idx < size && rawSP[start_idx] > 15000) {
            SP[start_idx] = 0; // 抹平無效的開局點
            start_idx++;
        }

        // 防呆：如果整組數據都是破表雜訊
        if (start_idx >= size) {
            trueMax = rawSP[0];
        } else {
            // B. 初始化錨點 (Anchor)
            uint16_t last_valid_rpm = rawSP[start_idx];
            trueMax = last_valid_rpm;
            SP[start_idx] = last_valid_rpm;

            // C. 順序掃描，拒絕不合理的物理暴衝
            for (int i = start_idx + 1; i < size; i++) {
                uint16_t curr_rpm = rawSP[i];
                
                // 強制轉型為 int32_t 防止 C++ uint16_t 下溢位 (Underflow)
                int32_t jump = (int32_t)curr_rpm - (int32_t)last_valid_rpm;
                
                // 🚨 物理極限門檻：
                // 1. 單次加速超過 +2500 RPM
                // 2. 單次減速超過 -4000 RPM
                // 3. 絕對值突破天花板 15000 RPM
                if (jump > 2000 || jump < -4000 || curr_rpm > 18000) {
                    // 拒絕此點！Anchor 留在原地不更新。
                    // 為了讓 WebUI 畫出來的圖表平滑，將這個雜訊點削平到與 Anchor 齊高
                    SP[i] = last_valid_rpm; 
                } else {
                    // 此點符合真實物理慣性！
                    last_valid_rpm = curr_rpm; // 更新 Anchor
                    SP[i] = curr_rpm;          // 允許寫入顯示陣列
                    
                    // 挑戰最高分
                    if (curr_rpm > trueMax) {
                        trueMax = curr_rpm;
                    }
                }
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
                if (SP[i] > 0) {
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
        
        // ==========================================
        // 4. 輸出乾淨的 CSV 資料流
        // ==========================================
        if (trueMax > 0) {
            Serial.println("===CSV_START===");
            for(int i = 0; i < size; i++) {
                // 輸出格式：時間, 原始轉速(帶雜訊), 過濾轉速(已削平), 最終結算峰值
                Serial.printf("%d,%d,%d,%d\n", T[i], rawSP[i], SP[i], (int)peak_rpm);
            }
            Serial.println("===CSV_END===");
        }

        count = 0;
        memset(rawProf, 0, sizeof(rawProf));
        return true;
    }
}