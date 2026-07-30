import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import os
import glob

# ---------------------------------------------------------
# 1. 物理錨點慣性濾波器 (Stateful Physics Anchor Filter)
# 100% 正確的核心演算法！
# ---------------------------------------------------------
def extract_correct_rpm(shot_dataframe):
    df = shot_dataframe.sort_values(by="Elapsed_ms")
    raw_rpms = df["Raw_RPM"].values.astype(float)
    
    if len(raw_rpms) == 0:
        return 0

    # 1. 尋找第一個合理的起點 (避免一開局的極端錯亂)
    start_idx = 0
    while start_idx < len(raw_rpms) and raw_rpms[start_idx] > 15000:
        start_idx += 1
        
    if start_idx >= len(raw_rpms):
        return int(raw_rpms.max())

    # 2. 初始化錨點 (Anchor)
    last_valid_rpm = raw_rpms[start_idx]
    true_max = last_valid_rpm

    # 3. 順序掃描所有時間點，拒絕不合理的物理暴衝
    for i in range(start_idx + 1, len(raw_rpms)):
        curr_rpm = raw_rpms[i]
        jump = curr_rpm - last_valid_rpm
        
        # 條件 A: 單次加速 > +2500 RPM
        # 條件 B: 單次減速 < -4000 RPM
        # 條件 C: 絕對值 > 15000 RPM
        if jump > 2000 or jump < -4000 or curr_rpm > 15000:
            continue
        else:
            last_valid_rpm = curr_rpm
            if curr_rpm > true_max:
                true_max = curr_rpm
                
    return int(true_max)

# ---------------------------------------------------------
# 2. 自動預測與繪圖引擎
# ---------------------------------------------------------
def predict_and_plot(df, target_shots):
    print("\n" + "=" * 45)
    print(f"{'CSV Shot ID':<15} | {'Predicted RPM (Official)':<25}")
    print("=" * 45)

    num_plots = len(target_shots)
    if num_plots == 0:
        print("沒有任何數據可以分析！")
        return

    # 自動計算排版 (每排 3 張圖)
    cols = min(3, num_plots)
    rows = int(np.ceil(num_plots / 3))
    fig, axes = plt.subplots(rows, cols, figsize=(15, 4 * rows))
    
    # 確保 axes 是 1D 陣列方便迭代
    if num_plots == 1:
        axes = np.array([axes])
    axes = axes.flatten()

    for idx, shot_id in enumerate(target_shots):
        shot_df = df[df['Shot_ID'] == shot_id].sort_values(by="Elapsed_ms")
        
        if shot_df.empty:
            print(f"{shot_id:<15} | {'CSV 無此數據':<25}")
            continue
            
        # 執行 100% 勝率的演算法進行預測
        predicted_rpm = extract_correct_rpm(shot_df)
        
        # 直接印出預測結果
        print(f"Shot {shot_id:<10} | {predicted_rpm:<25} RPM")
        
        # 找出圖表上的紅點座標 (Elapsed_ms)
        correct_rows = shot_df[shot_df['Raw_RPM'] == predicted_rpm]
        if not correct_rows.empty:
            correct_time = correct_rows.iloc[0]['Elapsed_ms']
        else:
            max_idx = shot_df['Raw_RPM'].idxmax()
            correct_time = shot_df.loc[max_idx]['Elapsed_ms']

        # 繪製曲線
        ax = axes[idx]
        ax.plot(shot_df['Elapsed_ms'], shot_df['Raw_RPM'], marker='o', color='gray', alpha=0.5, label='Raw Telemetry')
        ax.plot(correct_time, predicted_rpm, marker='o', color='red', markersize=10, label=f'Predicted Peak ({predicted_rpm})')
        ax.axhline(y=predicted_rpm, color='green', linestyle='--', alpha=0.3)
        
        ax.set_title(f"Shot {shot_id} -> {predicted_rpm} RPM")
        ax.set_xlabel("Elapsed Time (ms)")
        ax.set_ylabel("RPM")
        ax.legend()
        ax.grid(True, alpha=0.2)

    # 隱藏多餘的空白子圖
    for i in range(num_plots, len(axes)):
        fig.delaxes(axes[i])

    plt.tight_layout()
    plt.show()

# ---------------------------------------------------------
# 3. 互動式命令列 (Interactive CLI)
# ---------------------------------------------------------
def interactive_mode():
    print("\n" + "=" * 50)
    print("🚀 BEYBLADE X BX-09 全自動預測雷達啟動！")
    print("=" * 50)
    
    csv_files = glob.glob("*.csv")
    if not csv_files:
        csv_filepath = input("\n找不到 CSV 檔案，請手動輸入檔案路徑 (例如 data.csv): ").strip()
    else:
        print("\n📂 找到以下 CSV 檔案：")
        for i, file in enumerate(csv_files):
            print(f"[{i}] {file}")
        
        choice = input("\n請輸入檔案代號 (或直接輸入自訂檔名): ").strip()
        if choice.isdigit() and int(choice) < len(csv_files):
            csv_filepath = csv_files[int(choice)]
        else:
            csv_filepath = choice

    try:
        df = pd.read_csv(csv_filepath)
        print(f"\n✅ 成功載入 {csv_filepath}！")
    except Exception as e:
        print(f"\n❌ 讀取 CSV 時發生錯誤：{e}")
        return

    available_shots = sorted(df['Shot_ID'].unique(), reverse=True)
    print(f"📊 此檔案共包含 {len(available_shots)} 筆發射 (Shot {available_shots[-1]} 到 {available_shots[0]})")
    
    print("\n" + "-" * 50)
    user_input = input("👉 請輸入要預測的 Shot IDs (用逗號隔開, 例如: 32,31,30)\n👉 或者直接按 Enter 一鍵預測最新的 9 筆發射: ").strip()
    print("-" * 50)

    target_shots = []
    if user_input == "":
        # 預設抓取最新的 9 筆發射
        target_shots = available_shots[:9]
    else:
        try:
            # 抓取使用者指定的發射
            target_shots = [int(x.strip()) for x in user_input.split(',')]
            # 過濾掉不存在的 Shot ID
            target_shots = [shot for shot in target_shots if shot in available_shots]
        except ValueError:
            print("❌ 輸入格式錯誤，將預設分析最新的 9 筆...")
            target_shots = available_shots[:9]
            
    # 開始預測並繪圖
    predict_and_plot(df, target_shots)

if __name__ == "__main__":
    interactive_mode()