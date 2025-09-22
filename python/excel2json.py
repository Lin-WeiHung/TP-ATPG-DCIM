"""
xlsx_to_fault_json.py
---------------------
讀取 fault.xlsx，依照指定 schema 產生 fault.json
需要安裝：pandas、openpyxl
    pip install pandas openpyxl
"""

import json
import re
from pathlib import Path
import pandas as pd
import argparse

# ====== 1. 檔案路徑（由參數輸入）======

parser = argparse.ArgumentParser(description="Convert Excel to JSON with specified schema.")
parser.add_argument("src_xlsx", help="Source Excel file path (e.g. fault.xlsx)")
parser.add_argument("dst_json", help="Destination JSON file path (e.g. fault.json)")

args = parser.parse_args()

SRC_XLSX = Path(args.src_xlsx)
DST_JSON = Path(args.dst_json)

# ====== 2. 欄名對應（Excel → 欄位）======
# 若 Excel 欄名不同，請把右側字串改成你實際欄名
COL_MAP = {
    "name"   : "Name",    # Fault 名稱欄（含 e.g. "(0,00)" 後綴）
    "SFR"    : "SFR",     # <S/F/R>
    "Da"     : "Da",
    "Dv"     : "Dv",
    "Ia"     : "Ia",
    "Iv"     : "Iv",
    "Ops"    : "Ops",     # 逗號或空白分隔即可
    "Detect" : "Detect",  # 偵測操作
    "F"      : "F",
    "R"      : "R",
    "Gv"     : "Gv",
}

# ====== 3. 讀取 Excel ======
df = pd.read_excel(SRC_XLSX)
df.columns = [c.strip() for c in df.columns]   # 去空白

# ====== 4. 去除後綴並產生 subcase ======
#   Fault 名字可能像 "CIDC(1,0)" → name = "CIDC", suffix = "(1,0)"
def split_name(raw: str):
    """回傳 (name, suffix_str_without_paren)"""
    m = re.match(r"\s*([A-Za-z0-9_-]+)\s*(\(([^)]+)\))?", str(raw))
    if not m:
        return raw.strip(), ""     # fallback
    base = m.group(1)
    suffix = m.group(3) or ""      # 例如 "1,0"
    return base, suffix

# 建立 name / subcase 欄位
df[["base_name", "suffix"]] = df[COL_MAP["name"]].apply(
    lambda s: pd.Series(split_name(s))
)

# 依 base_name 編號 subcase
df["subcase"] = df.groupby("base_name").cumcount()

# ====== 5. 對各列組合成 dict ======
records = []
for _, row in df.iterrows():
    # Ops → 去空白、逗號/分號分隔、重新組成 "{W1}, {W0}" 格式，沒有則為空
    ops_raw = str(row[COL_MAP["Ops"]]) if pd.notna(row[COL_MAP["Ops"]]) else ""
    ops_tokens = [t.strip() for t in ops_raw.replace(";", ",").split(",") if t.strip()]
    ops_str = ", ".join(f"{{{op}}}" for op in ops_tokens) if ops_tokens else ""

    rec = {
        "name"   : row["base_name"],
        "subcase": int(row["subcase"]),
        "SFR"    : row[COL_MAP["SFR"]],
        "Da"     : int(row[COL_MAP["Da"]]) if pd.notna(row[COL_MAP["Da"]]) else -1,
        "Dv"     : int(row[COL_MAP["Dv"]]) if pd.notna(row[COL_MAP["Dv"]]) else -1,
        "Ia"     : int(row[COL_MAP["Ia"]]) if pd.notna(row[COL_MAP["Ia"]]) else -1,
        "Iv"     : int(row[COL_MAP["Iv"]]) if pd.notna(row[COL_MAP["Iv"]]) else -1,
        "Ops"    : ops_str,
        "Detect" : row[COL_MAP["Detect"]],
        "F"      : int(row[COL_MAP["F"]]) if pd.notna(row[COL_MAP["F"]]) else -1,
        "R"      : int(row[COL_MAP["R"]]) if pd.notna(row[COL_MAP["R"]]) else -1,
        "Gv"     : int(row[COL_MAP["Gv"]]) if pd.notna(row[COL_MAP["Gv"]]) else -1,
    }
    records.append(rec)

# ====== 6. 輸出 JSON ======
with DST_JSON.open("w", encoding="utf-8") as fp:
    json.dump(records, fp, indent=2, ensure_ascii=False)

print(f"完成！已產生 {DST_JSON}，共 {len(records)} 筆。")
