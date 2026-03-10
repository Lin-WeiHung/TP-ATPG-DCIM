# 🚀 CIM-ATPG Docker Image

> **CIM Memory ATPG Tool - Dual Mode Interface**  
> 整合 Generate（模板搜尋產生）與 Simulator（錯誤模擬分析）兩種模式

[![Rocky Linux 8](https://img.shields.io/badge/Rocky%20Linux-8-green?logo=rockylinux)](https://rockylinux.org/)
[![C++20](https://img.shields.io/badge/C++-20-blue?logo=cplusplus)](https://isocpp.org/)
[![GCC 13](https://img.shields.io/badge/GCC-13-orange?logo=gnu)](https://gcc.gnu.org/)

---

## 📖 目錄

- [工具簡介](#-工具簡介)
- [快速開始](#-快速開始)
- [建置映像檔 (Build)](#-建置映像檔-build)
- [模式說明](#-模式說明)
  - [Generate 模式](#generate-模式---產生最佳-march-test)
  - [Simulator 模式](#simulator-模式---模擬分析-march-test)
- [完整範例](#-完整範例)
- [輸入/輸出格式](#-輸入輸出格式)
- [指定本機輸出路徑](#-指定本機輸出路徑)
- [故障排除](#-故障排除)

---

## 🔍 工具簡介

**CIM-ATPG** 是專為 **Compute-in-Memory (CIM)** 記憶體設計的自動化測試向量產生工具，提供兩種操作模式：

| 模式 | 功能 | 輸入 | 輸出 |
|-----|------|------|------|
| **Generate** | 貪婪搜尋 + 自動精煉，產生最佳 March Test | 錯誤模型 JSON | JSON + 互動式 HTML 報表 |
| **Simulator** | 模擬現有 March Test 的覆蓋率 | 錯誤模型 + March Test JSON | 互動式 HTML 分析報表 |

### 適用情境

- 🧪 **Generate 模式**：自動搜尋最佳測試向量，並在未達 100% 時自動精煉 (refine)
- 📊 **Simulator 模式**：比較不同 March Test 的覆蓋率、論文實驗分析

---

## ⚡ 快速開始

```bash
# 1. 建置映像檔
docker build -f docker/Dockerfile -t cim-atpg:latest .

# 2. 查看說明
docker run --rm --network=host cim-atpg:latest --help

# 3. Generate 模式 - 產生 March Test
docker run --rm -it --network=host \
  -v `pwd`/input:/data -w /data \
  cim-atpg:latest \
  --mode generate S_C_faults.json output.json output.html

# 4. Simulator 模式 - 分析現有 March Test
docker run --rm -it --network=host \
  -v `pwd`/input:/data -w /data \
  cim-atpg:latest \
  --mode simulator S_C_faults.json Compare.json report.html
```

---

## 🔨 建置映像檔 (Build)

### 目錄結構要求

```
your_project/
├── docker/
│   └── Dockerfile
├── include/
│   ├── FaultSimulator.hpp
│   ├── FpParserAndTpGen.hpp
│   └── TemplateSearchers.hpp
└── src/
    ├── main.cpp
    ├── GreedySweepRunner.cpp
    └── FaultSimulationEvent.cpp
```

### Build 指令

```bash
docker build -f docker/Dockerfile -t cim-atpg:latest .
```

---

## 🎮 模式說明

### Generate 模式 - 產生最佳 March Test

使用貪婪搜尋演算法，掃描 (slots, L) 組合空間，自動產生高覆蓋率的 March Test 序列。若掃描後未達 100% 覆蓋率，會自動以更大的模板庫 (slots+1) 進行精煉 (refine)，逐一替換元素以提升覆蓋率。最終輸出與 Simulator 模式相同的互動式 HTML 報表。

#### 語法

```bash
docker run --rm -it --network=host \
  -v <本機路徑>:/data -w /data \
  cim-atpg:latest \
  --mode generate <faults.json> <output.json> <output.html> [options]
```

#### 參數

| 參數 | 預設值 | 說明 |
|-----|--------|------|
| `faults.json` | `input/S_C_faults.json` | 輸入錯誤模型 |
| `output.json` | `output/GreedySweep_Bests.json` | 輸出 JSON |
| `output.html` | `output/GreedySweep_Bests.html` | 輸出 HTML |
| `--start-slots` | 1 | 起始 slots 數 |
| `--start-L` | 1 | 起始 L 數 |
| `--max-slots` | 4 | 最大 slots 數（單一 element 最大 op 數） |
| `--max-L` | 6 | 最大 L 數（最大 element 數量） |
| `--w-state` | 0.9 | state_coverage 權重 |
| `--w-total` | 0.5 | total_coverage 權重 |
| `--op-penalty` | 0.01 | op 數量懲罰係數 |

#### 範例

```bash
# 快速測試
docker run --rm -it --network=host \
  -v `pwd`/input:/data -w /data \
  cim-atpg:latest \
  --mode generate S_C_faults.json out.json out.html --max-slots 2 --max-L 2

# 完整搜尋
docker run --rm -it --network=host \
  -v `pwd`/input:/data -w /data \
  cim-atpg:latest \
  --mode generate S_C_faults.json result.json result.html --max-slots 4 --max-L 6

# 自訂評分權重
docker run --rm -it --network=host \
  -v `pwd`/input:/data -w /data \
  cim-atpg:latest \
  --mode generate S_C_faults.json result.json result.html --w-state 0.8 --w-total 0.6 --op-penalty 0.02
```

#### 輸出範例

```
[Sweep] max_slots=3, max_L=6, valid configs=18
[Sweep] (1/18) slots=1, L=1 (ops=1) -> cov=0.0000% [0 ms]
...
[Sweep] (18/18) slots=3, L=6 (ops=18) -> cov=95.5882% [534 ms]

[Refine] === Fine-tuning best result via local search ===
[Refine] Greedy best slots=3, refine slots=4
[Refine] Before: 95.5882%
[Refine] After:  100.0000%
[Refine] Improvement: 4.4118 pp
[Refine] Elapsed: 17588 ms

[Sweep] === Summary ===
[Sweep] Configs tested: 18/18
[Sweep] Best coverage: 100.0000%
[Sweep] Best config: current pattern 2026-03-10
[Sweep] Reached 100%: No
[Sweep] Total elapsed: 19154 ms
[Sweep] HTML written: output/GreedySweep_Bests.html
```

> **Note**: 若貪婪搜尋已達 100% 覆蓋率，精煉步驟會自動跳過。

---

### Simulator 模式 - 模擬分析 March Test

模擬給定的 March Test 序列，計算對錯誤模型的覆蓋率，產生詳細的互動式分析報表。

#### 語法

```bash
docker run --rm -it --network=host \
  -v <本機路徑>:/data -w /data \
  cim-atpg:latest \
  --mode simulator <faults.json> <MarchTests.json> <output.html>
```

#### 參數

| 參數 | 必要 | 說明 |
|-----|------|------|
| `faults.json` | ✅ | 錯誤模型定義檔 |
| `MarchTests.json` | ✅ | March Test 序列檔 |
| `output.html` | ✅ | 輸出 HTML 報表 |

#### 範例

```bash
# 比較不同 March Test
docker run --rm -it --network=host \
  -v `pwd`/input:/data -w /data \
  cim-atpg:latest \
  --mode simulator S_C_faults.json Compare.json compare_report.html

# Ablation Study
docker run --rm -it --network=host \
  -v `pwd`/input:/data -w /data \
  cim-atpg:latest \
  --mode simulator S_C_faults.json Ablation.json ablation_report.html
```

#### 輸出範例

```
[Simulator] Faults→TPs: 289 us (faults=34, TPs=130)
[時間] 2) 解析 March tests 並正規化: 51 us (tests=4)
[時間] 3) 模擬+輸出 March Test 'Proposed Method': 210 us (ops=0, cov=100.00%)
[時間] 3) 模擬+輸出 March Test 'March COM': 189 us (ops=0, cov=94.12%)
[時間] 3) 模擬+輸出 March Test 'March DC': 162 us (ops=0, cov=52.94%)
[時間] 3) 模擬+輸出 March Test 'TAT brute': 254 us (ops=0, cov=89.71%)
HTML report written to: compare_report.html
```

---

## 📁 輸入/輸出格式

### 錯誤模型格式 (faults.json)

```json
[
  {
    "fault_id": "SA0",
    "category": "either_read_or_compute",
    "cell_scope": "single cell",
    "fault_primitives": ["< 1D/0D/-/- >", "< 0Ci; 1D/0D/-/- >"]
  }
]
```

#### 參數說明

| 欄位 | 可選值 | 說明 |
|-----|--------|------|
| `fault_id` | 任意字串 | 錯誤識別名稱（如 SA0, SA1, TF, CF 等） |
| `category` | `either_read_or_compute`<br>`must_read`<br>`must_compute` | 錯誤觸發條件：<br>• `either_read_or_compute`: 讀取或運算時均可觸發<br>• `must_read`: 僅在讀取操作時觸發<br>• `must_compute`: 僅在運算操作時觸發 |
| `cell_scope` | `single cell`<br>`coupling` | 錯誤影響範圍：<br>• `single cell`: 單一記憶體單元錯誤<br>• `coupling`: 多個記憶體單元間的耦合錯誤 |
| `fault_primitives` | 陣列 | 錯誤原型定義列表（見下方語法說明） |

#### Fault Primitives 語法

**基本格式**：`< [aggressor_cell;] victim_cell >`

- **Aggressor Cell**（左側，可選）：導致錯誤的干擾單元
- **Victim Cell**（右側，必要）：受影響的受害單元
- 使用 `;` 分隔 aggressor 與 victim（單一單元錯誤可省略 aggressor）

**狀態表示法**：

| 符號 | 意義 | 範例 |
|-----|------|------|
| `D` | Memory 資料值 | `0D`（記憶體儲存 0）、`1D`（記憶體儲存 1） |
| `Ci` | Computing Input 值 | `0Ci`（運算輸入 0）、`1Ci`（運算輸入 1） |
| `-` | Don't care（任意值） | `-/-/-/-` |
| `/` | 狀態分隔符 | 分隔四個時間/操作狀態 |

**範例解析**：

```json
"< 1D/0D/-/- >"           // 單一單元：記憶體值從 1 變 0
"< 0Ci; 1D/0D/-/- >"      // 耦合錯誤：當 aggressor 輸入 0Ci 時，
                          // victim 的記憶體值從 1D 變 0D
"< 0D/1D/-/-; -/-/-/1D >" // Aggressor 從 0D→1D 時，
                          // victim 被寫入錯誤值 1D
```

### March Test 格式 (MarchTests.json)

```json
[
  {
    "March_test": "Proposed Method",
    "Pattern": "a(W0); a(C(1)(1)(1), W1, C(0)(0)(0), C(0)(1)(0)); ..."
  },
  {
    "March_test": "March COM",
    "Pattern": "b(W0, C(0)(0)(0)); a(R0, W1, C(1)(1)(0)); ..."
  }
]
```

### Generate 輸出 (output.json)

最終結果（經精煉後的最佳 March Test）：

```json
[
  {
    "March_test": "current pattern 2026-03-10",
    "Pattern": "a(W0); a(W1, C(1)(1)(0)); a(C(1)(1)(1), W0, C(1)(1)(0)); ...",
    "state_coverage": 1.0,
    "total_coverage": 1.0
  }
]
```

---

## 🎯 完整範例

### 範例 1：端對端 Generate + Simulate

```bash
# Step 1: 產生最佳 March Test
docker run --rm -it --network=host \
  -v `pwd`/data:/data -w /data \
  cim-atpg:latest \
  --mode generate faults.json generated.json generated.html --max-slots 4 --max-L 6

# Step 2: 將產生的結果與其他 March Test 比較
docker run --rm -it --network=host \
  -v `pwd`/data:/data -w /data \
  cim-atpg:latest \
  --mode simulator faults.json generated.json comparison.html
```

### 範例 2：論文實驗比較

```bash
# 準備 Compare.json 包含要比較的 March Test
docker run --rm -it --network=host \
  -v `pwd`/input:/data -w /data \
  cim-atpg:latest \
  --mode simulator S_C_faults.json Compare.json paper_comparison.html
```

---

## 📦 指定本機輸出路徑

你可以透過 Volume 掛載，將容器輸出直接寫到本機資料夾。

方式 A：專用輸出目錄（建議）

```bash
# 建立本機輸出資料夾
mkdir -p host_out_generate host_out_sim

# Generate：把容器的 /out 掛載到本機，並把輸出寫到 /out
docker run --rm -it --network=host \
  -v `pwd`/input:/data \
  -v `pwd`/host_out_generate:/out \
  -w /data \
  cim-atpg:latest \
  --mode generate S_C_faults.json /out/gs.json /out/gs.html

# Simulator：同樣寫到 /out
docker run --rm -it --network=host \
  -v `pwd`/input:/data \
  -v `pwd`/host_out_sim:/out \
  -w /data \
  cim-atpg:latest \
  --mode simulator S_C_faults.json Compare.json /out/compare.html
```

方式 B：工作目錄即輸出位置（輕量）

```bash
docker run --rm -it --network=host \
  -v `pwd`/input:/work -w /work \
  cim-atpg:latest \
  --mode generate S_C_faults.json out.json out.html
```

權限小提示：若輸出檔在本機顯示 root 擁有者，可加入 ``-u `id -u`:`id -g``` 以你目前使用者身分寫檔。


## 🔧 故障排除

### Q1: 執行時出現 `filesystem error: cannot create directories`

已修復。現在支援直接使用檔名（如 `output.json`）而不需要指定目錄。

### Q2: Docker 輸出是一次性顯示而非逐行

使用 `-it` 參數啟用即時輸出：

```bash
docker run --rm -it --network=host ...
```

### Q3: 找不到模式

確認使用 `--mode generate` 或 `--mode simulator`：

```bash
docker run --rm --network=host cim-atpg:latest --mode generate --help
docker run --rm --network=host cim-atpg:latest --mode simulator --help
```

### Q4: 輸出檔案權限為 root

```bash
docker run --rm -it --network=host \
  -u `id -u`:`id -g` \
  -v `pwd`/data:/data -w /data \
  cim-atpg:latest ...
```

---

## 📄 授權資訊

Copyright © 2026 NCU EDA Lab. All rights reserved.
