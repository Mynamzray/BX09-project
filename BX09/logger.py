import serial
import csv
from datetime import datetime

# ⚠️ 注意：根據你的 Arduino IDE，這裡要改成對應的 COM Port (截圖顯示是 COM6)
COM_PORT = 'COM6' 
BAUD_RATE = 115200

print(f"📡 正在監聽 {COM_PORT} ... (按 Ctrl+C 停止)")
print("請確保已經關閉 Arduino IDE 的 Serial Monitor 和 Plotter！")

try:
    # 連接 ESP32
    ser = serial.Serial(COM_PORT, BAUD_RATE, timeout=1)
    
    # 用當下時間建立一個不重複的 Excel/CSV 檔案
    filename = f"BX09_DataLog_{datetime.now().strftime('%Y%m%d_%H%M%S')}.csv"
    
    with open(filename, mode='w', newline='', encoding='utf-8') as file:
        writer = csv.writer(file)
        # 寫入 Excel 的標題列
        writer.writerow(["Shot_Time", "Elapsed_Time(ms)", "Raw_RPM", "Filtered_RPM", "Final_Peak_RPM"])
        
        is_recording = False
        
        while True:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            
            if line:
                if line == "===CSV_START===":
                    is_recording = True
                    current_time = datetime.now().strftime('%H:%M:%S')
                    print(f"\n🚀 [{current_time}] 偵測到發射！正在寫入數據...")
                    continue
                elif line == "===CSV_END===":
                    is_recording = False
                    print(f"✅ 記錄完成！請查看檔案: {filename}")
                    continue
                    
                if is_recording:
                    # 將收到的逗號字串切開，寫進 Excel 的格子裡
                    data = line.split(',')
                    if len(data) == 4:
                        row = [current_time] + data
                        writer.writerow(row)
                        file.flush() # 確保就算突然當機，前面的數據也已經存檔

except serial.SerialException:
    print(f"❌ 無法連線到 {COM_PORT}。請檢查線有沒有插好，或 Arduino IDE 的監控視窗是否忘記關閉？")
except KeyboardInterrupt:
    print("\n🛑 紀錄程式已手動停止。")
finally:
    if 'ser' in locals() and ser.is_open:
        ser.close()