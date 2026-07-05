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
#include "Physics.h"
#include "UI.h"
#include "BLE_Manager.h"
#include "Button_Manager.h"
#include "Battery_Manager.h"
#include "Web_Manager.h"

// ==========================================
// 主程式入口 (Main Setup & Loop)
// ==========================================

void setup() {
    Serial.begin(115200);
    delay(2000);

    // 1. 先讓最肥的藍牙初始化，搶佔內部 RAM
    BLE_Manager::init();    

    // 2. 再啟動 Wi-Fi 和 Web Server
    Web_Manager::init();

    // 3. 最後再啟動 UI 和其他硬體
    UI::init();             
    Button_Manager::init(); 
    Battery_Manager::init(); 
}

void loop() {
    // 維持藍牙連線與系統監控
    BLE_Manager::connectTask();
    Button_Manager::handle();
    Battery_Manager::handle(); 
    
    // 🟢 處理 WebSocket 的內部連線維護
    Web_Manager::handle();

    UI::handleUpdate();
    lv_timer_handler(); 
    delay(5); 
}