import serial
import csv
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation
from datetime import datetime
import os

# ⚠️ 這裡設定你的 COM Port
COM_PORT = 'COM6'
BAUD_RATE = 115200

# 1. 建立當次測試的 Log 檔案 (以時間命名，放在同一個資料夾)
current_date = datetime.now().strftime('%Y%m%d_%H%M%S')
log_filename = f"BX09_LiveLog_{current_date}.csv"

# 寫入標題列
with open(log_filename, mode='w', newline='', encoding='utf-8') as f:
    writer = csv.writer(f)
    writer.writerow(["Shot_Time", "Elapsed_Time(ms)", "Raw_RPM", "Filtered_RPM", "Final_Peak_RPM"])

print(f"📁 歷史日誌已建立: {log_filename}")
print(f"📡 正在開啟 {COM_PORT} 監聽...")

# 2. 初始化圖表
fig, ax = plt.subplots(figsize=(10, 6))
raw_line, = ax.plot([], [], label='Raw RPM (Original)', color='#A0A0A0', linewidth=2, linestyle='--')
filtered_line, = ax.plot([], [], label='Filtered RPM (6000 Threshold)', color='#FF6B6B', linewidth=3)

ax.set_title("⚡ BX-09 Real-time Analyzer & Logger ⚡", fontsize=14, fontweight='bold')
ax.set_xlabel("Data Point Index", fontsize=12)
ax.set_ylabel("Rotational Speed (RPM)", fontsize=12)
ax.grid(True, linestyle=':', alpha=0.6)
ax.legend(loc='upper left')

# 數據快取
x_data, y_raw, y_filtered = [], [], []
is_recording = False
current_shot_time = ""

# 開啟串口
try:
    ser = serial.Serial(COM_PORT, BAUD_RATE, timeout=0.1)
except Exception as e:
    print(f"❌ 無法開啟 {COM_PORT}，請關閉 Arduino IDE 的監控視窗！\n錯誤原因: {e}")
    exit()

def update(frame):
    global is_recording, x_data, y_raw, y_filtered, current_shot_time
    
    if ser.in_waiting > 0:
        try:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            
            if line == "===CSV_START===":
                is_recording = True
                current_shot_time = datetime.now().strftime('%H:%M:%S')
                print(f"🚀 [{current_shot_time}] 偵測到拉動！繪圖並寫入日誌中...")
                x_data.clear()
                y_raw.clear()
                y_filtered.clear()
                return raw_line, filtered_line
                
            elif line == "===CSV_END===":
                is_recording = False
                if x_data:
                    ax.set_xlim(1, max(x_data) + 1)
                    ax.set_ylim(0, max(max(y_raw), max(y_filtered)) * 1.1)
                    fig.canvas.draw()
                print(f"✅ 這一局數據已安全存入日誌。")
                return raw_line, filtered_line
            
            if is_recording:
                data = line.split(',')
                if len(data) == 4: # 經過時間(ms), 原始, 過濾, 本局最高
                    idx = len(x_data) + 1
                    x_data.append(idx)
                    y_raw.append(int(data[1]))
                    y_filtered.append(int(data[2]))
                    
                    # 🟢 即時更新畫面
                    raw_line.set_data(x_data, y_raw)
                    filtered_line.set_data(x_data, y_filtered)
                    
                    # 🟢 同步寫入 CSV 檔案
                    with open(log_filename, mode='a', newline='', encoding='utf-8') as f:
                        writer = csv.writer(f)
                        # 格式：發射時間, 經過時間, 原始, 過濾, 最終最高轉速
                        writer.writerow([current_shot_time, data[0], data[1], data[2], data[3]])
                        
        except Exception:
            pass
            
    return raw_line, filtered_line

# 啟動高速動畫監聽
ani = FuncAnimation(fig, update, blit=True, interval=20, cache_frame_data=False)
plt.show()

ser.close()
print("🛑 監聽已結束。")