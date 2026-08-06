#pragma once
#include <Arduino.h>
#include "BLE_Manager.h"
#include "Stopwatch_Manager.h"

namespace UI {
    void clearStopwatchUI();
}

// ==========================================
// [模組 4] 實體按鍵控制器 (長按 1.5 秒清除紀錄版)
// ==========================================
namespace Button_Manager {
    const int BOOT_PIN = 0; // GPIO 0 BOOT 鍵
    
    unsigned long pressStartTime = 0;
    unsigned long lastDebounceTime = 0; 
    const unsigned long debounceDelay = 50; 
    const unsigned long longPressThreshold = 1500; // 1.5 秒長按門檻
    
    int lastButtonState = HIGH;
    int buttonState = HIGH;
    bool longPressHandled = false;

    void init() {
        pinMode(BOOT_PIN, INPUT_PULLUP);
        Serial.println("🔌 [系統] BOOT 鍵 (GPIO 0) 初始化完成 (支援長按清空紀錄)");
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
                    pressStartTime = millis();
                    longPressHandled = false;
                } else {
                    // 短按釋放動作
                    if (!longPressHandled) {
                        if (Stopwatch_Manager::isStopwatchMode) {
                            if (Stopwatch_Manager::state == Stopwatch_Manager::State::RUNNING) {
                                Serial.println("🔘 按鈕短按：手動停止秒錶計時！");
                                Stopwatch_Manager::stop();
                            } else {
                                Serial.println("🔘 秒錶尚未開始，忽略短按。");
                            }
                        } else {
                            Serial.println("🔘 按鈕短按：觸發藍牙開關...");
                            BLE_Manager::toggleBluetooth();
                        }
                    }
                }
            } else if (buttonState == LOW && !longPressHandled) {
                // 長按偵測 (1.5 秒)
                if (millis() - pressStartTime >= longPressThreshold) {
                    longPressHandled = true;
                    if (Stopwatch_Manager::isStopwatchMode) {
                        Serial.println("🗑️ 長按 1.5 秒觸發：清空秒錶所有歷史紀錄！");
                        Stopwatch_Manager::clearHistory();
                        if (example_lvgl_lock(100)) {
                            UI::clearStopwatchUI();
                            example_lvgl_unlock();
                        }
                    } else {
                        Serial.println("🔘 長按 1.5 秒：重置藍牙雷達...");
                        BLE_Manager::toggleBluetooth();
                    }
                }
            }
        }
        lastButtonState = reading;
    }
}