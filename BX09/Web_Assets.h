// ==========================================
// Web_Assets.h
// 前端資源庫：將 HTML / CSS / JS 徹底解耦拆分
// ==========================================
#pragma once
#include <Arduino.h>

const char WEB_HTML_HEAD[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="zh-TW">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
    <title>BX-09 戰情室</title>
)rawliteral";

const char WEB_CSS[] PROGMEM = R"rawliteral(
    <style>
        :root {
            --bg: #09090b; --panel: #18181b; --border: #27272a;
            --text: #f4f4f5; --muted: #a1a1aa;
            --cyan: #06b6d4; --amber: #f59e0b; --red: #ef4444; --emerald: #10b981;
        }
        * { box-sizing: border-box; }
        body { 
            background: var(--bg); color: var(--text); margin: 0; padding: 0; 
            font-family: ui-monospace, SFMono-Regular, "SF Mono", Menlo, Consolas, monospace; 
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
        .card { background: var(--panel); border: 1px solid var(--border); border-radius: 12px; padding: 16px; position: relative; display: flex; flex-direction: column; align-items: center; justify-content: center; overflow: hidden; transition: all 0.3s ease;}
        .card-title { font-size: 0.75rem; color: var(--muted); letter-spacing: 2px; margin-bottom: 4px; text-transform: uppercase; }
        .card-value { font-size: 2.8rem; font-weight: 900; margin: 5px 0; height: 1em; line-height: 1em; display: flex; justify-content: center; align-items: center;}
        .card-desc { font-size: 0.7rem; color: #71717a; }
        .text-cyan { color: var(--cyan); text-shadow: 0 0 12px rgba(6,182,212,0.5); }
        .text-amber { color: var(--amber); text-shadow: 0 0 12px rgba(245,158,11,0.5); }
        .tag-tr { position: absolute; top: 0; right: 0; padding: 2px 8px; font-size: 0.65rem; border-bottom-left-radius: 8px; font-weight: bold; }
        .tag-cyan { background: rgba(6,182,212,0.15); color: var(--cyan); }
        .tag-amber { background: rgba(245,158,11,0.15); color: var(--amber); }
        .chart-container { background: var(--panel); border: 1px solid var(--border); border-radius: 12px; padding: 16px; min-height: 350px; display: flex; flex-direction: column; }
        .chart-header { display: flex; justify-content: space-between; align-items: center; font-size: 0.8rem; margin-bottom: 10px; color: var(--muted); }
        canvas { width: 100%; height: 300px; display: block; flex-grow: 1; cursor: crosshair; }
        
        .btn-clear { background: rgba(239, 68, 68, 0.1); border: 1px solid var(--red); color: var(--red); padding: 4px 10px; border-radius: 6px; font-size: 0.7rem; cursor: pointer; font-family: monospace; font-weight: bold; transition: all 0.2s;}
        .btn-clear:hover { background: rgba(239, 68, 68, 0.3); }
        .btn-export { background: rgba(6, 182, 212, 0.1); border: 1px solid var(--cyan); color: var(--cyan); padding: 4px 10px; border-radius: 6px; font-size: 0.7rem; cursor: pointer; font-family: monospace; font-weight: bold; transition: all 0.2s; margin-left: 10px;}
        .btn-export:hover { background: rgba(6, 182, 212, 0.3); }
        .btn-csv { background: rgba(245, 158, 11, 0.1); border: 1px solid var(--amber); color: var(--amber); padding: 4px 10px; border-radius: 6px; font-size: 0.7rem; cursor: pointer; font-family: monospace; font-weight: bold; transition: all 0.2s; margin-right: 10px;}
        .btn-csv:hover { background: rgba(245, 158, 11, 0.3); }
        .pb-badge { display: none; background: var(--amber); color: #000; font-size: 0.65rem; padding: 2px 8px; border-radius: 4px; position: absolute; top: -12px; font-weight: bold; box-shadow: 0 0 10px var(--amber); animation: pop 0.5s cubic-bezier(0.34, 1.56, 0.64, 1); z-index: 10;}
        @keyframes pop { 0% {transform: scale(0);} 100% {transform: scale(1);} }

        .odo-wrap { display: inline-flex; height: 1em; line-height: 1em; align-items: center; justify-content: center; }
        .odo-digit { display: inline-block; width: 1ch; height: 1em; position: relative; overflow: hidden; vertical-align: top; }
        .odo-trk { display: block; position: absolute; top: 0; left: 0; width: 100%; transition: transform 1500ms cubic-bezier(0.34, 1.56, 0.64, 1); will-change: transform; }
        .odo-trk span { display: flex; align-items: center; justify-content: center; height: 1em; line-height: 1em; }
        .odo-static { display: inline-block; height: 1em; line-height: 1em; vertical-align: top; }

        /* 🟢 全新並排的 Logs & Terminal 網格佈局 */
        .logs-grid { max-width: 1200px; margin: 0 auto; padding: 0 12px 40px 12px; display: grid; gap: 12px; grid-template-columns: 1fr; }
        @media(min-width: 1024px) { .logs-grid { grid-template-columns: 1fr 1fr; padding: 0 20px 40px 20px; gap: 20px; } }
        
        .logs-panel { background: var(--panel); border: 1px solid var(--border); border-radius: 12px; padding: 16px; display: flex; flex-direction: column; }
        
        /* 歷史紀錄表格樣式 */
        .table-wrapper { overflow-y: auto; flex-grow: 1; height: 400px; padding-right: 4px; }
        table { width: 100%; border-collapse: collapse; font-size: 0.8rem; }
        th { position: sticky; top: 0; background: var(--panel); z-index: 10; text-align: left; padding: 8px; color: var(--muted); border-bottom: 1px solid var(--border); }
        td { padding: 10px 8px; border-bottom: 1px solid rgba(39,39,42,0.5); }
        tr:hover { background: rgba(255,255,255,0.02); cursor: pointer; }
        
        /* 🟢 點擊高亮效果 */
        tr.active-row { background: rgba(6, 182, 212, 0.15) !important; }
        tr.active-row td:first-child { box-shadow: inset 3px 0 0 var(--cyan); }

        /* 網頁終端機 (Serial Terminal) 樣式 */
        .terminal-container { background: #000; border: 1px solid var(--border); border-radius: 8px; padding: 12px; font-size: 0.75rem; height: 400px; overflow-y: auto; display: flex; flex-direction: column; gap: 4px; box-shadow: inset 0 0 15px rgba(0,0,0,0.8); user-select: text; -webkit-user-select: text; }
        .log-time { color: #52525b; margin-right: 8px; user-select: none; }
        .log-info { color: var(--cyan); }
        .log-warn { color: var(--amber); }
        .log-err { color: var(--red); }
        .log-success { color: var(--emerald); }
        .log-raw { color: #71717a; }
        .log-esp { color: #d946ef; font-weight: bold; } 
    </style>
</head>
)rawliteral";

const char WEB_HTML_BODY[] PROGMEM = R"rawliteral(
<body>
    <header>
        <div>
            <span class="title">⚡ BX-09 TELEMETRY</span>
            <span class="badge-version">ANALYTICS PRO</span>
        </div>
        <div class="status-pill" id="statusBadge">
            <div class="dot" id="statusDot"></div>
            <span id="statusText">等待連線...</span>
        </div>
    </header>
    <div class="container">
        <div class="col">
            <div class="card" id="launchStateCard" style="border-color: var(--red);">
                <span class="card-title" id="launchStateTitle">SYSTEM STATUS</span>
                <span class="card-value" id="launchStateVal" style="font-size: 1.1rem; color: var(--red);">NO BLUETOOTH CONNECTION</span>
            </div>
            <!-- 🟢 新增：獨立的官方分數專屬卡片 -->
            <div class="card" style="box-shadow: 0 0 15px rgba(245,158,11,0.1); border-color: rgba(245,158,11,0.3);">
                <div class="tag-tr tag-amber">OFFICIAL</div>
                <span class="card-title text-amber">BBP OFFICIAL SP</span>
                <span class="card-value text-amber" id="origSpVal">0</span>
                <span class="card-desc">官方晶片結算成績</span>
            </div>
            <div class="card" style="box-shadow: 0 0 15px rgba(6,182,212,0.1); border-color: rgba(6,182,212,0.3);">
                <div class="tag-tr tag-cyan">ENGINE</div>
                <div id="pbBadge" class="pb-badge">🏆 NEW PERSONAL BEST!</div>
                <span class="card-title text-cyan">PEAK RPM</span>
                <span class="card-value text-cyan" id="peakVal">0</span>
                <span class="card-desc">最高啟動轉速 <span id="rawPeakVal" style="color:var(--red); margin-left:5px;">(Raw: 0)</span></span>
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
        <div class="chart-container">
            <div class="chart-header">
                <span style="font-weight: bold; letter-spacing: 1px;">📈 SPIN ACCELERATION PROFILE</span>
                <div style="display:flex; align-items:center;">
                    <button class="btn-export" onclick="exportChart()">📸 SAVE PNG</button>
                </div>
            </div>
            <div style="position: relative; width: 100%; height: 100%;">
                <canvas id="telemetryChart"></canvas>
            </div>
        </div>
    </div>
    
    <!-- 🟢 全新的並排 Logs 網格 -->
    <div class="logs-grid">
        <!-- 面板 1：歷史紀錄表 -->
        <div class="logs-panel">
            <div style="display:flex; justify-content:space-between; align-items:center; margin-bottom: 10px;">
                <h3 style="font-size: 0.8rem; color: var(--muted); letter-spacing: 1px; text-transform: uppercase;">📋 Session Launch Logs</h3>
                <div>
                    <button class="btn-csv" onclick="exportCSV()">📊 EXPORT CSV</button>
                    <button class="btn-clear" onclick="clearHistory()">CLEAR DATA</button>
                </div>
            </div>
            <div class="table-wrapper">
                <table>
                    <thead>
                        <tr>
                            <th>ID</th><th>時間</th><th style="color: var(--cyan);">最高(Peak)</th><th style="color: var(--red);">原始峰值</th><th style="color: var(--amber);">平均(Avg)</th><th>點數</th>
                        </tr>
                    </thead>
                    <tbody id="logTableBody">
                        <tr id="emptyRow"><td colspan="6" style="text-align: center; color: var(--muted);">等待發射數據...</td></tr>
                    </tbody>
                </table>
            </div>
        </div>
        
        <!-- 面板 2：網頁終端機 -->
        <div class="logs-panel">
            <div style="display:flex; justify-content:space-between; align-items:center; margin-bottom: 10px;">
                <h3 style="font-size: 0.8rem; color: var(--muted); letter-spacing: 1px; text-transform: uppercase;">🖥️ System Activity & Serial Terminal</h3>
                <button class="btn-clear" onclick="document.getElementById('terminalBody').innerHTML='<div style=\'color:var(--muted)\'>[System] Terminal Cleared.</div>'">CLEAR LOG</button>
            </div>
            <div class="terminal-container" id="terminalBody">
                <div style="color:var(--muted)">[System] Terminal Initialized. Waiting for events...</div>
            </div>
        </div>
    </div>
)rawliteral";

const char WEB_JS[] PROGMEM = R"rawliteral(
    <script>
        function sysLog(msg, level='info') {
            const term = document.getElementById('terminalBody');
            if(!term) return;
            const now = new Date();
            const time = now.getHours().toString().padStart(2,'0') + ':' + 
                         now.getMinutes().toString().padStart(2,'0') + ':' + 
                         now.getSeconds().toString().padStart(2,'0') + '.' + 
                         now.getMilliseconds().toString().padStart(3,'0');
            
            const div = document.createElement('div');
            div.innerHTML = `<span class="log-time">[${time}]</span><span class="log-${level}">${msg}</span>`;
            term.appendChild(div);
            
            if(term.childNodes.length > 200) term.removeChild(term.firstChild);
            term.scrollTop = term.scrollHeight; 
        }

        function updateOdometer(id, value) {
            const el = document.getElementById(id);
            if (!el) return;
            const strVal = value.toLocaleString();
            const formatStr = strVal.replace(/[0-9]/g, 'd');
            
            if (el.dataset.format !== formatStr) {
                el.innerHTML = '';
                const wrap = document.createElement('div');
                wrap.className = 'odo-wrap';
                for (let char of strVal) {
                    if (/[0-9]/.test(char)) {
                        const digit = document.createElement('div');
                        digit.className = 'odo-digit';
                        const trk = document.createElement('div');
                        trk.className = 'odo-trk';
                        for(let d = 0; d <= 9; d++) {
                            const num = document.createElement('span');
                            num.innerText = d;
                            trk.appendChild(num);
                        }
                        digit.appendChild(trk);
                        wrap.appendChild(digit);
                    } else {
                        const stat = document.createElement('span');
                        stat.className = 'odo-static';
                        stat.innerText = char;
                        wrap.appendChild(stat);
                    }
                }
                el.appendChild(wrap);
                el.dataset.format = formatStr;
                void el.offsetWidth; 
            }
            
            const trks = el.querySelectorAll('.odo-trk');
            let idx = 0;
            for (let char of strVal) {
                if (/[0-9]/.test(char)) {
                    trks[idx].style.transform = `translateY(-${parseInt(char) * 10}%)`;
                    idx++;
                }
            }
        }

        let chartState = { tArr: [], rawArr: [], filteredArr: [], pbData: null, hoverIdx: -1 };

        function drawChart(tArr, rawArr, filteredArr, pbData = null) {
            chartState.tArr = tArr || [];
            chartState.rawArr = rawArr || [];
            chartState.filteredArr = filteredArr || [];
            chartState.pbData = pbData;
            chartState.hoverIdx = -1;
            renderCanvas();
        }

        function renderCanvas() {
            const canvas = document.getElementById('telemetryChart');
            const ctx = canvas.getContext('2d');
            const dpr = window.devicePixelRatio || 1;
            const rect = canvas.parentNode.getBoundingClientRect();
            canvas.width = rect.width * dpr;
            canvas.height = rect.height * dpr;
            ctx.scale(dpr, dpr);
            
            const w = rect.width, h = rect.height;
            ctx.fillStyle = '#18181b'; 
            ctx.fillRect(0, 0, w, h);
            
            const { tArr, rawArr, filteredArr, pbData, hoverIdx } = chartState;
            if(!tArr || tArr.length === 0) return;

            const padX = 40, padY = 20;
            const drawW = w - padX - 10, drawH = h - padY * 2;
            let maxT = tArr[tArr.length-1] || 1;
            let maxRPM = Math.max(...filteredArr, ...rawArr, 5000);
            
            if (pbData && pbData.t && pbData.filtered) {
                maxT = Math.max(maxT, pbData.t[pbData.t.length-1] || 1);
                maxRPM = Math.max(maxRPM, ...pbData.filtered, pbData.peak || 0);
            }

            const mapX = (t) => padX + (t / maxT) * drawW;
            const mapY = (v) => h - padY - (v / maxRPM) * drawH;

            ctx.lineWidth = 1; ctx.textAlign = 'right'; ctx.textBaseline = 'middle'; ctx.font = '10px monospace';
            for(let i=0; i<=5; i++) {
                let val = Math.round(maxRPM * (i/5));
                let y = mapY(val);
                ctx.strokeStyle = '#27272a';
                ctx.beginPath(); ctx.moveTo(padX, y); ctx.lineTo(w, y); ctx.stroke();
                ctx.fillStyle = '#a1a1aa'; ctx.fillText(val, padX - 5, y);
            }

            ctx.textAlign = 'center'; ctx.textBaseline = 'top';
            ctx.fillText("0ms", padX, h - padY + 5);
            ctx.fillText(maxT + "ms", w - 10, h - padY + 5);

            if (pbData && pbData.t && pbData.filtered) {
                ctx.strokeStyle = 'rgba(245, 158, 11, 0.4)';
                ctx.setLineDash([5, 5]); 
                ctx.lineWidth = 2;
                ctx.beginPath();
                for(let i=0; i<pbData.t.length; i++) {
                    let x = mapX(pbData.t[i]), y = mapY(pbData.filtered[i]);
                    if(i===0) ctx.moveTo(x,y); else ctx.lineTo(x,y);
                }
                ctx.stroke();
                ctx.setLineDash([]); 

                ctx.fillStyle = 'rgba(245, 158, 11, 0.7)';
                ctx.textAlign = 'left';
                ctx.textBaseline = 'bottom';
                ctx.fillText(`🏆 PB: ${pbData.peak} RPM`, padX + 10, mapY(pbData.peak) - 5);
            }

            ctx.strokeStyle = 'rgba(239, 68, 68, 0.4)'; ctx.lineWidth = 2; ctx.beginPath();
            for(let i=0; i<tArr.length; i++) {
                let x = mapX(tArr[i]), y = mapY(rawArr[i]);
                if(i===0) ctx.moveTo(x,y); else ctx.lineTo(x,y);
            } ctx.stroke();

            ctx.strokeStyle = '#06b6d4'; ctx.lineWidth = 3; ctx.beginPath();
            for(let i=0; i<tArr.length; i++) {
                let x = mapX(tArr[i]), y = mapY(filteredArr[i]);
                if(i===0) ctx.moveTo(x,y); else ctx.lineTo(x,y);
            } ctx.stroke();

            if (hoverIdx !== -1 && hoverIdx < tArr.length) {
                let t = tArr[hoverIdx];
                let r = rawArr[hoverIdx];
                let f = filteredArr[hoverIdx];

                let x = mapX(t);
                let yRaw = mapY(r);
                let yFilt = mapY(f);

                ctx.strokeStyle = 'rgba(255, 255, 255, 0.3)';
                ctx.setLineDash([4, 4]);
                ctx.lineWidth = 1;
                ctx.beginPath(); ctx.moveTo(x, padY); ctx.lineTo(x, h - padY); ctx.stroke();
                ctx.setLineDash([]);

                ctx.fillStyle = '#ef4444';
                ctx.beginPath(); ctx.arc(x, yRaw, 4, 0, Math.PI*2); ctx.fill();
                ctx.fillStyle = '#06b6d4';
                ctx.beginPath(); ctx.arc(x, yFilt, 4, 0, Math.PI*2); ctx.fill();

                let ttText = `${t}ms | Filt: ${f} | Raw: ${r}`;
                ctx.font = '11px ui-monospace, SFMono-Regular, monospace';
                let ttWidth = ctx.measureText(ttText).width + 20;
                let ttX = x - ttWidth / 2;
                if (ttX < padX) ttX = padX;
                if (ttX + ttWidth > w - 10) ttX = w - 10 - ttWidth;

                let ttY = Math.min(yRaw, yFilt) - 30;
                if (ttY < 10) ttY = Math.max(yRaw, yFilt) + 15;

                ctx.fillStyle = 'rgba(24, 24, 27, 0.95)';
                ctx.strokeStyle = '#3f3f46';
                ctx.lineWidth = 1;
                ctx.beginPath();
                if(ctx.roundRect) { ctx.roundRect(ttX, ttY, ttWidth, 22, 5); } 
                else { ctx.rect(ttX, ttY, ttWidth, 22); }
                ctx.fill(); ctx.stroke();

                ctx.fillStyle = '#f4f4f5';
                ctx.textAlign = 'center';
                ctx.textBaseline = 'middle';
                ctx.fillText(ttText, ttX + ttWidth / 2, ttY + 11);
            }
        }

        function exportChart() {
            sysLog('📸 正在匯出戰績圖表 PNG...', 'info');
            const canvas = document.getElementById('telemetryChart');
            const dataURL = canvas.toDataURL('image/png');
            const link = document.createElement('a');
            link.download = `BX09_Record_${new Date().getTime()}.png`;
            link.href = dataURL;
            link.click();
        }

        function exportCSV() {
            sysLog('📊 正在生成並匯出 Raw Data CSV...', 'info');
            if (launchHistory.length === 0) {
                sysLog('⚠️ 歷史紀錄為空，無法匯出 CSV', 'warn');
                return;
            }

            let csvContent = "\uFEFFShot_ID,Date_Time,Elapsed_ms,Raw_RPM,Filtered_RPM\n";
            launchHistory.forEach(shot => {
                if (!shot.t || !shot.raw || !shot.filtered) return;
                for (let i = 0; i < shot.t.length; i++) {
                    let ms = shot.t[i] !== undefined ? shot.t[i] : 0;
                    let raw = shot.raw[i] !== undefined ? shot.raw[i] : 0;
                    let filtered = shot.filtered[i] !== undefined ? shot.filtered[i] : 0;
                    csvContent += `${shot.id},${shot.time},${ms},${raw},${filtered}\n`;
                }
            });

            const blob = new Blob([csvContent], { type: 'text/csv;charset=utf-8;' });
            const url = URL.createObjectURL(blob);
            const link = document.createElement("a");
            link.setAttribute("href", url);
            link.setAttribute("download", `BX09_RawData_${new Date().getTime()}.csv`);
            document.body.appendChild(link);
            link.click();
            document.body.removeChild(link);
            
            sysLog('✅ CSV 匯出完成！可用 Excel 或 Python 進行深度分析。', 'success');
        }

        let socket;
        let launchHistory = [];
        let personalBest = null; 
        let currentBle = false;
        let currentBey = false;
        
        // 🟢 全域追蹤目前選取的發射 ID
        let activeShotId = null;

        function loadHistory() {
            try {
                const savedPB = localStorage.getItem('bx09_pb');
                if (savedPB) personalBest = JSON.parse(savedPB);

                const saved = localStorage.getItem('bx09_history');
                if (saved) {
                    launchHistory = JSON.parse(saved);
                    sysLog(`📂 從手機硬碟成功載入 ${launchHistory.length} 筆發射歷史與最高紀錄。`, 'success');
                    
                    if(launchHistory.length > 0) {
                        const latest = launchHistory[0];
                        activeShotId = latest.id; // 初始化高亮最新一筆
                        
                        updateOdometer('peakVal', latest.peak);
                        if (latest.raw_peak) document.getElementById('rawPeakVal').innerText = `(Raw: ${latest.raw_peak.toLocaleString()})`;
                        updateOdometer('avgVal', Math.round(latest.avg));
                        const totalDur = latest.t.length > 0 ? latest.t[latest.t.length - 1] : 0;
                        document.getElementById('durationVal').innerHTML = `${totalDur}<span style="font-size: 1rem; font-weight: normal;">ms</span>`;
                        
                        const isLatestPb = personalBest && latest.session_id === personalBest.session_id && latest.shot_id === personalBest.shot_id;
                        drawChart(latest.t, latest.raw, latest.filtered, isLatestPb ? null : personalBest);
                    }
                    renderTable();
                }
            } catch(e) {}
        }
        
        function clearHistory() {
            if(confirm("確定要刪除所有歷史紀錄與生涯最高紀錄嗎？")) {
                sysLog('🗑️ 警告：已清空所有歷史與最高紀錄！', 'warn');
                launchHistory = [];
                personalBest = null;
                activeShotId = null;
                try { 
                    localStorage.removeItem('bx09_history'); 
                    localStorage.removeItem('bx09_pb'); 
                } catch(e) {}
                
                renderTable();
                drawChart([0, 100], [0, 0], [0, 0]);
                updateOdometer('peakVal', 0);
                let rawEl = document.getElementById('rawPeakVal');
                if (rawEl) rawEl.innerText = "(Raw: 0)";
                updateOdometer('avgVal', 0);
                document.getElementById('durationVal').innerHTML = `0<span style="font-size: 1rem; font-weight: normal;">ms</span>`;
            }
        }

        function initWebSocket() {
            sysLog('🔗 正在嘗試建立 WebSocket 高速通道 (ws://192.168.4.1/ws)...', 'info');
            socket = new WebSocket('ws://192.168.4.1/ws');
            socket.onopen = () => {
                document.getElementById('statusDot').className = 'dot connected';
                document.getElementById('statusText').innerText = '已連線至 BX-09';
                document.getElementById('statusText').style.color = 'var(--emerald)';
                sysLog('✅ WebSocket 高速通道連線成功！', 'success');
            };
            socket.onclose = () => {
                document.getElementById('statusDot').className = 'dot';
                document.getElementById('statusText').innerText = '已斷線，重連中...';
                document.getElementById('statusText').style.color = 'var(--red)';
                sysLog('❌ WebSocket 已斷開連線，2秒後自動重試...', 'err');
                setTimeout(initWebSocket, 2000);
            };
            socket.onmessage = (event) => {
                try {
                    let rawDataStr = event.data;
                    if(rawDataStr.length > 100) rawDataStr = rawDataStr.substring(0, 100) + '... (長度:' + event.data.length + ' bytes)';

                    const data = JSON.parse(event.data);
                    if (data.type === "log") {
                        sysLog(`📡 [ESP32 核心] ${data.msg}`, 'esp');
                    }
                    else if (data.type === "launch") handleNewLaunch(data);
                    else if (data.type === "status") handleStatus(data);
                    else if (data.type === "sync") handleSync(data);
                    // 🟢 新增：攔截官方分數資料，更新到儀表板，並將歷史陣列展開到終端機
                    else if (data.type === "official_data") {
                        sysLog(`🏆 [官方同步] 獲取官方最終成績: ${data.origSP} RPM`, 'amber');
                        updateOdometer('origSpVal', data.origSP);
                        
                        let histStr = data.history.join(' RPM, ');
                        if(data.history.length > 0) histStr += " RPM";
                        sysLog(`📜 [官方歷史] 最近 ${data.history.length} 筆紀錄: [${histStr}]`, 'info');
                    }
                    // 🟢 新增：攔截電池封包，並以漂亮的格式印在終端機上！
                    else if (data.type === "battery") {
                        let icon = data.isCharging ? '⚡' : '🔋';
                        let color = data.percentage <= 20 ? 'err' : (data.percentage <= 50 ? 'warn' : 'info');
                        sysLog(`${icon} [電源狀態] 電量: ${data.percentage}% | 電壓: ${data.voltage}V`, color);
                    }
                } catch (err) {}
            };
        }

        function updateStatusUI() {
            const stateVal = document.getElementById('launchStateVal');
            const stateCard = document.getElementById('launchStateCard');
            if (!currentBle) {
                stateVal.innerText = "NO BLUETOOTH CONNECTION";
                stateVal.style.color = "var(--red)"; stateCard.style.borderColor = "var(--red)";
            } else if (!currentBey) {
                stateVal.innerText = "INSTALL BEYBLADE";
                stateVal.style.color = "var(--cyan)"; stateCard.style.borderColor = "var(--cyan)";
            } else {
                stateVal.innerText = "READY FOR LAUNCH";
                stateVal.style.color = "var(--amber)"; stateCard.style.borderColor = "var(--amber)";
            }
        }

        function handleStatus(data) {
            if (currentBle !== data.bleConnected || currentBey !== data.beyInstalled) {
                sysLog(`🔄 狀態切換 - 藍牙:${data.bleConnected ? '連線' : '斷開'} | 陀螺:${data.beyInstalled ? '已安裝' : '未安裝'}`, 'info');
            }
            currentBle = data.bleConnected;
            currentBey = data.beyInstalled;
            updateStatusUI();
        }

        function handleSync(data) {
            sysLog('🔄 收到設備重啟初始同步資料', 'info');
            handleStatus(data);
        }

        function handleNewLaunch(data) {
            if (data.session_id) {
                const exists = launchHistory.find(item => item.session_id === data.session_id && item.shot_id === data.shot_id);
                if (exists) return; 
            }

            sysLog(`🚀 完美解析發射資料！(ID: #${data.shot_id} | Peak: ${data.peak} RPM)`, 'success');

            const nextId = launchHistory.length > 0 ? launchHistory[0].id + 1 : 1;
            activeShotId = nextId; // 🟢 自動高亮新進入的發射紀錄

            // 在網頁終端機印出原汁原味的 CSV 區塊
            let csvBlock = `<div style="font-family: monospace; font-size: 0.8rem; line-height: 1.3; background: #09090b; padding: 8px; border-radius: 6px; border: 1px dashed #3f3f46; margin: 6px 0; user-select: text; -webkit-user-select: text;">`;
            csvBlock += `<span style="color:var(--cyan)">===CSV_START===</span><br>`;
            for(let i = 0; i < data.t.length; i++) {
                csvBlock += `<span style="color:var(--muted)">${data.t[i]},${data.raw[i]},${data.filtered[i]},${data.peak}</span><br>`;
            }
            csvBlock += `<span style="color:var(--cyan)">===CSV_END===</span></div>`;
            sysLog(`📥 [CSV Dump] 擷取到完整序列：<br>${csvBlock}`, 'raw');

            if (!data.is_history) {
                document.getElementById('launchStateCard').style.borderColor = 'var(--emerald)';
                document.getElementById('launchStateVal').style.color = 'var(--emerald)';
                document.getElementById('launchStateVal').innerText = '💥 LAUNCH DETECTED!';
                setTimeout(() => { updateStatusUI(); }, 2500);
            }

            let isNewPB = false;
            if (!personalBest || data.peak > personalBest.peak) {
                personalBest = data;
                isNewPB = true;
                sysLog(`🏆 恭喜！打破生涯最高紀錄：${data.peak} RPM！`, 'amber');
                try { localStorage.setItem('bx09_pb', JSON.stringify(personalBest)); } catch(e){}
                
                const pbBadge = document.getElementById('pbBadge');
                pbBadge.style.display = 'block';
                setTimeout(() => { pbBadge.style.display = 'none'; }, 4000);
            }

            updateOdometer('peakVal', data.peak);
            let rawEl = document.getElementById('rawPeakVal');
            if (rawEl && data.raw_peak) rawEl.innerText = `(Raw: ${data.raw_peak.toLocaleString()})`;
            
            updateOdometer('avgVal', Math.round(data.avg));
            const totalDuration = data.t.length > 0 ? data.t[data.t.length - 1] : 0;
            document.getElementById('durationVal').innerHTML = `${totalDuration}<span style="font-size: 1rem; font-weight: normal;">ms</span>`;

            drawChart(data.t, data.raw, data.filtered, isNewPB ? null : personalBest);

            const timestamp = new Date().toLocaleTimeString();
            launchHistory.unshift({ 
                id: nextId, 
                time: timestamp, 
                session_id: data.session_id,
                shot_id: data.shot_id,
                ...data 
            });
            if (launchHistory.length > 100) launchHistory.pop();
            
            try { localStorage.setItem('bx09_history', JSON.stringify(launchHistory)); } catch(e) {}
            
            renderTable();
        }

        function renderTable() {
            const tbody = document.getElementById('logTableBody');
            tbody.innerHTML = '';
            if (launchHistory.length === 0) {
                tbody.innerHTML = `<tr id="emptyRow"><td colspan="6" style="text-align: center; color: var(--muted);">等待發射數據...</td></tr>`;
                return;
            }
            launchHistory.forEach((item) => {
                const tr = document.createElement('tr');
                // 🟢 判斷是否為目前選取的紀錄，加上高亮 class
                if (item.id === activeShotId) tr.className = 'active-row';
                
                tr.innerHTML = `
                    <td style="font-weight:bold;">#${item.id}</td>
                    <td>${item.time}</td>
                    <td style="color:var(--cyan); font-weight:bold;">${item.peak.toLocaleString()}</td>
                    <td style="color:var(--red);">${item.raw_peak ? item.raw_peak.toLocaleString() : '-'}</td>
                    <td style="color:var(--amber);">${Math.round(item.avg).toLocaleString()}</td>
                    <td>${item.size}</td>
                `;
                
                tr.onclick = () => {
                    // 🟢 點擊時更新高亮狀態
                    activeShotId = item.id;
                    document.querySelectorAll('#logTableBody tr').forEach(r => r.classList.remove('active-row'));
                    tr.classList.add('active-row');
                    
                    sysLog(`🔍 正在重播檢閱第 #${item.id} 筆歷史紀錄`, 'info');
                    
                    // 🟢 連動印出這筆紀錄專屬的 CSV 區塊！
                    let csvBlock = `<div style="font-family: monospace; font-size: 0.8rem; line-height: 1.3; background: #09090b; padding: 8px; border-radius: 6px; border: 1px dashed #06b6d4; margin: 6px 0; user-select: text; -webkit-user-select: text;">`;
                    csvBlock += `<span style="color:var(--cyan)">===CSV_START===</span><br>`;
                    for(let i = 0; i < item.t.length; i++) {
                        let ms = item.t[i] !== undefined ? item.t[i] : 0;
                        let raw = item.raw[i] !== undefined ? item.raw[i] : 0;
                        let filtered = item.filtered[i] !== undefined ? item.filtered[i] : 0;
                        csvBlock += `<span style="color:var(--muted)">${ms},${raw},${filtered},${item.peak}</span><br>`;
                    }
                    csvBlock += `<span style="color:var(--cyan)">===CSV_END===</span></div>`;
                    sysLog(`📥 [CSV Dump] 歷史紀錄 #${item.id} 陣列展開：<br>${csvBlock}`, 'raw');
                    
                    updateOdometer('peakVal', item.peak);
                    let rawEl = document.getElementById('rawPeakVal');
                    if (rawEl && item.raw_peak) rawEl.innerText = `(Raw: ${item.raw_peak.toLocaleString()})`;
                    
                    updateOdometer('avgVal', Math.round(item.avg));
                    document.getElementById('durationVal').innerHTML = `${item.t[item.t.length - 1]}<span style="font-size: 1rem; font-weight: normal;">ms</span>`;
                    
                    const isItemPb = personalBest && item.session_id === personalBest.session_id && item.shot_id === personalBest.shot_id;
                    drawChart(item.t, item.raw, item.filtered, isItemPb ? null : personalBest);
                    
                    const stateVal = document.getElementById('launchStateVal');
                    stateVal.innerText = `🔍 REVIEWING LAUNCH #${item.id}`;
                    stateVal.style.color = "var(--cyan)"; 
                    document.getElementById('launchStateCard').style.borderColor = "var(--cyan)";
                    
                    setTimeout(updateStatusUI, 3000);
                };
                tbody.appendChild(tr);
            });
        }

        window.onload = () => {
            sysLog('🟢 系統介面初始化中...', 'info');
            initWebSocket();
            drawChart([0, 100], [0, 0], [0, 0]); 
            updateOdometer('peakVal', 0);
            updateOdometer('avgVal', 0);
            loadHistory(); 
            
            const canvas = document.getElementById('telemetryChart');
            
            function handleHover(e) {
                const { tArr } = chartState;
                if(!tArr || tArr.length === 0) return;
                
                const rect = canvas.getBoundingClientRect();
                const clientX = e.touches ? e.touches[0].clientX : e.clientX;
                const mouseX = clientX - rect.left;
                
                const padX = 40;
                const drawW = rect.width - padX - 10;
                const maxT = tArr[tArr.length-1] || 1;
                
                let targetT = ((mouseX - padX) / drawW) * maxT;
                let closestIdx = -1;
                let minDiff = Infinity;
                
                for(let i=0; i<tArr.length; i++) {
                    let diff = Math.abs(tArr[i] - targetT);
                    if(diff < minDiff) {
                        minDiff = diff;
                        closestIdx = i;
                    }
                }
                
                if (mouseX >= padX - 10 && mouseX <= rect.width) {
                    if (chartState.hoverIdx !== closestIdx) {
                        chartState.hoverIdx = closestIdx;
                        renderCanvas();
                    }
                } else {
                    if (chartState.hoverIdx !== -1) {
                        chartState.hoverIdx = -1;
                        renderCanvas();
                    }
                }
            }
            
            const clearHover = () => { chartState.hoverIdx = -1; renderCanvas(); };
            
            canvas.addEventListener('mousemove', handleHover);
            canvas.addEventListener('touchmove', handleHover, {passive: true});
            canvas.addEventListener('mouseleave', clearHover);
            canvas.addEventListener('touchend', clearHover);
            
            window.addEventListener('resize', () => {
                if(launchHistory.length > 0) {
                    const latest = launchHistory[0];
                    const isLatestPb = personalBest && latest.session_id === personalBest.session_id && latest.shot_id === personalBest.shot_id;
                    drawChart(latest.t, latest.raw, latest.filtered, isLatestPb ? null : personalBest);
                } else {
                    drawChart([0, 100], [0, 0], [0, 0]);
                }
            });
        };
    </script>
</body>
</html>
)rawliteral";