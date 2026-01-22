# 🚀 GreedySweepRunner Docker Image

> **CIM Memory ATPG - Greedy Template Search Tool**  
> 基於模板的貪婪搜尋演算法，用於自動生成高覆蓋率的 March Test 序列

[![Rocky Linux 8](https://img.shields.io/badge/Rocky%20Linux-8-green?logo=rockylinux)](https://rockylinux.org/)
[![C++20](https://img.shields.io/badge/C++-20-blue?logo=cplusplus)](https://isocpp.org/)
[![GCC 13](https://img.shields.io/badge/GCC-13-orange?logo=gnu)](https://gcc.gnu.org/)

---

## 📖 目錄

- [工具簡介](#-工具簡介)
- [快速開始](#-快速開始)
- [建置映像檔 (Build)](#-建置映像檔-build)
- [執行容器 (Run)](#-執行容器-run)
- [參數說明](#-參數說明)
- [輸入/輸出格式](#-輸入輸出格式)
- [完整範例](#-完整範例)
- [雲端部署指南](#-雲端部署指南)
- [故障排除](#-故障排除)

---

## 🔍 工具簡介

### 什麼是 GreedySweepRunner？

**GreedySweepRunner** 是一個自動化測試向量產生工具 (ATPG)，專為 **Compute-in-Memory (CIM)** 記憶體設計。它使用貪婪搜尋演算法，在給定的操作預算內，自動產生能達到最高錯誤覆蓋率的 March Test 序列。

### 核心功能

| 功能 | 說明 |
|-----|------|
| **模板搜尋** | 基於模板抽象層，將「結構」與「值」分離 |
| **參數掃描** | 自動掃描 (slots, L) 參數組合，找出最佳配置 |
| **覆蓋率分析** | 計算 State Coverage、Sensitivity Coverage、Total Coverage |
| **早停機制** | 達到 100% 覆蓋率時自動終止，節省運算時間 |
| **報表生成** | 輸出 JSON 資料 + 互動式 HTML 視覺化報表 |

### 適用情境

- 🧪 **錯誤模型驗證**：驗證 CIM 記憶體的錯誤模型覆蓋率
- 📊 **參數空間探索**：找出最佳的 March Test 配置參數
- 🔬 **學術研究**：比較不同演算法的覆蓋率效能
- 🏭 **生產測試**：生成生產環境使用的測試向量

---

## ⚡ 快速開始

```bash
# 1. 建置映像檔
docker build -f docker/Dockerfile.GreedySweep -t greedysweep:latest .

# 2. 查看說明
docker run --rm greedysweep:latest --help

# 3. 執行測試 (掛載本機資料)
docker run --rm \
  -v $(pwd)/input:/data \
  -w /data \
  greedysweep:latest \
  faults.json output.json output.html
```

---

## 🔨 建置映像檔 (Build)

### 目錄結構要求

```
your_project/
├── docker/
│   └── Dockerfile.GreedySweep    # Dockerfile
├── include/                       # C++ header files
│   ├── FaultSimulator.hpp
│   ├── FpParserAndTpGen.hpp
│   ├── TemplateSearchers.hpp
│   └── TemplateSearchReport.hpp
└── src/
    └── GreedySweepRunner.cpp      # 主程式
```

### Build 指令

```bash
# 在專案根目錄執行
docker build -f docker/Dockerfile.GreedySweep -t greedysweep:v1 .
```

#### Build 參數說明

| 參數 | 說明 | 範例 |
|-----|------|------|
| `-f` | 指定 Dockerfile 路徑 | `-f docker/Dockerfile.GreedySweep` |
| `-t` | 設定映像檔標籤 | `-t greedysweep:v1` |
| `.` | Build Context (當前目錄) | 必須包含 `include/` 和 `src/` |

#### 驗證建置成功

```bash
# 檢查映像檔
docker images | grep greedysweep

# 預期輸出
# greedysweep   v1   abc123def456   10 seconds ago   850MB
```

---

## 🏃 執行容器 (Run)

### 基本語法（即時輸出建議）

```bash
docker run --rm -it \
  -v <本機路徑>:<容器路徑> \
  -w <工作目錄> \
  greedysweep:v1 \
  <輸入檔> <輸出JSON> <輸出HTML> [選項]
```

> 小技巧：
> - `-t` 會配置 TTY，搭配程式內的即時 flush，可逐步顯示進度。
> - 若需要更保險的行為，可覆寫 entrypoint 使用 `stdbuf`：
>   ```bash
>   docker run --rm -it \
>     -v $(pwd)/data:/data \
>     -w /data \
>     --entrypoint sh \
>     greedysweep:v1 -lc "stdbuf -oL -eL GreedySweepRunner faults.json o.json o.html"
>   ```

### 執行模式

#### 模式 1：查看說明

```bash
docker run --rm greedysweep:v1 --help
```

**輸出：**
```
Usage: GreedySweepRunner [faults.json] [output.json] [output.html] [--start-slots N] [--start-L M] [--max-slots X] [--max-L Y]

  faults.json     : 輸入檔案路徑 (預設: input/S_C_faults.json)
  output.json     : 輸出 JSON 檔案 (預設: output/GreedySweep_Bests.json)
  output.html     : 輸出 HTML 檔案 (預設: output/GreedySweep_Bests.html)

  --start-slots N : 起始 slots 數 (預設 1)
  --start-L M     : 起始 L 數 (預設 1)
  --max-slots X   : 單一 element 最大 op 數 (預設 4)
  --max-L Y       : 最大 element 數量 (預設 6)
```

#### 模式 2：掛載本機資料執行

```bash
# 準備資料目錄
mkdir -p my_data
cp your_faults.json my_data/

# 執行分析
docker run --rm -it \
  -v $(pwd)/my_data:/workspace \
  -w /workspace \
  greedysweep:v1 \
  your_faults.json result.json result.html --max-slots 4 --max-L 6
```

#### 模式 3：模擬雲端環境

```bash
# 模擬 ITRI 雲端掛載路徑
docker run --rm -it \
  -v $(pwd)/test_data:/mnt/vol/data/my_project \
  -w /mnt/vol/data/my_project \
  greedysweep:v1 \
  S_C_faults.json output.json output.html
```

---

## 📋 參數說明

### 位置參數

| 參數 | 必要性 | 預設值 | 說明 |
|-----|--------|-------|------|
| `faults.json` | 選填 | `input/S_C_faults.json` | 輸入的錯誤模型檔案 |
| `output.json` | 選填 | `output/GreedySweep_Bests.json` | 輸出的 JSON 結果檔 |
| `output.html` | 選填 | `output/GreedySweep_Bests.html` | 輸出的 HTML 報表檔 |

### 選項參數

| 參數 | 預設值 | 範圍 | 說明 |
|-----|--------|------|------|
| `--start-slots` | 1 | ≥ 1 | 起始的 slots 數量（每個 Element 的操作槽位數） |
| `--start-L` | 1 | ≥ 1 | 起始的 L 數量（March Element 數量） |
| `--max-slots` | 4 | ≥ start-slots | 最大的 slots 數量 |
| `--max-L` | 6 | ≥ start-L | 最大的 L 數量 |

### 參數組合計算

程式會自動產生所有合法的 `(slots, L)` 組合：

```
總組合數 = (max_slots - start_slots + 1) × (max_L - start_L + 1)

例如：--max-slots 4 --max-L 6 (預設)
組合數 = 4 × 6 = 24 種配置
```

### 效能考量

| 配置 | 組合數 | 預估時間 | 適用場景 |
|-----|--------|---------|---------|
| `--max-slots 2 --max-L 2` | 4 | < 1 秒 | 快速測試 |
| `--max-slots 4 --max-L 4` | 16 | 數秒 | 一般分析 |
| `--max-slots 4 --max-L 6` | 24 | 數秒~數分鐘 | 完整掃描 |
| `--max-slots 6 --max-L 8` | 48 | 數分鐘 | 深度搜尋 |

---

## 📁 輸入/輸出格式

### 輸入檔案格式 (faults.json)

```json
[
  {
    "fault_id": "SA0",
    "category": "either_read_or_compute",
    "cell_scope": "single cell",
    "fault_primitives": ["< 1D/0D/-/- >"]
  },
  {
    "fault_id": "TFu",
    "category": "either_read_or_compute",
    "cell_scope": "single cell",
    "fault_primitives": ["< 0W1D/0D/-/- >"]
  },
  {
    "fault_id": "IC11",
    "category": "must_compute",
    "cell_scope": "single cell",
    "fault_primitives": ["< AND1Ci1D/-/-/0Co >"]
  }
]
```

#### 欄位說明

| 欄位 | 類型 | 說明 |
|-----|------|------|
| `fault_id` | string | 錯誤識別碼（如 SA0, TFu, CFid 等） |
| `category` | string | 錯誤類別：`either_read_or_compute`, `must_read`, `must_compute` |
| `cell_scope` | string | 影響範圍：`single cell`, `two cell (row-agnostic)`, `two cell cross row` |
| `fault_primitives` | array | 錯誤原語列表 |

### 輸出檔案格式

#### JSON 輸出 (output.json)

```json
[
  {
    "March_test": "Best_slots2_L2",
    "Pattern": "a(W0); a(W1, C(1)(1)(1));",
    "state_coverage": 0.75,
    "total_coverage": 0.2794
  },
  {
    "March_test": "Best_slots1_L2",
    "Pattern": "a(W0); a(C(1)(1)(1));",
    "state_coverage": 0.3529,
    "total_coverage": 0.1176
  }
]
```

#### 欄位說明

| 欄位 | 類型 | 說明 |
|-----|------|------|
| `March_test` | string | 配置名稱 (格式: `Best_slots{N}_L{M}`) |
| `Pattern` | string | March Test 序列的文字表示 |
| `state_coverage` | float | 狀態覆蓋率 (0.0 ~ 1.0) |
| `total_coverage` | float | 總體覆蓋率 (0.0 ~ 1.0) |

#### HTML 輸出 (output.html)

互動式視覺化報表，包含：
- 📊 覆蓋率進度條
- 📈 各配置的分數比較
- 📋 March Test 序列詳細內容
- 🔍 操作層級的分數細節

---

## 🎯 完整範例

### 範例 1：基本執行流程

```bash
# Step 1: 準備測試資料
mkdir -p test_data
cat > test_data/simple_faults.json << 'EOF'
[
  {"fault_id":"SA0","category":"either_read_or_compute","cell_scope":"single cell","fault_primitives":["< 1D/0D/-/- >"]},
  {"fault_id":"SA1","category":"either_read_or_compute","cell_scope":"single cell","fault_primitives":["< 0D/1D/-/- >"]},
  {"fault_id":"TFu","category":"either_read_or_compute","cell_scope":"single cell","fault_primitives":["< 0W1D/0D/-/- >"]}
]
EOF

# Step 2: 執行分析
docker run --rm \
  -v $(pwd)/test_data:/data \
  -w /data \
  greedysweep:v1 \
  simple_faults.json result.json result.html --max-slots 2 --max-L 2

# Step 3: 檢查結果
ls -la test_data/
cat test_data/result.json
```

### 範例 2：預期輸出

**終端機輸出：**
```
[Sweep] max_slots=2, max_L=2, valid configs=4
[Sweep] (1/4) slots=1, L=1 (ops=1) -> cov=0.0000% [0 ms]
[Sweep] (2/4) slots=1, L=2 (ops=2) -> cov=11.7647% [0 ms]
[Sweep] (3/4) slots=2, L=1 (ops=2) -> cov=0.0000% [0 ms]
[Sweep] (4/4) slots=2, L=2 (ops=4) -> cov=27.9412% [4 ms]
[Sweep] JSON written: ./result.json (4 items)

[Sweep] === Summary ===
[Sweep] Configs tested: 4/4
[Sweep] Best coverage: 27.9412%
[Sweep] Best config: Best_slots2_L2
[Sweep] Reached 100%: No
[Sweep] Total elapsed: 5 ms, greedy time: 4 ms
[Sweep] HTML written: ./result.html
```

**生成的檔案：**
```
test_data/
├── simple_faults.json    # 輸入 (原有)
├── result.json           # JSON 結果 (新增)
└── result.html           # HTML 報表 (新增)
```

### 範例 3：完整錯誤模型分析

```bash
# 使用完整的 S_C 錯誤模型
docker run --rm \
  -v $(pwd)/input:/data \
  -w /data \
  greedysweep:v1 \
  S_C_faults.json full_result.json full_result.html \
  --max-slots 4 --max-L 6
```

---

## ☁️ 雲端部署指南

### ITRI 雲端平台部署

此 Docker Image 已設計為雲端原生 (Cloud Native)，可直接部署至 ITRI 或其他雲端平台。

#### 關鍵設計特點

1. **PATH 環境變數**：執行檔已加入 PATH，可從任何目錄呼叫
2. **相對路徑支援**：透過 `-w` 切換工作目錄後，使用相對路徑即可
3. **Volume 掛載**：支援任意路徑掛載，與雲端儲存整合

#### 雲端執行範例

```bash
# 模擬雲端環境的標準掛載方式
docker run --rm \
  -v /cloud/storage/user123/project:/mnt/vol/data/my_thesis \
  -w /mnt/vol/data/my_thesis \
  greedysweep:v1 \
  faults.json output.json output.html
```

#### 上傳至 Container Registry

```bash
# 標記映像檔
docker tag greedysweep:v1 your-registry.azurecr.io/greedysweep:v1

# 登入 Registry
docker login your-registry.azurecr.io

# 推送映像檔
docker push your-registry.azurecr.io/greedysweep:v1
```

---

## 🔧 故障排除

### 常見問題

#### Q1: Build 時找不到 header 檔案

```
fatal error: nlohmann/json.hpp: No such file or directory
```

**解決方案**：確認 `include/` 目錄存在且包含所需 header 檔案。

#### Q2: 執行時找不到輸入檔案

```
Error: Cannot open file 'faults.json'
```

**解決方案**：
1. 確認已正確掛載 Volume (`-v`)
2. 確認已設定工作目錄 (`-w`)
3. 使用絕對路徑或確認相對路徑正確

#### Q3: 輸出檔案權限問題

輸出檔案可能為 root 所有：

```bash
# 使用當前使用者執行
docker run --rm \
  -u $(id -u):$(id -g) \
  -v $(pwd)/data:/data \
  -w /data \
  greedysweep:v1 ...
```

#### Q4: 記憶體不足
#### Q5: 執行時出現 `filesystem error: cannot create directories: Invalid argument []`

過去版本在僅提供檔名（例如 `output.json`、`output.html`）時，會嘗試建立「空的父目錄」而導致例外。自本版起已修正：

- 你可以直接使用不含目錄的檔名，程式會正常寫檔。
- 若指定含目錄的路徑（如 `results/output.json`），程式會嘗試建立父目錄；若建立失敗，會顯示警告但不會中止。

建議做法：

```bash
docker run --rm \
  -v $(pwd)/data:/data \
  -w /data \
  greedysweep:latest \
  faults.json output.json output.html
```


大型錯誤模型可能需要更多記憶體：

```bash
docker run --rm \
  --memory=4g \
  -v $(pwd)/data:/data \
  -w /data \
  greedysweep:v1 ...
```

### 取得協助

如有其他問題，請提供以下資訊：

1. Docker 版本：`docker --version`
2. 執行的完整指令
3. 錯誤訊息全文
4. 輸入檔案範例

---

## 📄 授權資訊

Copyright © 2026 NCU CIM Lab. All rights reserved.

---

## 📞 聯絡方式

- **維護團隊**：NCU CIM Memory Lab
- **問題回報**：請透過 GitHub Issues 提交
