// ==========================================
// 1. LVGL 核心與驅動 
// ==========================================
#include <lvgl.h>
#include "user_config.h"
#include "lvgl_port.h"
#include "lcd_bl_pwm_bsp.h"
#include <Preferences.h> 
#include <WiFi.h>
#include <WiFiUdp.h>
WiFiUDP udp;
// ==========================================
// 2. Arduino 與你的藍牙函式庫
// ==========================================
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

#define BX09_MAC "da:c4:51:04:58:86"
#include "Physics.h"
#include "UI.h"
#include "BLE_Manager.h"
#include "Button_Manager.h"
#include "Battery_Manager.h"
// ==========================================
// 主程式入口 (Main Setup & Loop)
// ==========================================
void setup() {
Serial.begin(115200);
    delay(2000); // 讓 Serial Monitor 有時間連上
    Serial.println(">>> BX-09 OS (LVGL Version) 啟動 <<<");
    // 🟢 新增：啟動專屬 Wi-Fi 熱點
    Serial.println("啟動無線遙測熱點中...");
    // 設定你的 Wi-Fi 名稱與密碼 (密碼至少需要 8 個字元)
    WiFi.softAP("BX09_Telemetry", "beyblade123"); 
    udp.begin(12345); // 開啟 12345 通訊埠
    Serial.print("熱點已啟動！請讓筆電連線。IP: ");
    Serial.println(WiFi.softAPIP());
    UI::init();             // 1. 讓 Waveshare 先初始化，隨便它怎麼洗腳位
    Button_Manager::init(); // 2. 我們最後出場，把 GPIO 0 強制搶回來！
    Battery_Manager::init(); // 🟢 加入這行：初始化電池監控
    BLE_Manager::init();    // 3. 啟動藍牙
}

void loop() {
    // 1. 維持藍牙連線
    BLE_Manager::connectTask();
    Button_Manager::handle(); // 🟢 每局監聽按鍵狀態
    Battery_Manager::handle(); // 🟢 每 5 秒更新一次電量
    UI::handleUpdate();
    lv_timer_handler(); 
    delay(5); 
} 