// ==========================================
// 1. LVGL 核心與驅動 
// ==========================================
#include <lvgl.h>
#include "user_config.h"
#include "lvgl_port.h"
#include "lcd_bl_pwm_bsp.h"
#include <Preferences.h> 
#include <WiFi.h>
#include <Arduino.h>
// #include <BLEDevice.h>
// #include <BLEUtils.h>
// #include <BLEScan.h>
// #include <BLEAdvertisedDevice.h>
#include <NimBLEDevice.h>
#define BX09_MAC "da:c4:51:04:58:86"
// #include "Physics.h"
#include "UI.h"
#include "BLE_Manager.h"
#include "Button_Manager.h"
#include "Battery_Manager.h"
// #include "Web_Manager.h"

// ==========================================
// 主程式入口 (Main Setup & Loop)
// ==========================================

void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.printf("Free heap: %d, Free internal: %d, Free PSRAM: %d\n",
     esp_get_free_heap_size(),
     heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
     heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
     
    // 1. 先讓最肥的藍牙初始化，搶佔內部 RAM
    BLE_Manager::init();    

    // 2. 再啟動 Wi-Fi 和 Web Server
    // Web_Manager::init();

    // 3. 最後再啟動 UI 和其他硬體
    UI::init();             
    Button_Manager::init(); 
    Battery_Manager::init(); 
}

void loop() {
    // 1. 處理非 UI 的背景邏輯
    BLE_Manager::connectTask();
    Button_Manager::handle();
    Battery_Manager::handle(); 
    // Web_Manager::handle();

    // 2. 安全地更新 UI (必須取得 lvgl_port 的 Mutex 鎖!)
    if (example_lvgl_lock(50)) {
        UI::handleUpdate();
        example_lvgl_unlock();
    }

    vTaskDelay(pdMS_TO_TICKS(5)); 
}