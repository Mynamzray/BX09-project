import socket
import threading
import queue
import csv
import subprocess
from datetime import datetime
import customtkinter as ctk
import matplotlib.pyplot as plt
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
import warnings

warnings.filterwarnings("ignore")

WIFI_SSID = "BX09_Telemetry"
UDP_IP = "0.0.0.0"
UDP_PORT = 12345

data_queue = queue.Queue()

x_data, y_raw, y_filtered = [], [], []
history_rpms = []
is_recording = False
current_shot_time = ""
current_shot_peak = 0

current_date = datetime.now().strftime('%Y%m%d_%H%M%S')
log_filename = f"BX09_Log_{current_date}.csv"
with open(log_filename, mode='w', newline='', encoding='utf-8') as f:
    writer = csv.writer(f)
    writer.writerow(["Shot_Time", "Elapsed_Time(ms)", "Raw_RPM", "Filtered_RPM", "Final_Peak_RPM"])

def udp_listener():
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        sock.bind((UDP_IP, UDP_PORT))
    except Exception as e:
        data_queue.put({"type": "SYS", "msg": f"PORT BIND ERROR: {e}"})
        return

    data_queue.put({"type": "SYS", "msg": "UDP LISTENER ACTIVATED."})
    
    while True:
        try:
            data, addr = sock.recvfrom(1024)
            line = data.decode('utf-8', errors='ignore').strip()
            data_queue.put({"type": "UDP", "msg": line})
        except Exception as e:
            pass

def check_wifi():
    try:
        result = subprocess.run(['ping', '-n', '1', '-w', '500', '192.168.4.1'], 
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, creationflags=0x08000000)
        return result.returncode == 0
    except:
        return False

ctk.set_appearance_mode("dark")
ctk.set_default_color_theme("blue")

