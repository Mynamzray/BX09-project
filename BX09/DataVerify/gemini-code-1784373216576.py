import pandas as pd
import numpy as np

def extract_correct_rpm(shot_dataframe):
    """
    模擬官方演算法：Size-3 滾動中位數濾波 + 全局最大值提取
    """
    df = shot_dataframe.sort_values(by="Elapsed_ms")
    raw_rpms = df["Raw_RPM"].values
    
    n = len(raw_rpms)
    if n < 3: return raw_rpms.max() if n > 0 else 0
        
    # 滾動中位數濾波
    smoothed = np.zeros(n)
    for i in range(1, n - 1):
        smoothed[i] = np.median(raw_rpms[i-1:i+2])
        
    # 邊緣補齊
    smoothed[0] = np.median([raw_rpms[0], raw_rpms[0], raw_rpms[1]])
    smoothed[-1] = np.median([raw_rpms[-2], raw_rpms[-1], raw_rpms[-1]])
    
    return int(smoothed.max())

# 使用範例
# shot_df = df[df['Shot_ID'] == 8]
# print(extract_correct_rpm(shot_df))