import pandas as pd
import re
from pathlib import Path

# ----------------- Helper --------------------------------------------------
def normalize(val):
    """把 空字元 / '-' / NaN 都轉成萬用字元 'X'，其他字串去掉前後空白。"""
    if pd.isna(val):
        return 'X'
    s = str(val).strip()
    return 'X' if s in ('', '-', 'nan', 'NaN', 'NAN') else s

# def parse_ops(op):
#     """將 Ops 欄位解析成 {'R','C'} 的集合；'X' 代表空集合。"""
#     s = normalize(op)
#     if s == 'X':
#         return frozenset()
#     # 抓出所有 R / C 字母（不區分大小寫）
#     return frozenset(re.findall(r'[WRI]', s.upper()))

def dominates(row_i, row_j):
    """若 row_i 支配 row_j，回傳 True。"""
    # Da, Dv, Ia, Iv 四欄：row_i 必須更「嚴格」或相等
    for col in ["Da", "Dv", "Ia", "Iv"]:
        v_i, v_j = row_i[col], row_j[col]
        if v_j != 'X' and v_i != v_j:
            return False
    # Ops 欄：row_i 的觀測集合須 ⊇ row_j
    # return parse_ops(row_i["Ops"]).issuperset(parse_ops(row_j["Ops"]))
    return normalize(row_i["Ops"]) == normalize(row_j["Ops"])

# ----------------- 0. 讀取 Excel ------------------------------------------
src_path = Path("../fault.xlsx")     # 改成你自己的檔案路徑
df = pd.read_excel(src_path)

df.columns = [c.strip() for c in df.columns]  # 去掉欄名多餘空白
# 確保必要欄位存在
required_cols = {"Da", "Dv", "Ia", "Iv", "Ops"}
missing = required_cols - set(df.columns)
if missing:
    raise ValueError(f"缺少欄位: {missing}")

# ----------------- 1. 欄位正規化 ------------------------------------------
for c in ["Da", "Dv", "Ia", "Iv", "Ops"]:
    df[c] = df[c].apply(normalize)

# ----------------- 2. 支配判定 --------------------------------------------
n = len(df)
dominated = [False] * n

for i in range(n):
    for j in range(n):
        if i == j:
            continue
        if dominates(df.loc[i], df.loc[j]):
            dominated[j] = True          # i 支配 j

df["Dominated"] = dominated
minimal_df = df[~df["Dominated"]].copy().reset_index(drop=True)

# ----------------- 3. 匯出結果 --------------------------------------------
out_path = Path("../fault_dominance.xlsx")
with pd.ExcelWriter(out_path, engine="openpyxl") as writer:
    df.to_excel(writer, sheet_name="AllFaults", index=False)
    minimal_df.to_excel(writer, sheet_name="MinimalSet", index=False)
