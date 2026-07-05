// Web_Manager.h
#ifndef WEB_MANAGER_H
#define WEB_MANAGER_H

#include <Arduino.h>

class Web_Manager {
public:
    // 初始化 Wi-Fi 熱點與啟動網頁伺服器
    static void init();

    // 處理斷線客戶端的清理 (放在 loop 中執行)
    static void handle();

    // 將最新的轉速資料透過 WebSocket 廣播給手機瀏覽器
    static void broadcastLaunch(uint16_t* T, uint16_t* rawSP, uint16_t* SP, uint16_t size, uint16_t peak, float avg);
    };

#endif