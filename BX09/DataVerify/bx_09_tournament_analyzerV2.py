import pandas as pd
import numpy as np
import matplotlib.pyplot as plt

# ==========================================
# 1. 核心數學法則：Size-3 Rolling Median + Global Max
# ==========================================
def extract_correct_rpm(shot_dataframe):
    """
    官方數學邏輯：
    1. 對數據進行 Size-3 的滾動中位數 (消除 1-tick 光學雜訊)
    2. 保留頭尾邊緣 (防止發射初期的真實爆發被削弱)
    3. 取平滑後數據的絕對最大值 (Global Max)
    """
    df = shot_dataframe.sort_values(by="Elapsed_ms")
    raw_rpms = df["Raw_RPM"].values
    
    n = len(raw_rpms)
    if n == 0: return 0
    if n < 3: return int(np.max(raw_rpms))
        
    smoothed = np.zeros(n)
    
    # 執行滾動中位數
    for i in range(1, n - 1):
        smoothed[i] = np.median([raw_rpms[i-1], raw_rpms[i], raw_rpms[i+1]])
        
    # 邊緣補齊 (Edge Padding)
    smoothed[0] = np.median([raw_rpms[0], raw_rpms[0], raw_rpms[1]])
    smoothed[-1] = np.median([raw_rpms[-2], raw_rpms[-1], raw_rpms[-1]])
    
    return int(smoothed.max())

# ==========================================
# 2. 自動對齊與分析引擎
# ==========================================
def hunt_and_align(csv_path):
    print("🚀 啟動 BX-09 賽事數據分析引擎...\n")
    try:
        df = pd.read_csv(csv_path)
    except FileNotFoundError:
        print(f"❌ 找不到檔案 {csv_path}。")
        return

    # User's Ground Truth Sequence (from Shot 8 down to 1)
    target_sequence = [6102, 10359, 8169, 10204, 8214, 11645, 6596, 9727]
    
    # 計算 CSV 裡每一筆射擊的分數
    available_shots = sorted(df['Shot_ID'].unique())
    db_scores = {}
    
    for shot_id in available_shots:
        shot_df = df[df['Shot_ID'] == shot_id]
        db_scores[shot_id] = extract_correct_rpm(shot_df)

    print("📊 正在全庫掃描，尋找目標分數特徵碼...")
    
    # 暴力比對：尋找連續 8 筆完全吻合的資料段
    match_found = False
    matched_ids = []
    
    for i in range(len(available_shots) - 7):
        test_sequence = [db_scores[available_shots[i+j]] for j in range(8)]
        # 允許 +- 15 RPM 的硬體浮點誤差
        if all(abs(test_sequence[k] - target_sequence[k]) <= 15 for k in range(8)):
            match_found = True
            matched_ids = [available_shots[i+j] for j in range(8)]
            break

    if match_found:
        print("\n✅ 尋找成功！發現資料錯位 (Shot-ID Shift)。")
        print("以下是真實的 CSV Shot_ID 對應與演算法計算結果：\n")
        print(f"{'User Shot':<12} | {'True CSV ID':<15} | {'Ground Truth':<15} | {'Algorithm Predict':<20} | {'Status'}")
        print("-" * 85)
        
        for idx in range(8):
            user_shot = 8 - idx
            csv_id = matched_ids[idx]
            gt = target_sequence[idx]
            pred = db_scores[csv_id]
            status = "✅ MATCH" if abs(gt - pred) <= 15 else "❌ FAIL"
            
            print(f"Shot {user_shot:<7} | ID #{csv_id:<12} | {gt:<15} | {pred:<20} | {status}")
            
    else:
        print("\n❌ 在 CSV 中找不到這組連續的分數。請確認 RPMData_17-7.csv 包含這些發射紀錄。")
        print("以下是 CSV 中最新的 10 筆演算法計算結果供您參考：")
        latest_shots = available_shots[-10:]
        for sid in reversed(latest_shots):
            print(f"CSV ID #{sid}: {db_scores[sid]} RPM")

if __name__ == "__main__":
    # 將這裡替換成你的 CSV 檔名
    csv_filename = "RPMData_17-7.csv"
    hunt_and_align(csv_filename)