class TelemetryApp(ctk.CTk):
    def __init__(self):
        super().__init__()

        self.title("BX-09 TELEMETRY DASHBOARD V3.1")
        self.geometry("1400x800")
        self.configure(fg_color="#0A0A0A")
        
        # 優雅退場機制
        self.is_running = True
        self.protocol("WM_DELETE_WINDOW", self.on_closing)

        # 核心排版：True 50/50 Split
        self.grid_rowconfigure(1, weight=1)
        self.grid_columnconfigure(0, weight=1, uniform="half") 
        self.grid_columnconfigure(1, weight=1, uniform="half")

        # --- 頂部狀態列 ---
        self.header_frame = ctk.CTkFrame(self, fg_color="#121212", corner_radius=0)
        self.header_frame.grid(row=0, column=0, columnspan=2, sticky="ew", padx=10, pady=(10, 5))

        self.lbl_wifi = ctk.CTkLabel(self.header_frame, text="[ WIFI: SCANNING... ]", text_color="gray", font=("Consolas", 14, "bold"))
        self.lbl_wifi.pack(side="left", padx=20, pady=10)

        self.lbl_status = ctk.CTkLabel(self.header_frame, text="[ STATUS: WAITING FOR SIGNAL ]", text_color="#FFC300", font=("Consolas", 14, "bold"))
        self.lbl_status.pack(side="left", padx=20, pady=10)

        # 右上角按鈕區塊
        self.btn_disconnect = ctk.CTkButton(self.header_frame, text="DISCONNECT", fg_color="#FF0033", hover_color="#C70039", 
                                            font=("Consolas", 12, "bold"), command=self.force_disconnect)
        self.btn_disconnect.pack(side="right", padx=(10, 20), pady=10)

        self.btn_connect = ctk.CTkButton(self.header_frame, text="CONNECT WIFI", fg_color="#1F618D", hover_color="#2980B9", 
                                         font=("Consolas", 12, "bold"), command=self.force_connect)
        self.btn_connect.pack(side="right", padx=0, pady=10)

        # --- 左側面板 ---
        self.left_frame = ctk.CTkFrame(self, fg_color="#121212")
        self.left_frame.grid(row=1, column=0, sticky="nsew", padx=(10, 5), pady=(5, 10))
        self.left_frame.grid_rowconfigure(3, weight=1)
        self.left_frame.grid_rowconfigure(5, weight=1)
        self.left_frame.grid_columnconfigure(0, weight=1)

        ctk.CTkLabel(self.left_frame, text="PEAK RPM", text_color="gray", font=("Consolas", 14, "bold")).grid(row=0, column=0, sticky="w", padx=20, pady=(20, 0))
        self.lbl_peak = ctk.CTkLabel(self.left_frame, text="0", text_color="#00FFCC", font=("Consolas", 72, "bold"))
        self.lbl_peak.grid(row=1, column=0, sticky="w", padx=20, pady=(0, 20))

        ctk.CTkLabel(self.left_frame, text="RECENT HISTORY (Top 10)", text_color="gray", font=("Consolas", 14, "bold")).grid(row=2, column=0, sticky="w", padx=20, pady=(5, 5))
        self.box_history = ctk.CTkTextbox(self.left_frame, fg_color="#0A0A0A", text_color="white", font=("Consolas", 14), state="disabled")
        self.box_history.grid(row=3, column=0, sticky="nsew", padx=20, pady=(0, 15))
        self.update_history_ui()

        ctk.CTkLabel(self.left_frame, text="> SYSTEM ACTIVITY", text_color="#FFC300", font=("Consolas", 14, "bold")).grid(row=4, column=0, sticky="w", padx=20, pady=(5, 5))
        self.box_sys = ctk.CTkTextbox(self.left_frame, fg_color="#0A0A0A", text_color="#DDDDDD", font=("Consolas", 12), state="disabled")
        self.box_sys.grid(row=5, column=0, sticky="nsew", padx=20, pady=(0, 20))

        # --- 右側面板 ---
        self.right_frame = ctk.CTkFrame(self, fg_color="#121212")
        self.right_frame.grid(row=1, column=1, sticky="nsew", padx=(5, 10), pady=(5, 10))
        self.right_frame.grid_columnconfigure(0, weight=1)
        self.right_frame.grid_rowconfigure(0, weight=65)
        self.right_frame.grid_rowconfigure(2, weight=35)

        plt.style.use('dark_background')
        self.fig, self.ax = plt.subplots(figsize=(5, 4)) 
        self.fig.patch.set_facecolor('#121212')
        self.ax.set_facecolor('#121212')
        
        self.line_raw, = self.ax.plot([], [], label='RAW RPM', color='#555555', linewidth=2, linestyle='--')
        self.line_filtered, = self.ax.plot([], [], label='FILTERED RPM', color='#00FFCC', linewidth=3)
        
        self.ax.set_title("BX-09 LIVE TELEMETRY", fontsize=16, fontweight='bold', color='#FF0033')
        self.ax.set_xlabel("Data Point Index", fontsize=10, color='gray')
        self.ax.set_ylabel("RPM", fontsize=10, color='gray')
        self.ax.grid(True, linestyle=':', color='#333333')
        self.ax.legend(loc='upper left', facecolor='#0A0A0A', edgecolor='#333333', labelcolor='white')

        self.canvas = FigureCanvasTkAgg(self.fig, master=self.right_frame)
        self.canvas.get_tk_widget().grid(row=0, column=0, sticky="nsew", padx=10, pady=10)

        ctk.CTkLabel(self.right_frame, text="> LIVE UDP DATA", text_color="#FF0033", font=("Consolas", 14, "bold")).grid(row=1, column=0, sticky="w", padx=15, pady=(5, 0))
        self.box_udp = ctk.CTkTextbox(self.right_frame, fg_color="#0A0A0A", text_color="#777777", font=("Consolas", 12), state="disabled")
        self.box_udp.grid(row=2, column=0, sticky="nsew", padx=15, pady=(5, 15))

        self.log_sys("GUI Initialized. Starting background services...")
        threading.Thread(target=udp_listener, daemon=True).start()
        
        self.wifi_state = None
        self.check_wifi_loop()
        self.process_queue_loop()

    # ==========================================
    # 邏輯控制區
    # ==========================================
    def on_closing(self):
        """攔截關閉視窗，讓背景執行緒安全停止"""
        self.is_running = False
        self.destroy()

    def force_connect(self):
        self.log_sys("Attempting manual Wi-Fi connection...")
        self.lbl_wifi.configure(text="[ WIFI: FORCING CONNECTION... ]", text_color="#FFC300")
        def run_connect():
            try:
                subprocess.run(['netsh', 'wlan', 'connect', f'name={WIFI_SSID}'], creationflags=0x08000000)
            except: pass
        threading.Thread(target=run_connect, daemon=True).start()

    def force_disconnect(self):
        self.log_sys("Disconnecting from ESP32...")
        def run_disconnect():
            try:
                subprocess.run(['netsh', 'wlan', 'disconnect'], creationflags=0x08000000)
            except: pass
        threading.Thread(target=run_disconnect, daemon=True).start()

    def log_sys(self, msg):
        ts = datetime.now().strftime('%H:%M:%S')
        self.box_sys.configure(state="normal")
        self.box_sys.insert("end", f"[{ts}] {msg}\n")
        self.box_sys.see("end")
        self.box_sys.configure(state="disabled")

    def log_udp(self, msg):
        self.box_udp.configure(state="normal")
        self.box_udp.insert("end", f"> {msg}\n")
        self.box_udp.see("end")
        self.box_udp.configure(state="disabled")

    def update_history_ui(self):
        self.box_history.configure(state="normal")
        self.box_history.delete("1.0", "end")
        for i in range(10):
            if i < len(history_rpms):
                self.box_history.insert("end", f"{i+1:2d}. {history_rpms[i]} RPM\n\n")
            else:
                self.box_history.insert("end", f"{i+1:2d}. ---\n\n")
        self.box_history.configure(state="disabled")

    def check_wifi_loop(self):
        if not self.is_running: return
        def task():
            current_state = check_wifi()
            data_queue.put({"type": "WIFI_UPDATE", "state": current_state})
        threading.Thread(target=task, daemon=True).start()
        if self.is_running:
            self.after(2000, self.check_wifi_loop)

    def process_queue_loop(self):
        if not self.is_running: return
        global is_recording, x_data, y_raw, y_filtered, current_shot_time, current_shot_peak
        
        need_redraw = False

        while not data_queue.empty():
            item = data_queue.get()
            
            if item["type"] == "WIFI_UPDATE":
                current_state = item["state"]
                if current_state != self.wifi_state:
                    if current_state:
                        self.lbl_wifi.configure(text=f"[ WIFI: CONNECTED TO {WIFI_SSID} ]", text_color="#00FF00")
                        self.log_sys("Connection established (192.168.4.1)")
                    else:
                        self.lbl_wifi.configure(text="[ WIFI: DISCONNECTED ]", text_color="#FF0033")
                        self.log_sys("WARNING: Wi-Fi signal lost!")
                    self.wifi_state = current_state
            
            elif item["type"] == "SYS":
                self.log_sys(item["msg"])
                
            elif item["type"] == "UDP":
                line = item["msg"]
                self.log_udp(line)

                if line == "===CSV_START===":
                    is_recording = True
                    current_shot_time = datetime.now().strftime('%H:%M:%S')
                    current_shot_peak = 0
                    
                    self.lbl_status.configure(text="[ STATUS: RECEIVING DATA... ]", text_color="#FF0033")
                    self.log_sys(">> INCOMING TRANSMISSION DETECTED <<")
                    
                    x_data.clear()
                    y_raw.clear()
                    y_filtered.clear()
                    
                    self.box_udp.configure(state="normal")
                    self.box_udp.delete("1.0", "end")
                    self.box_udp.configure(state="disabled")

                elif line == "===CSV_END===":
                    is_recording = False
                    self.lbl_status.configure(text="[ STATUS: READY ]", text_color="#00FF00")
                    
                    if current_shot_peak > 0:
                        self.lbl_peak.configure(text=str(current_shot_peak))
                        history_rpms.insert(0, current_shot_peak)
                        if len(history_rpms) > 10:
                            history_rpms.pop()
                        self.update_history_ui()
                        self.log_sys(f"Data parsed successfully. Peak: {current_shot_peak} RPM")
                        
                    need_redraw = True

                elif is_recording:
                    parts = line.split(',')
                    if len(parts) == 4:
                        idx = len(x_data) + 1
                        x_data.append(idx)
                        y_raw.append(int(parts[1]))
                        y_filtered.append(int(parts[2]))
                        current_shot_peak = int(parts[3])
                        
                        self.line_raw.set_data(x_data, y_raw)
                        self.line_filtered.set_data(x_data, y_filtered)
                        need_redraw = True
                        
                        with open(log_filename, mode='a', newline='', encoding='utf-8') as f:
                            writer = csv.writer(f)
                            writer.writerow([current_shot_time, parts[0], parts[1], parts[2], parts[3]])

        if need_redraw and x_data:
            self.ax.set_xlim(1, max(x_data) + 1)
            self.ax.set_ylim(0, max(max(y_raw), max(y_filtered)) * 1.1)
            self.canvas.draw_idle()

        if self.is_running:
            self.after(20, self.process_queue_loop)

if __name__ == "__main__":
    app = TelemetryApp()
    app.mainloop()