"""
fault_minimize.py
-----------------
讀取 fault.xlsx (需含欄 Da Dv Ia Iv …)，
計算 (Da,Dv,Ia,Iv) 的最小集合並在每列加入 f_* 欄位，
匯出 fault_minimized.xlsx

安裝依賴:
    pip install pandas openpyxl
"""

import pandas as pd
from pathlib import Path

# ---------- 0. 參數 ----------
SRC = Path("fault.xlsx")             # ← Excel 檔名
DST = Path("fault_minimized.xlsx")   # ← 輸出檔名
VAR_COLS = ["Da", "Dv", "Ia", "Iv"]  # 要最小化的四欄
WILDCARD  = "X"                      # 內部先用 'X' 表 don’t-care
INT_MAP   = {"0": 0, "1": 1, "X": -1}

# ---------- 1. 讀檔 & 正規化 ----------
df = pd.read_excel(SRC)
df.columns = [c.strip() for c in df.columns]      # 去空白

def norm(v):
    if pd.isna(v):
        return WILDCARD
    s = str(v).strip().upper()
    s = s.split('.')[0]  # 移除小數部分
    return WILDCARD if s in {"", "-", "X"} else s

for c in VAR_COLS:
    df[c] = df[c].apply(norm)

# ---------- 2. 求最小集合 ----------
state_set = set(tuple(row) for row in df[VAR_COLS].values)

def compatible(a, b):
    """判斷兩 tuple 是否可合併"""
    return all(x == y or x == WILDCARD or y == WILDCARD for x, y in zip(a, b))

def merge(a, b):
    """合併兩 compatible tuple"""
    return tuple(
        x if x == y else (y if x == WILDCARD else x)
        for x, y in zip(a, b)
    )

changed = True
while changed:
    changed = False
    new_states = set(state_set)
    states = list(state_set)
    for i in range(len(states)):
        for j in range(i + 1, len(states)):
            s1, s2 = states[i], states[j]
            if compatible(s1, s2):
                merged = merge(s1, s2)
                if merged not in state_set:
                    new_states.add(merged)
                    changed = True
    state_set = new_states

# ---------- 3. 去除被覆蓋 (subsumed) ----------
def covers(a, b):
    """a covers b if for every pos a==b or a==X"""
    return all(x == y or x == WILDCARD for x, y in zip(a, b))

# ---------- 4. 幫每列配對「最終所屬狀態」 ----------
def find_state(row):
    for st in state_set:
        if covers(st, row):
            return st
    raise RuntimeError("找不到覆蓋狀態？")

f_cols = {f"f_{c}": [] for c in VAR_COLS}
for _, r in df.iterrows():
    st = find_state(tuple(r[c] for c in VAR_COLS))
    for idx, c in enumerate(VAR_COLS):
        f_cols[f"f_{c}"].append(INT_MAP[st[idx]])

for c in f_cols:
    df[c] = f_cols[c]

# ---------- 5. 匯出 ----------
# 原 DF 可能含字串，為避免 0→0.0，自行指定 int64 dtype
for c in VAR_COLS + list(f_cols.keys()):
    df[c] = df[c].map(INT_MAP).astype("Int64")

df.to_excel(DST, index=False, engine="openpyxl")
print(f"Done! 匯出 {DST}，最小集合大小 = {len(state_set)}")
