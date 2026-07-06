#include "Web_Manager.h"
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
DNSServer dnsServer;
const byte DNS_PORT = 53;

// 🟢 100% 離線版網頁：零外部依賴 (No Tailwind, No Chart.js CDN)
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="zh-TW">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
    <title>BX-09 戰情室</title>
    <style>
        /* 內嵌原生 CSS 完美還原 Tailwind 暗黑科技風 */
        :root {
            --bg: #09090b; --panel: #18181b; --border: #27272a;
            --text: #f4f4f5; --muted: #a1a1aa;
            --cyan: #06b6d4; --amber: #f59e0b; --red: #ef4444; --emerald: #10b981;
        }
        * { box-sizing: border-box; }
        body { 
            background: var(--bg); color: var(--text); margin: 0; padding: 0;
            font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, monospace;
            user-select: none; -webkit-user-select: none;
        }
        header { 
            background: rgba(24,24,27,0.8); border-bottom: 1px solid var(--border);
            padding: 12px 16px; display: flex; justify-content: space-between; align-items: center;
        }
        .title { font-size: 1.1rem; font-weight: bold; color: var(--cyan); letter-spacing: 1px; }
        .badge-version { background: rgba(6,182,212,0.1); border: 1px solid var(--cyan); color: var(--cyan); padding: 2px 8px; border-radius: 12px; font-size: 0.7rem; margin-left: 8px;}
        
        .status-pill { background: var(--panel); border: 1px solid var(--border); padding: 4px 12px; border-radius: 20px; display: flex; align-items: center; gap: 8px; font-size: 0.75rem; color: var(--muted); }
        .dot { width: 8px; height: 8px; border-radius: 50%; background: var(--red); box-shadow: 0 0 5px var(--red); }
        .dot.connected { background: var(--emerald); box-shadow: 0 0 5px var(--emerald); }

        .container { max-width: 1200px; margin: 0 auto; padding: 12px; display: grid; gap: 12px; grid-template-columns: 1fr; }
        @media(min-width: 1024px) { .container { grid-template-columns: 1fr 3fr; padding: 20px; gap: 20px; } }
        
        .col { display: flex; flex-direction: column; gap: 12px; }
        .card { background: var(--panel); border: 1px solid var(--border); border-radius: 12px; padding: 16px; position: relative; display: flex; flex-direction: column; align-items: center; justify-content: center; overflow: hidden; }
        
        .card-title { font-size: 0.75rem; color: var(--muted); letter-spacing: 2px; margin-bottom: 4px; text-transform: uppercase; }
        .card-value { font-size: 2.8rem; font-weight: 900; margin: 5px 0; }
        .card-desc { font-size: 0.7rem; color: #71717a; }
        
        .text-cyan { color: var(--cyan); text-shadow: 0 0 12px rgba(6,182,212,0.5); }
        .text-amber { color: var(--amber); text-shadow: 0 0 12px rgba(245,158,11,0.5); }
        
        .tag-tr { position: absolute; top: 0; right: 0; padding: 2px 8px; font-size: 0.65rem; border-bottom-left-radius: 8px; font-weight: bold; }
        .tag-cyan { background: rgba(6,182,212,0.15); color: var(--cyan); }
        .tag-amber { background: rgba(245,158,11,0.15); color: var(--amber); }

        /* 圖表區塊 */
        .chart-container { background: var(--panel); border: 1px solid var(--border); border-radius: 12px; padding: 16px; min-height: 350px; display: flex; flex-direction: column; }
        .chart-header { display: flex; justify-content: space-between; font-size: 0.8rem; margin-bottom: 10px; color: var(--muted); }
        canvas { width: 100%; height: 300px; display: block; flex-grow: 1; }

        /* 表格區塊 */
        .logs-section { padding: 16px; border-top: 1px solid var(--border); margin-top: 10px; }
        table { width: 100%; border-collapse: collapse; font-size: 0.8rem; }
        th { text-align: left; padding: 8px; color: var(--muted); border-bottom: 1px solid var(--border); }
        td { padding: 10px 8px; border-bottom: 1px solid rgba(39,39,42,0.5); }
        tr:hover { background: rgba(255,255,255,0.02); cursor: pointer; }
    </style>
</head>
<body>

    <header>
        <div>
            <span class="title">⚡ BX-09 TELEMETRY</span>
            <span class="badge-version">OFFLINE v5.0</span>
        </div>
        <div class="status-pill" id="statusBadge">
            <div class="dot" id="statusDot"></div>
            <span id="statusText">等待連線...</span>
        </div>
    </header>

    <div class="container">
        <!-- 數值面板 -->
        <div class="col">
            <div class="card" id="launchStateCard" style="border-color: var(--amber);">
                <span class="card-title" id="launchStateTitle">STATUS</span>
                <span class="card-value" id="launchStateVal" style="font-size: 1.2rem; color: var(--amber);">WAITING FOR LAUNCH</span>
            </div>
            <div class="card" style="box-shadow: 0 0 15px rgba(6,182,212,0.1); border-color: rgba(6,182,212,0.3);">
                <div class="tag-tr tag-cyan">PEAK</div>
                <span class="card-title text-cyan">PEAK RPM</span>
                <span class="card-value text-cyan" id="peakVal">0</span>
                <span class="card-desc">最高啟動轉速</span>
            </div>
            <div class="card">
                <div class="tag-tr tag-amber">AVG</div>
                <span class="card-title text-amber">AVERAGE RPM</span>
                <span class="card-value text-amber" id="avgVal">0</span>
                <span class="card-desc">全程平均轉速</span>
            </div>
            <div class="card">
                <span class="card-title">LAUNCH DURATION</span>
                <span class="card-value" id="durationVal" style="color: var(--text);">0<span style="font-size: 1rem; font-weight: normal;">ms</span></span>
                <span class="card-desc">拉線發力時長</span>
            </div>
        </div>

        <!-- 自製原生 Canvas 圖表 -->
        <div class="chart-container">
            <div class="chart-header">
                <span style="font-weight: bold; letter-spacing: 1px;">📈 SPIN ACCELERATION PROFILE</span>
                <span style="color: var(--cyan);">X: Time(ms) | Y: Speed(RPM)</span>
            </div>
            <div style="position: relative; width: 100%; height: 100%;">
                <canvas id="telemetryChart"></canvas>
            </div>
        </div>
    </div>

    <!-- 歷史紀錄 -->
    <div class="logs-section">
        <div style="max-width: 1200px; margin: 0 auto;">
            <h3 style="font-size: 0.8rem; color: var(--muted); letter-spacing: 1px; text-transform: uppercase;">📋 Session Launch Logs</h3>
            <div style="overflow-x: auto;">
                <table>
                    <thead>
                        <tr>
                            <th>ID</th><th>時間</th><th style="color: var(--cyan);">最高(Peak)</th><th style="color: var(--amber);">平均(Avg)</th><th>點數</th>
                        </tr>
                    </thead>
                    <tbody id="logTableBody">
                        <tr id="emptyRow"><td colspan="5" style="text-align: center; color: var(--muted);">等待發射數據...</td></tr>
                    </tbody>
                </table>
            </div>
        </div>
    </div>

    <script>
        // 🟢 內建輕量級 Canvas 繪圖引擎 (取代 Chart.js)
        function drawChart(tArr, rawArr, filteredArr) {
            const canvas = document.getElementById('telemetryChart');
            const ctx = canvas.getContext('2d');
            
            // 處理 Retina 高解析度螢幕防模糊
            const dpr = window.devicePixelRatio || 1;
            const rect = canvas.parentNode.getBoundingClientRect();
            canvas.width = rect.width * dpr;
            canvas.height = rect.height * dpr;
            ctx.scale(dpr, dpr);
            
            const w = rect.width, h = rect.height;
            ctx.clearRect(0, 0, w, h);
            if(!tArr || tArr.length === 0) return;

            const padX = 40, padY = 20;
            const drawW = w - padX - 10, drawH = h - padY * 2;
            const maxT = tArr[tArr.length-1] || 1;
            const maxRPM = Math.max(...filteredArr, ...rawArr, 5000); // Y軸最高點

            const mapX = (t) => padX + (t / maxT) * drawW;
            const mapY = (v) => h - padY - (v / maxRPM) * drawH;

            // 1. 畫背景網格與 Y 軸文字
            ctx.lineWidth = 1;
            ctx.textAlign = 'right';
            ctx.textBaseline = 'middle';
            ctx.font = '10px monospace';
            for(let i=0; i<=5; i++) {
                let val = Math.round(maxRPM * (i/5));
                let y = mapY(val);
                ctx.strokeStyle = '#27272a';
                ctx.beginPath(); ctx.moveTo(padX, y); ctx.lineTo(w, y); ctx.stroke();
                ctx.fillStyle = '#a1a1aa';
                ctx.fillText(val, padX - 5, y);
            }

            // 2. 畫 X 軸文字 (起點與終點)
            ctx.textAlign = 'center'; ctx.textBaseline = 'top';
            ctx.fillText("0ms", padX, h - padY + 5);
            ctx.fillText(maxT + "ms", w - 10, h - padY + 5);

            // 3. 畫 Raw Data (紅色半透明)
            ctx.strokeStyle = 'rgba(239, 68, 68, 0.4)';
            ctx.lineWidth = 2;
            ctx.beginPath();
            for(let i=0; i<tArr.length; i++) {
                let x = mapX(tArr[i]), y = mapY(rawArr[i]);
                if(i===0) ctx.moveTo(x,y); else ctx.lineTo(x,y);
            }
            ctx.stroke();

            // 4. 畫 Filtered Data (青色主線)
            ctx.strokeStyle = '#06b6d4';
            ctx.lineWidth = 3;
            ctx.beginPath();
            for(let i=0; i<tArr.length; i++) {
                let x = mapX(tArr[i]), y = mapY(filteredArr[i]);
                if(i===0) ctx.moveTo(x,y); else ctx.lineTo(x,y);
            }
            ctx.stroke();
        }

        // --- WebSocket 邏輯 ---
        let socket;
        let launchHistory = [];

        function initWebSocket() {
            socket = new WebSocket('ws://192.168.4.1/ws');
            socket.onopen = () => {
                document.getElementById('statusDot').className = 'dot connected';
                document.getElementById('statusText').innerText = '已連線至 BX-09';
                document.getElementById('statusText').style.color = 'var(--emerald)';
            };
            socket.onclose = () => {
                document.getElementById('statusDot').className = 'dot';
                document.getElementById('statusText').innerText = '已斷線，重連中...';
                document.getElementById('statusText').style.color = 'var(--red)';
                setTimeout(initWebSocket, 2000);
            };
            socket.onmessage = (event) => {
                try {
                    const data = JSON.parse(event.data);
                    if (data.type === "launch") handleNewLaunch(data);
                } catch (err) {}
            };
        }

        function handleNewLaunch(data) {
            document.getElementById('launchStateCard').style.borderColor = 'var(--emerald)';
            document.getElementById('launchStateVal').style.color = 'var(--emerald)';
            document.getElementById('launchStateVal').innerText = '💥 DETECTED!';
            
            setTimeout(() => {
                document.getElementById('launchStateCard').style.borderColor = 'var(--amber)';
                document.getElementById('launchStateVal').style.color = 'var(--amber)';
                document.getElementById('launchStateVal').innerText = 'WAITING FOR LAUNCH';
            }, 2000);

            document.getElementById('peakVal').innerText = data.peak.toLocaleString();
            document.getElementById('avgVal').innerText = Math.round(data.avg).toLocaleString();
            const totalDuration = data.t.length > 0 ? data.t[data.t.length - 1] : 0;
            document.getElementById('durationVal').innerHTML = `${totalDuration}<span style="font-size: 1rem; font-weight: normal;">ms</span>`;

            drawChart(data.t, data.raw, data.filtered);

            const timestamp = new Date().toLocaleTimeString();
            launchHistory.unshift({ id: launchHistory.length + 1, time: timestamp, ...data });
            if (launchHistory.length > 10) launchHistory.pop();
            renderTable();
        }

        function renderTable() {
            const tbody = document.getElementById('logTableBody');
            tbody.innerHTML = '';
            launchHistory.forEach((item) => {
                const tr = document.createElement('tr');
                tr.innerHTML = `
                    <td style="font-weight:bold;">#${item.id}</td>
                    <td>${item.time}</td>
                    <td style="color:var(--cyan); font-weight:bold;">${item.peak.toLocaleString()} RPM</td>
                    <td style="color:var(--amber);">${Math.round(item.avg).toLocaleString()} RPM</td>
                    <td>${item.size}</td>
                `;
                tr.onclick = () => {
                    document.getElementById('peakVal').innerText = item.peak.toLocaleString();
                    document.getElementById('avgVal').innerText = Math.round(item.avg).toLocaleString();
                    document.getElementById('durationVal').innerHTML = `${item.t[item.t.length - 1]}<span style="font-size: 1rem; font-weight: normal;">ms</span>`;
                    drawChart(item.t, item.raw, item.filtered);
                };
                tbody.appendChild(tr);
            });
        }

        // 初始化
        window.onload = () => {
            initWebSocket();
            // 初始化時畫一個空圖表網格
            drawChart([0, 100], [0, 0], [0, 0]); 
            
            // 監聽螢幕旋轉/調整大小，重繪 Canvas 防止變形
            window.addEventListener('resize', () => {
                if(launchHistory.length > 0) {
                    const latest = launchHistory[0];
                    drawChart(latest.t, latest.raw, latest.filtered);
                } else {
                    drawChart([0, 100], [0, 0], [0, 0]);
                }
            });
        };
    </script>
</body>
</html>
)rawliteral";

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if(type == WS_EVT_CONNECT) {
        Serial.printf("WebSocket 用戶端 #%u 已成功連線！\n", client->id());
    }
}

void Web_Manager::init() {
    Serial.println("===========================================");
    Serial.println("🟢 啟動專屬 Wi-Fi 熱點與 Web 戰情室...");
    
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
    WiFi.softAP("BX09_Telemetry"); 
    
    // DNS 攔截：Captive Portal 的核心
    dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
    
    Serial.print("熱點啟動成功！AP SSID: BX09_Telemetry (Open Wi-Fi)\n");

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send_P(200, "text/html", index_html);
    });

    // 攔截所有其他請求導向首頁 (觸發彈窗)
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

void Web_Manager::broadcastLaunch(uint16_t* T, uint16_t* rawSP, uint16_t* SP, uint16_t size, uint16_t peak, float avg) {
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