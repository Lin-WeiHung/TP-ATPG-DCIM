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
| **Generate** | 使用貪婪搜尋產生最佳 March Test | 錯誤模型 JSON | JSON + HTML 報表 |
| **Simulator** | 模擬現有 March Test 的覆蓋率 | 錯誤模型 + March Test JSON | HTML 分析報表 |

### 適用情境

- 🧪 **Generate 模式**：自動搜尋最佳測試向量、參數空間探索
- 📊 **Simulator 模式**：比較不同 March Test 的覆蓋率、論文實驗分析

---

## ⚡ 快速開始

```bash
# 1. 建置映像檔
docker build -f docker/Dockerfile -t cim-atpg:latest .

# 2. 查看說明
docker run --rm cim-atpg:latest --help

# 3. Generate 模式 - 產生 March Test
docker run --rm -it \
  -v $(pwd)/input:/data -w /data \
  cim-atpg:latest \
  --mode generate S_C_faults.json output.json output.html

# 4. Simulator 模式 - 分析現有 March Test
docker run --rm -it \
  -v $(pwd)/input:/data -w /data \
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
│   ├── TemplateSearchers.hpp
│   └── TemplateSearchReport.hpp
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

使用貪婪搜尋演算法，自動產生高覆蓋率的 March Test 序列。

#### 語法

```bash
docker run --rm -it \
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
| `--max-slots` | 4 | 最大 slots 數 |
| `--max-L` | 6 | 最大 L 數 |

#### 範例

```bash
# 快速測試
docker run --rm -it \
  -v $(pwd)/input:/data -w /data \
  cim-atpg:latest \
  --mode generate S_C_faults.json out.json out.html --max-slots 2 --max-L 2

# 完整搜尋
docker run --rm -it \
  -v $(pwd)/input:/data -w /data \
  cim-atpg:latest \
  --mode generate S_C_faults.json result.json result.html --max-slots 4 --max-L 6
```

#### 輸出範例

```
[Sweep] max_slots=2, max_L=2, valid configs=4
[Sweep] (1/4) slots=1, L=1 (ops=1) -> cov=0.0000% [0 ms]
[Sweep] (2/4) slots=1, L=2 (ops=2) -> cov=11.7647% [0 ms]
[Sweep] (3/4) slots=2, L=1 (ops=2) -> cov=0.0000% [0 ms]
[Sweep] (4/4) slots=2, L=2 (ops=4) -> cov=27.9412% [3 ms]
[Sweep] JSON written: out.json (4 items)

[Sweep] === Summary ===
[Sweep] Configs tested: 4/4
[Sweep] Best coverage: 27.9412%
[Sweep] Best config: Best_slots2_L2
[Sweep] Reached 100%: No
```

---

### Simulator 模式 - 模擬分析 March Test

模擬給定的 March Test 序列，計算對錯誤模型的覆蓋率，產生詳細的互動式分析報表。

#### 語法

```bash
docker run --rm -it \
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
docker run --rm -it \
  -v $(pwd)/input:/data -w /data \
  cim-atpg:latest \
  --mode simulator S_C_faults.json Compare.json compare_report.html

# Ablation Study
docker run --rm -it \
  -v $(pwd)/input:/data -w /data \
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
    "fault_primitives": ["< 1D/0D/-/- >"]
  }
]
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

```json
[
  {
    "March_test": "Best_slots2_L2",
    "Pattern": "a(W0); a(W1, C(1)(1)(1));",
    "state_coverage": 0.75,
    "total_coverage": 0.2794
  }
]
```

---

## 🎯 完整範例

### 範例 1：端對端 Generate + Simulate

```bash
# Step 1: 產生最佳 March Test
docker run --rm -it \
  -v $(pwd)/data:/data -w /data \
  cim-atpg:latest \
  --mode generate faults.json generated.json generated.html --max-slots 4 --max-L 6

# Step 2: 將產生的結果與其他 March Test 比較
docker run --rm -it \
  -v $(pwd)/data:/data -w /data \
  cim-atpg:latest \
  --mode simulator faults.json generated.json comparison.html
```

### 範例 2：論文實驗比較

```bash
# 準備 Compare.json 包含要比較的 March Test
docker run --rm -it \
  -v $(pwd)/input:/data -w /data \
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
docker run --rm -it \
  -v $(pwd)/input:/data \
  -v $(pwd)/host_out_generate:/out \
  -w /data \
  cim-atpg:latest \
  --mode generate S_C_faults.json /out/gs.json /out/gs.html

# Simulator：同樣寫到 /out
docker run --rm -it \
  -v $(pwd)/input:/data \
  -v $(pwd)/host_out_sim:/out \
  -w /data \
  cim-atpg:latest \
  --mode simulator S_C_faults.json Compare.json /out/compare.html
```

方式 B：工作目錄即輸出位置（輕量）

```bash
docker run --rm -it \
  -v $(pwd)/input:/work -w /work \
  cim-atpg:latest \
  --mode generate S_C_faults.json out.json out.html
```

權限小提示：若輸出檔在本機顯示 root 擁有者，可加入 `-u $(id -u):$(id -g)` 以你目前使用者身分寫檔。


## 🔧 故障排除

### Q1: 執行時出現 `filesystem error: cannot create directories`

已修復。現在支援直接使用檔名（如 `output.json`）而不需要指定目錄。

### Q2: Docker 輸出是一次性顯示而非逐行

使用 `-it` 參數啟用即時輸出：

```bash
docker run --rm -it ...
```

### Q3: 找不到模式

確認使用 `--mode generate` 或 `--mode simulator`：

```bash
docker run --rm cim-atpg:latest --mode generate --help
docker run --rm cim-atpg:latest --mode simulator --help
```

### Q4: 輸出檔案權限為 root

```bash
docker run --rm -it \
  -u $(id -u):$(id -g) \
  -v $(pwd)/data:/data -w /data \
  cim-atpg:latest ...
```

---

## 📄 授權資訊

Copyright © 2026 NCU EDA Lab. All rights reserved.
