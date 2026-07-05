#include "Web_Manager.h"
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>

DNSServer dnsServer;
const byte DNS_PORT = 53;
// 建立網頁伺服器與 WebSocket 實例 (監聽 Port 80)
 AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// 前端 Dashboard 網頁原始碼 (存於 PROGMEM 中節省 RAM 空間)
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="zh-TW" class="dark">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>BX-09 戰情室 - Web Telemetry</title>
    <script src="https://cdn.tailwindcss.com"></script>
    <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
    <style>
        @import url('https://fonts.googleapis.com/css2?family=Share+Tech+Mono&display=swap');
        .font-tech { font-family: 'Share Tech Mono', monospace; }
        .glow-text-cyan { text-shadow: 0 0 8px rgba(6, 182, 212, 0.8), 0 0 15px rgba(6, 182, 212, 0.3); }
        .glow-text-amber { text-shadow: 0 0 8px rgba(245, 158, 11, 0.8), 0 0 15px rgba(245, 158, 11, 0.3); }
        .border-cyan-glow { box-shadow: 0 0 15px rgba(6, 182, 212, 0.15); border: 1px solid rgba(6, 182, 212, 0.3); }
    </style>
</head>
<body class="bg-zinc-950 text-slate-100 min-h-screen flex flex-col font-tech select-none">

    <header class="border-b border-zinc-900 bg-zinc-900/40 backdrop-blur px-4 py-3 flex items-center justify-between">
        <div class="flex items-center space-x-3">
            <span class="text-xl font-bold tracking-wider text-cyan-400">⚡ BX-09 TELEMETRY</span>
            <span class="text-xs bg-cyan-950 text-cyan-400 border border-cyan-800 px-2 py-0.5 rounded-full">v4.0 AP Mode</span>
        </div>
        <div id="statusBadge" class="flex items-center space-x-2 bg-zinc-900 px-3 py-1 rounded-full border border-zinc-800">
            <span id="statusIndicator" class="h-2 w-2 rounded-full bg-red-500 animate-pulse"></span>
            <span id="statusText" class="text-xs text-slate-400">正在嘗試連線...</span>
        </div>
    </header>

    <main class="flex-grow p-4 max-w-7xl mx-auto w-full grid grid-cols-1 lg:grid-cols-4 gap-4">
        <div class="lg:col-span-1 flex flex-col space-y-4">
            <div id="launchStateCard" class="bg-zinc-900/60 p-4 rounded-xl border border-zinc-800 text-center transition-all duration-300">
                <span id="launchStateText" class="text-sm tracking-widest text-slate-400 block mb-1">STATUS</span>
                <span id="launchStateVal" class="text-lg font-bold text-amber-500 animate-pulse">WAITING FOR LAUNCH</span>
            </div>
            <div class="bg-zinc-900/40 p-5 rounded-xl border-cyan-glow flex flex-col items-center relative overflow-hidden">
                <div class="absolute top-0 right-0 bg-cyan-500/10 text-cyan-400 text-xs px-2.5 py-0.5 rounded-bl">PEAK</div>
                <span class="text-sm tracking-widest text-cyan-500/80 mb-1">PEAK RPM</span>
                <span id="peakVal" class="text-5xl font-black text-cyan-400 glow-text-cyan my-1">0</span>
                <span class="text-xs text-slate-500">最高啟動轉速</span>
            </div>
            <div class="bg-zinc-900/40 p-5 rounded-xl border border-zinc-800/80 flex flex-col items-center relative overflow-hidden">
                <div class="absolute top-0 right-0 bg-amber-500/10 text-amber-400 text-xs px-2.5 py-0.5 rounded-bl">AVG</div>
                <span class="text-sm tracking-widest text-amber-500/80 mb-1">AVERAGE RPM</span>
                <span id="avgVal" class="text-4xl font-bold text-amber-400 glow-text-amber my-1">0</span>
                <span class="text-xs text-slate-500">全程平均轉速</span>
            </div>
            <div class="bg-zinc-900/40 p-5 rounded-xl border border-zinc-800/80 flex flex-col items-center relative overflow-hidden">
                <span class="text-sm tracking-widest text-slate-400 mb-1">LAUNCH DURATION</span>
                <span id="durationVal" class="text-3xl font-bold text-slate-300 my-1">0 <span class="text-sm font-normal">ms</span></span>
                <span class="text-xs text-slate-500">拉線發力時長</span>
            </div>
        </div>

        <div class="lg:col-span-3 bg-zinc-900/20 p-5 rounded-xl border border-zinc-900 flex flex-col min-h-[350px] lg:min-h-[450px]">
            <div class="flex items-center justify-between mb-4">
                <h3 class="text-sm font-bold tracking-wider text-slate-400">📈 SPIN ACCELERATION PROFILE</h3>
                <span class="text-xs text-cyan-500/80">X: Time (ms) | Y: Speed (RPM)</span>
            </div>
            <div class="flex-grow relative w-full h-full min-h-[300px]">
                <canvas id="telemetryChart"></canvas>
            </div>
        </div>
    </main>

    <footer class="border-t border-zinc-900 bg-zinc-900/20 p-4 w-full">
        <div class="max-w-7xl mx-auto">
            <h3 class="text-xs font-bold tracking-wider text-slate-500 mb-3 uppercase">📋 Session Launch Logs</h3>
            <div class="overflow-x-auto">
                <table class="w-full text-left text-xs border-collapse">
                    <thead>
                        <tr class="border-b border-zinc-900 text-slate-400">
                            <th class="py-2 px-3">發射 ID</th>
                            <th class="py-2 px-3">時間</th>
                            <th class="py-2 px-3 text-cyan-400">最高轉速 (Peak)</th>
                            <th class="py-2 px-3 text-amber-400">平均轉速 (Avg)</th>
                            <th class="py-2 px-3">資料點數</th>
                        </tr>
                    </thead>
                    <tbody id="logTableBody" class="divide-y divide-zinc-900/60">
                        <tr id="emptyRow" class="text-slate-500">
                            <td colspan="5" class="py-4 text-center">等待發射數據...</td>
                        </tr>
                    </tbody>
                </table>
            </div>
        </div>
    </footer>

    <script>
        const statusBadge = document.getElementById('statusBadge');
        const statusIndicator = document.getElementById('statusIndicator');
        const statusText = document.getElementById('statusText');
        const peakVal = document.getElementById('peakVal');
        const avgVal = document.getElementById('avgVal');
        const durationVal = document.getElementById('durationVal');
        const launchStateCard = document.getElementById('launchStateCard');
        const launchStateVal = document.getElementById('launchStateVal');
        const logTableBody = document.getElementById('logTableBody');

        let launchHistory = [];

        const ctx = document.getElementById('telemetryChart').getContext('2d');
        const telemetryChart = new Chart(ctx, {
            type: 'line',
            data: {
                labels: [], 
                datasets: [
                    {
                        label: '過濾平滑曲線 (Filtered SP)',
                        data: [],
                        borderColor: '#06b6d4',
                        borderWidth: 3,
                        pointRadius: 2,
                        pointBackgroundColor: '#06b6d4',
                        tension: 0.15,
                        fill: false
                    },
                    {
                        label: '原始感測雜訊 (Raw SP)',
                        data: [],
                        borderColor: 'rgba(239, 68, 68, 0.4)',
                        borderWidth: 0,
                        pointRadius: 5,
                        pointHoverRadius: 7,
                        pointBackgroundColor: '#ef4444',
                        showLine: false,
                        fill: false
                    }
                ]
            },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                animation: { duration: 300 }, 
                scales: {
                    x: { grid: { color: '#18181b' }, ticks: { color: '#71717a' }, title: { display: true, text: '時間 (ms)', color: '#71717a' } },
                    y: { beginAtZero: true, max: 20000, grid: { color: '#18181b' }, ticks: { color: '#71717a' }, title: { display: true, text: '轉速 (RPM)', color: '#71717a' } }
                },
                plugins: { legend: { labels: { color: '#e4e4e7', font: { family: 'Share Tech Mono' } } } }
            }
        });

        const gateway = `ws://${window.location.hostname}/ws`;
        let socket;

        function initWebSocket() {
            socket = new WebSocket(gateway);
            socket.onopen = onOpen;
            socket.onclose = onClose;
            socket.onmessage = onMessage;
        }

        function onOpen() {
            statusIndicator.className = "h-2 w-2 rounded-full bg-emerald-500 animate-pulse";
            statusText.innerText = "已連線至 BX-09 晶片";
            statusText.className = "text-xs text-emerald-400";
            statusBadge.className = "flex items-center space-x-2 bg-zinc-900 px-3 py-1 rounded-full border border-emerald-800/30";
        }

        function onClose() {
            statusIndicator.className = "h-2 w-2 rounded-full bg-red-500 animate-pulse";
            statusText.innerText = "已斷線，5秒後重連...";
            statusText.className = "text-xs text-red-400";
            statusBadge.className = "flex items-center space-x-2 bg-zinc-900 px-3 py-1 rounded-full border border-red-800/30";
            setTimeout(initWebSocket, 5000);
        }

        function onMessage(event) {
            try {
                const data = JSON.parse(event.data);
                if (data.type === "launch") handleNewLaunch(data);
            } catch (err) { console.error(err); }
        }

        function handleNewLaunch(data) {
            launchStateCard.className = "bg-emerald-950/40 p-4 rounded-xl border border-emerald-500 text-center transition-all duration-300";
            launchStateVal.className = "text-lg font-bold text-emerald-400 glow-text-cyan";
            launchStateVal.innerText = "💥 LAUNCH DETECTED!";

            setTimeout(() => {
                launchStateCard.className = "bg-zinc-900/60 p-4 rounded-xl border border-zinc-800 text-center transition-all duration-300";
                launchStateVal.className = "text-lg font-bold text-amber-500 animate-pulse";
                launchStateVal.innerText = "WAITING FOR LAUNCH";
            }, 2500);

            peakVal.innerText = data.peak.toLocaleString();
            avgVal.innerText = Math.round(data.avg).toLocaleString();
            const totalDuration = data.t.length > 0 ? data.t[data.t.length - 1] : 0;
            durationVal.innerHTML = `${totalDuration} <span class="text-sm font-normal">ms</span>`;

            updateChart(data.t, data.raw, data.filtered);

            const timestamp = new Date().toLocaleTimeString();
            const launchItem = {
                id: launchHistory.length + 1, time: timestamp, peak: data.peak, avg: data.avg, size: data.size, t: data.t, raw: data.raw, filtered: data.filtered
            };

            launchHistory.unshift(launchItem);
            if (launchHistory.length > 10) launchHistory.pop(); 
            renderHistoryTable();
        }

        function updateChart(tArr, rawArr, filteredArr) {
            telemetryChart.data.labels = tArr;
            telemetryChart.data.datasets[0].data = filteredArr;
            telemetryChart.data.datasets[1].data = rawArr;
            telemetryChart.update();
        }

        function renderHistoryTable() {
            const emptyRow = document.getElementById('emptyRow');
            if (emptyRow) emptyRow.remove();

            logTableBody.innerHTML = '';
            launchHistory.forEach((item) => {
                const tr = document.createElement('tr');
                tr.className = "border-b border-zinc-900/40 hover:bg-zinc-900/60 cursor-pointer transition-colors duration-200";
                tr.innerHTML = `
                    <td class="py-2.5 px-3 font-bold text-slate-400">#${item.id}</td>
                    <td class="py-2.5 px-3 text-slate-400">${item.time}</td>
                    <td class="py-2.5 px-3 text-cyan-400 font-bold">${item.peak.toLocaleString()} RPM</td>
                    <td class="py-2.5 px-3 text-amber-500">${Math.round(item.avg).toLocaleString()} RPM</td>
                    <td class="py-2.5 px-3 text-slate-500">${item.size} Pts</td>
                `;
                tr.onclick = () => {
                    peakVal.innerText = item.peak.toLocaleString();
                    avgVal.innerText = Math.round(item.avg).toLocaleString();
                    const totalDuration = item.t.length > 0 ? item.t[item.t.length - 1] : 0;
                    durationVal.innerHTML = `${totalDuration} <span class="text-sm font-normal">ms</span>`;
                    updateChart(item.t, item.raw, item.filtered);
                    launchStateVal.innerText = `🔍 ANALYSIS LAUNCH #${item.id}`;
                    launchStateVal.className = "text-lg font-bold text-cyan-400";
                };
                logTableBody.appendChild(tr);
            });
        }

        window.onload = initWebSocket;
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
    
    WiFi.softAP("BX09_Telemetry", "beyblade123");
    
    Serial.print("熱點啟動成功！AP SSID: BX09_Telemetry\n");
    Serial.print("👉 請用手機連線，瀏覽器輸入: ");
    Serial.println(WiFi.softAPIP());
    dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
    // 🟢 修正：這裡必須是小寫的 server，對應上面定義的 AsyncWebServer server(80);
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send_P(200, "text/html", index_html);
        server.begin();
    });

    ws.onEvent(onEvent);
    server.addHandler(&ws); // 🟢 這裡也是小寫 server
    server.begin();         // 🟢 這裡也是小寫 server
    
    Serial.println("🟢 Web Server 啟動完成！");
    Serial.println("===========================================");
}

void Web_Manager::handle() {
    dnsServer.processNextRequest(); // 🟢 攔截手機發出的連線檢查請求
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
    for(int i = 0; i < size; i++) {
        json += String(T[i]);
        if(i < size - 1) json += ",";
    }
    json += "],";

    json += "\"raw\":[";
    for(int i = 0; i < size; i++) {
        json += String(rawSP[i]);
        if(i < size - 1) json += ",";
    }
    json += "],";

    json += "\"filtered\":[";
    for(int i = 0; i < size; i++) {
        json += String(SP[i]);
        if(i < size - 1) json += ","; 
    }
    json += "]";
    json += "}";

    ws.textAll(json);
}