import pandas as pd
import numpy as np

# 這是你提供的絕對真理 (Ground Truth)
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

def isolate_truth(csv_path):
    print("🔬 啟動 BX-09 官方數據顯微鏡...\n")
    try:
        df = pd.read_csv(csv_path)
    except FileNotFoundError:
        print(f"❌ 找不到檔案 {csv_path}！請確認檔名。")
        return

    for shot_id, official_rpm in GT.items():
        shot_df = df[df['Shot_ID'] == shot_id].sort_values(by="Elapsed_ms")
        if shot_df.empty:
            continue
            
        raw_rpms = shot_df['Raw_RPM'].values
        
        # 尋找官方數字在陣列中的確切位置
        # 允許 +- 15 的浮點數誤差
        target_idx = -1
        for i, rpm in enumerate(raw_rpms):
            if abs(rpm - official_rpm) <= 15:
                target_idx = i
                break
                
        print(f"==================================================")
        print(f"🎯 Shot {shot_id} | 官方答案: {official_rpm} RPM")
        
        if target_idx != -1:
            # 抓取前後 3 個點來觀察特徵
            start = max(0, target_idx - 3)
            end = min(len(raw_rpms), target_idx + 4)
            context = raw_rpms[start:end]
            
            # 將官方數字加上 [括號] 標記
            context_str = []
            for i in range(start, end):
                if i == target_idx:
                    context_str.append(f"[{int(raw_rpms[i])}]")
                else:
                    context_str.append(f"{int(raw_rpms[i])}")
                    
            print(f"   波形上下文: {' -> '.join(context_str)}")
            
            # 尋找這段發射的絕對最高點
            max_rpm = int(raw_rpms.max())
            if max_rpm != official_rpm:
                print(f"   ⚠️ 演算法刻意忽略了最高點: {max_rpm} RPM")
        else:
            print(f"   ❌ 在 CSV 的 Shot {shot_id} 中找不到接近 {official_rpm} 的點！")

if __name__ == "__main__":
    # 請將這裡替換成你目前使用的 ESP32 CSV 檔案
    target_csv = "RPMData_17-7_2.csv"
    isolate_truth(target_csv)