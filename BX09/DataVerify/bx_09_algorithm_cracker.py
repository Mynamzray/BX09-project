import pandas as pd
import numpy as np
import matplotlib.pyplot as plt

# 這是老闆提供的絕對真理 (Ground Truth)
GT = {
    8: 6102,
    7: 10359,
    6: 8169,
    5: 10204,
    4: 8214,
    3: 11645,
    2: 6596,
    1: 9727
}

def rpm_to_ticks(rpm_array):
    """將 RPM 還原為硬體的原始時間間隔 (Ticks)"""
    # 避免除以 0
    safe_rpm = np.where(rpm_array == 0, 1, rpm_array)
    return 7500000.0 / safe_rpm

def ticks_to_rpm(ticks_array):
    """將 Ticks 轉換回 RPM"""
    safe_ticks = np.where(ticks_array == 0, 1, ticks_array)
    return 7500000.0 / safe_ticks

def extract_features(raw_rpm, algo_type, param):
    """
    訊號處理核心引擎：
    支援多種數學模型，用來暴力測試官方的隱藏邏輯
    """
    if len(raw_rpm) == 0: return 0
    
    if algo_type.startswith("Ticks_"):
        # 【假說 1】官方是對 Ticks 進行平滑處理
        signal = rpm_to_ticks(raw_rpm)
        s = pd.Series(signal)
        
        if "EMA" in algo_type:
            # 越小代表越平滑 (更吃重歷史軌跡)
            smoothed_ticks = s.ewm(alpha=param, adjust=False).mean().values
        elif "Mean" in algo_type:
            smoothed_ticks = s.rolling(window=param, min_periods=1, center=True).mean().values
        elif "Median" in algo_type:
            smoothed_ticks = s.rolling(window=param, min_periods=1, center=True).median().values
            
        # 將平滑後的時間轉換回 RPM，並取最大值
        smoothed_rpm = ticks_to_rpm(smoothed_ticks)
        return int(np.max(smoothed_rpm))
        
    else:
        # 【假說 2】官方是對 RPM 進行平滑處理
        s = pd.Series(raw_rpm)
        
        if "EMA" in algo_type:
            smoothed_rpm = s.ewm(alpha=param, adjust=False).mean().values
        elif "Mean" in algo_type:
            smoothed_rpm = s.rolling(window=param, min_periods=1, center=True).mean().values
        elif "Median" in algo_type:
            smoothed_rpm = s.rolling(window=param, min_periods=1, center=True).median().values
            
        return int(np.max(smoothed_rpm))

def run_grid_search(csv_path):
    print("🚀 啟動 BX-09 全自動演算法暴力破解引擎...\n")
    try:
        df = pd.read_csv(csv_path)
    except FileNotFoundError:
        print(f"❌ 找不到檔案 {csv_path}！")
        return

    # 提取目標 8 筆發射的原始資料
    shots_data = {}
    for sid in GT.keys():
        shot_df = df[df['Shot_ID'] == sid]
        if shot_df.empty:
            print(f"⚠️ 警告：在 CSV 中找不到 Shot {sid} 的資料！請確認 CSV 內容。")
            return
        shots_data[sid] = shot_df.sort_values(by="Elapsed_ms")['Raw_RPM'].values

    # 定義要暴力測試的演算法矩陣
    algorithms = []
    
    # 1. 測試傳統的 RPM 濾波
    for w in range(2, 10):
        algorithms.append(("RPM_Median", w))
        algorithms.append(("RPM_Mean", w))
    for a in np.arange(0.05, 1.0, 0.05):
        algorithms.append(("RPM_EMA", round(a, 2)))
        
    # 2. 測試硬體層級的 Ticks 濾波 (極大機率是這個)
    for w in range(2, 10):
        algorithms.append(("Ticks_Median", w))
        algorithms.append(("Ticks_Mean", w))
    for a in np.arange(0.05, 1.0, 0.05):
        algorithms.append(("Ticks_EMA", round(a, 2)))

    # 開始比對與計算分數
    results = []
    print("🕵️‍♂️ 正在交叉比對 100+ 種數位訊號濾波模型...")
    
    for algo_type, param in algorithms:
        total_error = 0
        predictions = {}
        
        for sid, expected_rpm in GT.items():
            pred = extract_features(shots_data[sid], algo_type, param)
            predictions[sid] = pred
            # 計算絕對誤差 (Absolute Error)
            total_error += abs(pred - expected_rpm)
            
        mean_error = total_error / len(GT)
        results.append({
            'algo': f"{algo_type} (param={param})",
            'mae': mean_error,
            'predictions': predictions
        })

    # 根據誤差從小到大排序
    results.sort(key=lambda x: x['mae'])
    
    print("\n🏆 破解完成！以下是與官方數據最吻合的前 3 個數學公式：\n")
    for i in range(3):
        res = results[i]
        print(f"🏅 第 {i+1} 名公式: {res['algo']}")
        print(f"   平均誤差: {res['mae']:.1f} RPM")
        print(f"   詳細預測結果:")
        for sid in GT.keys():
            diff = res['predictions'][sid] - GT[sid]
            sign = "+" if diff > 0 else ""
            print(f"     Shot {sid}: 官方 {GT[sid]} | 預測 {res['predictions'][sid]} ({sign}{diff})")
        print("-" * 50)
        
    print("\n💡 結論指南：")
    print("如果第一名公式的平均誤差小於 20 RPM，代表我們已經 100% 破解了 Takara Tomy 的核心代碼！")
    print("你可以直接把獲勝的公式邏輯套用回你的 C++ 或 Python 主程式中。")

if __name__ == "__main__":
    # 將這裡替換成老闆你上傳的檔案名稱
    target_csv = "RPMData_17-7_2.csv"
    run_grid_search(target_csv)