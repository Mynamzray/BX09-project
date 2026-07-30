#include "Web_Manager.h"
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include "Web_Assets.h" 

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
DNSServer dnsServer;
const byte DNS_PORT = 53;

float Web_Manager::last_peak = 0;
float Web_Manager::last_avg = 0;
uint16_t Web_Manager::last_duration = 0;
static bool sys_bleConnected = false;
static bool sys_beyInstalled = false;

// 🟢 記憶體最佳化：8 彈匣、單次 64 點。大幅釋放 SRAM 空間給藍牙！
#define MAX_HISTORY_SLOTS 8
#define MAX_CACHE_SIZE 64

struct LaunchRecord {
    uint32_t shot_id;
    uint16_t peak;
    uint16_t raw_peak;
    float avg;
    uint16_t size;
    uint16_t T[MAX_CACHE_SIZE];
    uint16_t rawSP[MAX_CACHE_SIZE];
    uint16_t SP[MAX_CACHE_SIZE];
};

static LaunchRecord launch_history[MAX_HISTORY_SLOTS];
static uint8_t history_count = 0;
static uint32_t next_shot_id = 1;
static uint32_t boot_session_id = 0; 

static volatile bool pendingLaunch = false;
static volatile uint32_t sync_client_id = 0;
static int sync_index = -1;
static unsigned long last_sync_time = 0;

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if(type == WS_EVT_CONNECT) {
        Serial.printf("WebSocket 用戶端 #%u 已連線！觸發離線數據同步...\n", client->id());
        Web_Manager::syncInitialData(client->id());
        
        if (history_count > 0) {
            sync_client_id = client->id(); 
            sync_index = history_count - 1;
        }
    }
}

void Web_Manager::init() {
    Serial.println("===========================================");
    Serial.println("🟢 啟動專屬 Wi-Fi 熱點與 Web 戰情室...");
    
    // 破解手機 Captive Portal 誤判過濾
    boot_session_id = (esp_random() % 1000000) + micros();

    WiFi.mode(WIFI_AP);
    
    // 讀取實體網卡，解決 0000 問題
    String mac = WiFi.macAddress();
    mac.replace(":", ""); 
    String uniqueID = mac.substring(mac.length() - 4); 
    String apName = "BX09_" + uniqueID; 
    
    Serial.println(">>> 開啟專屬熱點: " + apName);

    WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
    WiFi.softAP(apName.c_str()); 
    dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        String full_html = String(WEB_HTML_HEAD) + String(WEB_CSS) + String(WEB_HTML_BODY) + String(WEB_JS);
        request->send(200, "text/html", full_html);
    });
    
    server.onNotFound([](AsyncWebServerRequest *request){
        String full_html = String(WEB_HTML_HEAD) + String(WEB_CSS) + String(WEB_HTML_BODY) + String(WEB_JS);
        request->send(200, "text/html", full_html);
    });

    ws.onEvent(onEvent);
    server.addHandler(&ws);
    server.begin();
}

