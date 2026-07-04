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

# --- 設定與全域變數 ---
WIFI_SSID = "BX09_Telemetry"
UDP_IP = "0.0.0.0"
UDP_PORT = 12345
data_queue = queue.Queue()
session_data = [["Shot_Time", "Elapsed_Time(ms)", "Raw_RPM", "Filtered_RPM", "Final_Peak_RPM"]]

def udp_listener():
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        sock.bind((UDP_IP, UDP_PORT))
    except: return
    while True:
        try:
            data, addr = sock.recvfrom(1024)
            data_queue.put({"type": "UDP", "msg": data.decode('utf-8', errors='ignore').strip()})
        except: pass

def check_wifi():
    try:
        res = subprocess.run(['ping', '-n', '1', '-w', '500', '192.168.4.1'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, creationflags=0x08000000)
        return res.returncode == 0
    except: return False

ctk.set_appearance_mode("dark")
ctk.set_default_color_theme("blue")

class TelemetryApp(ctk.CTk):
    def __init__(self):
        super().__init__()
        self.title("BX-09 TELEMETRY DASHBOARD V3.3 (Playback Edition)")
        self.geometry("1400x800")
        self.configure(fg_color="#0A0A0A")
        
        # 狀態變數初始化
        self.is_running = True
        self.shot_database = []  # 儲存每一發完整數據的資料庫
        self.x_data, self.y_raw, self.y_filtered = [], [], []
        self.current_udp_buffer = [] # 暫存當前發射的 UDP 字串
        self.is_recording = False
        self.current_shot_time = ""
        self.wifi_state = None
        
        self.protocol("WM_DELETE_WINDOW", self.on_closing)

        # ==========================================
        # UI 佈局 (50/50)
        # ==========================================
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

        self.btn_save = ctk.CTkButton(self.header_frame, text="SAVE TO CSV", fg_color="#27AE60", hover_color="#2ECC71", font=("Consolas", 12, "bold"), command=self.save_csv)
        self.btn_save.pack(side="right", padx=(10, 20), pady=10)

        self.btn_disconnect = ctk.CTkButton(self.header_frame, text="DISCONNECT", fg_color="#FF0033", hover_color="#C70039", font=("Consolas", 12, "bold"), command=self.force_disconnect)
        self.btn_disconnect.pack(side="right", padx=(10, 0), pady=10)

        self.btn_connect = ctk.CTkButton(self.header_frame, text="CONNECT WIFI", fg_color="#1F618D", hover_color="#2980B9", font=("Consolas", 12, "bold"), command=self.force_connect)
        self.btn_connect.pack(side="right", padx=0, pady=10)

        # --- 左側面板 ---
        self.left_frame = ctk.CTkFrame(self, fg_color="#121212")
        self.left_frame.grid(row=1, column=0, sticky="nsew", padx=(10, 5), pady=(5, 10))
        self.left_frame.grid_rowconfigure(3, weight=1)
        self.left_frame.grid_rowconfigure(5, weight=1)
        self.left_frame.grid_columnconfigure(0, weight=1)

        ctk.CTkLabel(self.left_frame, text="FILTERED PEAK RPM", text_color="gray", font=("Consolas", 14, "bold")).grid(row=0, column=0, sticky="w", padx=20, pady=(20, 0))
        
        self.lbl_peak = ctk.CTkLabel(self.left_frame, text="0", text_color="#00FFCC", font=("Consolas", 72, "bold"))
        self.lbl_peak.grid(row=1, column=0, sticky="w", padx=20, pady=(0, 0))
        
        self.lbl_peak_raw = ctk.CTkLabel(self.left_frame, text="RAW PEAK: 0", text_color="#FF0033", font=("Consolas", 16, "bold"))
        self.lbl_peak_raw.grid(row=2, column=0, sticky="w", padx=25, pady=(0, 15))

        ctk.CTkLabel(self.left_frame, text="RECENT HISTORY (CLICK TO REVIEW)", text_color="gray", font=("Consolas", 14, "bold")).grid(row=3, column=0, sticky="w", padx=20, pady=(5, 5))
        
        self.scroll_history = ctk.CTkScrollableFrame(self.left_frame, fg_color="#0A0A0A")
        self.scroll_history.grid(row=4, column=0, sticky="nsew", padx=20, pady=(0, 15))

        ctk.CTkLabel(self.left_frame, text="> SYSTEM ACTIVITY", text_color="#FFC300", font=("Consolas", 14, "bold")).grid(row=5, column=0, sticky="w", padx=20, pady=(5, 5))
        self.box_sys = ctk.CTkTextbox(self.left_frame, fg_color="#0A0A0A", text_color="#DDDDDD", font=("Consolas", 12), state="disabled")
        self.box_sys.grid(row=6, column=0, sticky="nsew", padx=20, pady=(0, 20))

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
        self.marker_raw, = self.ax.plot([], [], marker='o', color='#FF0033', markersize=8, linestyle='None', label='RAW PEAK')
        
        self.ax.set_title("BX-09 LIVE TELEMETRY", fontsize=16, fontweight='bold', color='#FF0033')
        self.ax.set_xlabel("Data Point Index", fontsize=10, color='gray')
        self.ax.set_ylabel("RPM", fontsize=10, color='gray')
        self.ax.grid(True, linestyle=':', color='#333333')
        self.ax.legend(loc='upper right', facecolor='#0A0A0A', edgecolor='#333333', labelcolor='white')

        self.canvas = FigureCanvasTkAgg(self.fig, master=self.right_frame)
        self.canvas.get_tk_widget().grid(row=0, column=0, sticky="nsew", padx=10, pady=10)

        ctk.CTkLabel(self.right_frame, text="> LIVE UDP DATA", text_color="#FF0033", font=("Consolas", 14, "bold")).grid(row=1, column=0, sticky="w", padx=15, pady=(5, 0))
        self.box_udp = ctk.CTkTextbox(self.right_frame, fg_color="#0A0A0A", text_color="#777777", font=("Consolas", 12), state="disabled")
        self.box_udp.grid(row=2, column=0, sticky="nsew", padx=15, pady=(5, 15))

        self.log_sys("GUI Initialized. Starting background services...")
        threading.Thread(target=udp_listener, daemon=True).start()
        
        self.check_wifi_loop()
        self.process_queue_loop()

    # ==========================================
    # 邏輯控制區
    # ==========================================
    def on_closing(self):
        self.is_running = False
        self.destroy()

    def force_connect(self):
        self.log_sys("Attempting manual Wi-Fi connection...")
        self.lbl_wifi.configure(text="[ WIFI: FORCING CONNECTION... ]", text_color="#FFC300")
        def run_connect():
            try: subprocess.run(['netsh', 'wlan', 'connect', f'name={WIFI_SSID}'], creationflags=0x08000000)
            except: pass
        threading.Thread(target=run_connect, daemon=True).start()

    def force_disconnect(self):
        self.log_sys("Disconnecting from ESP32...")
        def run_disconnect():
            try: subprocess.run(['netsh', 'wlan', 'disconnect'], creationflags=0x08000000)
            except: pass
        threading.Thread(target=run_disconnect, daemon=True).start()

    def save_csv(self):
        global session_data
        if len(session_data) <= 1:
            self.log_sys("CSV Save Error: No data to save yet!")
            return
        filename = f"BX09_Data_{datetime.now().strftime('%Y%m%d_%H%M%S')}.csv"
        try:
            with open(filename, mode='w', newline='', encoding='utf-8') as f:
                writer = csv.writer(f)
                writer.writerows(session_data)
            self.log_sys(f"SUCCESS: Data saved to {filename}")
        except Exception as e:
            self.log_sys(f"CSV Save Error: {e}")

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
        for widget in self.scroll_history.winfo_children():
            widget.destroy()

        for shot in self.shot_database:
            btn_text = f"Shot #{shot['id']:02d} | F: {shot['peak']} RPM (R: {shot['raw_peak']})"
            btn = ctk.CTkButton(self.scroll_history, text=btn_text, 
                                fg_color="#1A1A1A", hover_color="#333333", text_color="white",
                                anchor="w", font=("Consolas", 14),
                                command=lambda s=shot: self.load_historical_shot(s))
            btn.pack(fill="x", pady=2, padx=2)

    def load_historical_shot(self, shot):
        self.lbl_status.configure(text=f"[ STATUS: REVIEWING SHOT #{shot['id']:02d} ]", text_color="#00FFCC")
        
        self.lbl_peak.configure(text=str(shot['peak']))
        self.lbl_peak_raw.configure(text=f"RAW PEAK: {shot['raw_peak']}")

        self.line_raw.set_data(shot['x_data'], shot['y_raw'])
        self.line_filtered.set_data(shot['x_data'], shot['y_filtered'])
        
        if shot['x_data'] and shot['y_raw']:
            max_idx = shot['x_data'][shot['y_raw'].index(shot['raw_peak'])]
            self.marker_raw.set_data([max_idx], [shot['raw_peak']])
        
        self.ax.set_xlim(1, max(shot['x_data']) + 1 if shot['x_data'] else 2)
        self.ax.set_ylim(0, max(max(shot['y_raw'] or [0]), max(shot['y_filtered'] or [0])) * 1.1)
        self.canvas.draw_idle()

        self.box_udp.configure(state="normal")
        self.box_udp.delete("1.0", "end")
        self.box_udp.insert("end", "\n".join(f"> {line}" for line in shot['udp_data']) + "\n> ===CSV_END===")
        self.box_udp.see("end")
        self.box_udp.configure(state="disabled")

    def check_wifi_loop(self):
        if not self.is_running: return
        def task():
            data_queue.put({"type": "WIFI_UPDATE", "state": check_wifi()})
        threading.Thread(target=task, daemon=True).start()
        if self.is_running:
            self.after(2000, self.check_wifi_loop)

    def process_queue_loop(self):
        if not self.is_running: return
        global session_data
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
                
                if "[ STATUS: REVIEWING" not in self.lbl_status.cget("text"):
                    self.log_udp(line)

                if line == "===CSV_START===":
                    self.is_recording = True
                    self.current_shot_time = datetime.now().strftime('%H:%M:%S')
                    self.lbl_status.configure(text="[ STATUS: RECEIVING DATA... ]", text_color="#FF0033")
                    self.log_sys(">> INCOMING TRANSMISSION DETECTED <<")
                    
                    self.x_data.clear()
                    self.y_raw.clear()
                    self.y_filtered.clear()
                    self.current_udp_buffer.clear() 
                    self.marker_raw.set_data([], [])
                    
                    self.box_udp.configure(state="normal")
                    self.box_udp.delete("1.0", "end")
                    self.box_udp.configure(state="disabled")

                elif line == "===CSV_END===":
                    self.is_recording = False
                    self.lbl_status.configure(text="[ STATUS: READY ]", text_color="#00FF00")
                    
                    max_raw = 0
                    if self.y_raw:
                        max_raw = max(self.y_raw)
                        max_idx = self.x_data[self.y_raw.index(max_raw)]
                        self.marker_raw.set_data([max_idx], [max_raw])
                    
                    if self.y_filtered:
                        current_shot_peak = max(self.y_filtered)
                        self.lbl_peak.configure(text=str(current_shot_peak))
                        self.lbl_peak_raw.configure(text=f"RAW PEAK: {max_raw}")
                        
                        # 把整局的資料包裝起來存進資料庫
                        shot_data = {
                            'id': len(self.shot_database) + 1,
                            'peak': current_shot_peak,
                            'raw_peak': max_raw,
                            'x_data': list(self.x_data),
                            'y_raw': list(self.y_raw),
                            'y_filtered': list(self.y_filtered),
                            'udp_data': list(self.current_udp_buffer)
                        }
                        self.shot_database.insert(0, shot_data)
                        self.update_history_ui()
                        self.log_sys(f"Parsed. Filtered: {current_shot_peak} | Raw: {max_raw}")
                        
                    need_redraw = True

                elif self.is_recording:
                    parts = line.split(',')
                    if len(parts) == 4:
                        self.x_data.append(len(self.x_data) + 1)
                        self.y_raw.append(int(parts[1]))
                        self.y_filtered.append(int(parts[2]))
                        
                        self.line_raw.set_data(self.x_data, self.y_raw)
                        self.line_filtered.set_data(self.x_data, self.y_filtered)
                        
                        self.current_udp_buffer.append(line)
                        session_data.append([self.current_shot_time, parts[0], parts[1], parts[2], parts[3]])
                        need_redraw = True

        if need_redraw and self.x_data:
            self.ax.set_xlim(1, max(self.x_data) + 1)
            self.ax.set_ylim(0, max(max(self.y_raw), max(self.y_filtered)) * 1.1)
            self.canvas.draw_idle()

        if self.is_running:
            self.after(20, self.process_queue_loop)

if __name__ == "__main__":
    app = TelemetryApp()
    app.mainloop()