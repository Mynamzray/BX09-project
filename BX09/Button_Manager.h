#pragma once
#include <Arduino.h>

// ==========================================
// [模組 4] 實體按鍵控制器 (GPIO 0 BOOT 鍵奪回版)
// ==========================================
namespace Button_Manager {
    const int BOOT_PIN = 0; // 放心用 GPIO 0！
    
    unsigned long lastDebounceTime = 0; 
    const unsigned long debounceDelay = 50; 
    
    int lastButtonState = HIGH;
    int buttonState = HIGH;

    void init() {
        // 強制設定為輸入並啟用內部上拉
        pinMode(BOOT_PIN, INPUT_PULLUP);
        Serial.println("🔌 [系統] BOOT 鍵 (GPIO 0) 已經成功奪回控制權！");
    }

    void handle() {
        int reading = digitalRead(BOOT_PIN);

        if (reading != lastButtonState) {
            lastDebounceTime = millis();
        }

        if ((millis() - lastDebounceTime) > debounceDelay) {
            if (reading != buttonState) {
                buttonState = reading;
                
                if (buttonState == LOW) {
                    Serial.println("🔘 BOOT 按鍵被按下！觸發藍牙開關...");
                    BLE_Manager::toggleBluetooth(); 
                }
            }
        }
        lastButtonState = reading;
    }
}