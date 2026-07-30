#ifndef WEB_MANAGER_H
#define WEB_MANAGER_H

#include <Arduino.h>

class Web_Manager {
public:
    // 🟢 新增：用於狀態同步與快取的靜態變數
    static float last_peak;
    static float last_avg;
    static uint16_t last_duration;

    // 核心生命週期函數
    static void init();
    static void handle();

    // 🟢 新增：負責與前端網頁同步硬體狀態的函數
    static void broadcastStatus(bool bleConnected, bool beyInstalled);
    static void broadcastBattery(int percentage, float voltage, bool isCharging);
    static void syncInitialData(uint32_t clientId);

    // 發射數據廣播
    static void broadcastLaunch(uint16_t* T, uint16_t* rawSP, uint16_t* SP, uint16_t size, uint16_t peak, float avg, uint16_t raw_peak);
    static void broadcastOfficialHistory(uint16_t origSP, uint16_t* history, uint8_t count);
};

#endif