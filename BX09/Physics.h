#pragma once
#include <Arduino.h>
#include <WiFiUdp.h>

extern WiFiUDP udp; // 借用主程式的全域 UDP 物件

namespace Physics {
    uint16_t rawProf[32] = {0};
    int count = 0;
    
    float peak_rpm = 0;
    float avg_rpm = 0;

    float allTimePeak = 0;     
    float history[8] = {0};    
    int historyCount = 0;      

    uint16_t SP[32] = {0};
    uint16_t rawSP[32] = {0}; // 備份原始數據
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
        // 2. 雜訊防護 (智慧型判定)
        // ==========================================
        // 步驟 2-1: 單點高於 25000 RPM 直接丟棄
        for (int i = 0; i < size; i++) {
            if (SP[i] > 25000) {
                SP[i] = 0; 
            }
        }

        // 步驟 2-2: 線性補差 (🟢 修正：避開前 4 點的黃金發力期，防止爆發力被誤殺)
        for (int pass = 0; pass < 2; pass++) { 
            for (int i = 4; i < size - 1; i++) { // 從第 4 點之後才啟動跳動過濾
                if (SP[i] == 0 || abs(SP[i] - SP[i-1]) > 7000 || abs(SP[i] - SP[i+1]) > 7000) { 
                    uint16_t interpolated = (SP[i-1] + SP[i+1]) / 2;
                    if (interpolated > 0 && interpolated < 25000) {
                        SP[i] = interpolated;
                    }
                }
            }
        }

        // 步驟 2-3: 邊界平滑
        if (size > 4) {
            if (abs(SP[size-1] - SP[size-2]) > 6000) SP[size-1] = SP[size-2];
        }

        // ==========================================
        // 3. 尋找峰值
        // ==========================================
        uint16_t trueMax = 0;
        int peakIndex = 0;

        for (int i = 0; i < size; i++) {
            if (SP[i] < 1000 && i >= 3) break; 
            if (SP[i] > trueMax) {
                trueMax = SP[i];
                peakIndex = i;
            }
        }

        // ==========================================
        // 4. [修復] 雙路徑輸出 (100% 對齊 Python 通訊協定)
        // ==========================================
        if (trueMax > 0) {
            peak_rpm = trueMax;

            // 發送起點標頭
            Serial.println("===CSV_START===");
            udp.beginPacket("192.168.4.255", 12345);
            udp.print("===CSV_START===\n");
            udp.endPacket();

            // 循環發送點位
            for(int i = 0; i < size; i++) {
                Serial.printf("%d,%d,%d,%d\n", T[i], rawSP[i], SP[i], trueMax);

                char buf[64];
                sprintf(buf, "%d,%d,%d,%d\n", T[i], rawSP[i], SP[i], trueMax);
                udp.beginPacket("192.168.4.255", 12345);
                udp.print(buf);
                udp.endPacket();
            }

            // 發送終點標頭 (🟢 修正：徹底消滅舊版分隔線，對齊協定)
            Serial.println("===CSV_END===");
            udp.beginPacket("192.168.4.255", 12345);
            udp.print("===CSV_END===\n");
            udp.endPacket();
        }

        // ==========================================
        // 5. 儲存結果並釋放記憶體
        // ==========================================
        if (trueMax > 0) {
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