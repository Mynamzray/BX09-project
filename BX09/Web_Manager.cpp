#include "Web_Manager.h"
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include "Web_Assets.h" // 🟢 引入剛剛拆分出來的前端資源

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
DNSServer dnsServer;
const byte DNS_PORT = 53;

// 靜態狀態快取變數
float Web_Manager::last_peak = 0;
float Web_Manager::last_avg = 0;
uint16_t Web_Manager::last_duration = 0;
static bool sys_bleConnected = false;
static bool sys_beyInstalled = false;

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if(type == WS_EVT_CONNECT) {
        Serial.printf("WebSocket 用戶端 #%u 已成功連線！推送歷史快取...\n", client->id());
        // 🟢 任務一：新手機連線時，立刻把目前快取的硬體狀態與最後一次射擊成績塞給它
        Web_Manager::syncInitialData(client->id());
    }
}

void Web_Manager::init() {
    Serial.println("===========================================");
    Serial.println("🟢 啟動專屬 Wi-Fi 入口熱點與 Web 戰情室...");
    
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
    WiFi.softAP("BX09_Telemetry"); 
    dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());

    // 🟢 任務二核心：在根路由將拆開的靜態資源在記憶體中動態拼接，組裝成完整網頁
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        String full_html = String(WEB_HTML_HEAD) + String(WEB_CSS) + String(WEB_HTML_BODY) + String(WEB_JS);
        request->send(200, "text/html", full_html);
    });

    // Captive Portal 攔截機制：將所有系統網路探測流量導向 192.168.4.1 觸發自動彈窗
    server.onNotFound([](AsyncWebServerRequest *request){
        request->redirect("http://192.168.4.1/");
    });

    ws.onEvent(onEvent);
    server.addHandler(&ws);
    server.begin();
    Serial.println("🟢 Captive Portal & Web Server 啟動完成！");
    Serial.println("===========================================");
}

void Web_Manager::handle() {
    dnsServer.processNextRequest();
    ws.cleanupClients();
}

// 🟢 任務一：實作狀態即時廣播函數
void Web_Manager::broadcastStatus(bool bleConnected, bool beyInstalled) {
    sys_bleConnected = bleConnected;
    sys_beyInstalled = beyInstalled;
    if (ws.count() == 0) return; // 沒有網頁連線就跳過，省頻寬
    
    String json = "{\"type\":\"status\",\"bleConnected\":" + String(bleConnected ? "true" : "false") + ",\"beyInstalled\":" + String(beyInstalled ? "true" : "false") + "}";
    ws.textAll(json);
}

// 🟢 任務一：實作新客戶端首次接入時的快取數據拉取
void Web_Manager::syncInitialData(uint32_t clientId) {
    String json = "{\"type\":\"sync\",";
    json += "\"peak\":" + String(last_peak) + ",";
    json += "\"avg\":" + String(last_avg) + ",";
    json += "\"duration\":" + String(last_duration) + ",";
    json += "\"bleConnected\":" + String(sys_bleConnected ? "true" : "false") + ",";
    json += "\"beyInstalled\":" + String(sys_beyInstalled ? "true" : "false");
    json += "}";
    ws.text(clientId, json);
}

void Web_Manager::broadcastLaunch(uint16_t* T, uint16_t* rawSP, uint16_t* SP, uint16_t size, uint16_t peak, float avg, uint16_t raw_peak) {
    // 射擊時同步更新快取，防範使用者中途刷新網頁數據遺失
    last_peak = peak;
    last_avg = avg;
    last_duration = size > 0 ? T[size-1] : 0;

    if (ws.count() == 0) return;

    String json = "{";
    json += "\"type\":\"launch\",";
    json += "\"peak\":" + String(peak) + ",";
    json += "\"avg\":" + String(avg) + ",";
    json += "\"size\":" + String(size) + ",";
    
    json += "\"t\":[";
    for(int i = 0; i < size; i++) { json += String(T[i]); if(i < size - 1) json += ","; }
    json += "],\"raw\":[";
    for(int i = 0; i < size; i++) { json += String(rawSP[i]); if(i < size - 1) json += ","; }
    json += "],\"filtered\":[";
    for(int i = 0; i < size; i++) { json += String(SP[i]); if(i < size - 1) json += ","; }
    json += "]}";

    ws.textAll(json);
}