void Web_Manager::handle() {
    dnsServer.processNextRequest();
    ws.cleanupClients();

    // 觸發點 1：非阻塞式倒出離線歷史數據
    if (sync_client_id != 0 && sync_index >= 0) {
        if (millis() - last_sync_time > 50) {
            last_sync_time = millis();
            int i = sync_index;
            
            // 預先分配記憶體，組裝完美 JSON
            String json;
            json.reserve(2048); 
            
            json += "{\"type\":\"launch\",";
            json += "\"is_history\":true,"; 
            json += "\"session_id\":" + String(boot_session_id) + ",";
            json += "\"shot_id\":" + String(launch_history[i].shot_id) + ",";
            json += "\"peak\":" + String(launch_history[i].peak) + ",";
            json += "\"raw_peak\":" + String(launch_history[i].raw_peak) + ","; 
            json += "\"avg\":" + String(launch_history[i].avg) + ",";
            json += "\"size\":" + String(launch_history[i].size) + ",";
            
            json += "\"t\":[";
            for(int j = 0; j < launch_history[i].size; j++) { json += String(launch_history[i].T[j]); if(j < launch_history[i].size - 1) json += ","; }
            json += "],\"raw\":[";
            for(int j = 0; j < launch_history[i].size; j++) { json += String(launch_history[i].rawSP[j]); if(j < launch_history[i].size - 1) json += ","; }
            json += "],\"filtered\":[";
            for(int j = 0; j < launch_history[i].size; j++) { json += String(launch_history[i].SP[j]); if(j < launch_history[i].size - 1) json += ","; }
            json += "]}";

            ws.text(sync_client_id, json);
            
            sync_index--;
            if (sync_index < 0) sync_client_id = 0; 
        }
    }

    // 觸發點 2：當下立即的射擊廣播
    if (pendingLaunch) {
        pendingLaunch = false;
        if (ws.count() > 0) {
            String json;
            json.reserve(2048);
            
            json += "{\"type\":\"launch\",";
            json += "\"is_history\":false,"; 
            json += "\"session_id\":" + String(boot_session_id) + ",";
            json += "\"shot_id\":" + String(launch_history[0].shot_id) + ",";
            json += "\"peak\":" + String(launch_history[0].peak) + ",";
            json += "\"raw_peak\":" + String(launch_history[0].raw_peak) + ","; 
            json += "\"avg\":" + String(launch_history[0].avg) + ",";
            json += "\"size\":" + String(launch_history[0].size) + ",";
            
            json += "\"t\":[";
            for(int j = 0; j < launch_history[0].size; j++) { json += String(launch_history[0].T[j]); if(j < launch_history[0].size - 1) json += ","; }
            json += "],\"raw\":[";
            for(int j = 0; j < launch_history[0].size; j++) { json += String(launch_history[0].rawSP[j]); if(j < launch_history[0].size - 1) json += ","; }
            json += "],\"filtered\":[";
            for(int j = 0; j < launch_history[0].size; j++) { json += String(launch_history[0].SP[j]); if(j < launch_history[0].size - 1) json += ","; }
            json += "]}";

            // 🟢 Debug 追蹤：讓你知道 ESP32 真的有把資料推出去！
            Serial.println("[Web JSON 推播] 🚀 成功發送最新戰績給網頁！"); 
            ws.textAll(json);
        } else {
            Serial.println("[Web JSON 推播] ⚠️ 當前沒有網頁連線，數據已存入離線快取！");
        }
    }
}
void Web_Manager::broadcastStatus(bool bleConnected, bool beyInstalled) {
    sys_bleConnected = bleConnected;
    sys_beyInstalled = beyInstalled;
    if (ws.count() == 0) return;
    
    String json = "{\"type\":\"status\",\"bleConnected\":" + String(bleConnected ? "true" : "false") + ",\"beyInstalled\":" + String(beyInstalled ? "true" : "false") + "}";
    ws.textAll(json);
}

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
    for (int i = MAX_HISTORY_SLOTS - 1; i > 0; i--) {
        launch_history[i] = launch_history[i-1];
    }
    if (history_count < MAX_HISTORY_SLOTS) history_count++;

    launch_history[0].shot_id = next_shot_id++;
    launch_history[0].peak = peak;
    launch_history[0].raw_peak = raw_peak;
    launch_history[0].avg = avg;
    launch_history[0].size = (size > MAX_CACHE_SIZE) ? MAX_CACHE_SIZE : size;

    for (int i = 0; i < launch_history[0].size; i++) {
        launch_history[0].T[i] = T[i];
        launch_history[0].rawSP[i] = rawSP[i];
        launch_history[0].SP[i] = SP[i];
    }

    last_peak = peak;
    last_avg = avg;
    last_duration = launch_history[0].size > 0 ? launch_history[0].T[launch_history[0].size-1] : 0;
    
    pendingLaunch = true; 
}
// 在 Web_Manager.cpp 的最底下加入這個函式
void Web_Manager::broadcastBattery(int percentage, float voltage, bool isCharging) {
    if (ws.count() == 0) return;
    
    // 組裝 JSON 發送給 Web Dashboard，加入 voltage
    String json = "{\"type\":\"battery\",\"percentage\":" + String(percentage) + ",\"voltage\":" + String(voltage, 2) + ",\"isCharging\":" + String(isCharging ? "true" : "false") + "}";
    ws.textAll(json);
}
void Web_Manager::broadcastOfficialHistory(uint16_t origSP, uint16_t* history, uint8_t count) {
    if (ws.count() == 0) return;
    
    String json;
    json.reserve(256);
    json += "{\"type\":\"official_data\",";
    json += "\"origSP\":" + String(origSP) + ",";
    json += "\"history\":[";
    for (int i = 0; i < count; i++) {
        json += String(history[i]);
        if (i < count - 1) json += ",";
    }
    json += "]}";
    
    ws.textAll(json);
}