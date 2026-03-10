# CIM ATPG — Fault Removal Impact Analysis Report

> **測試日期**: 2026-03-04  
> **Baseline 覆蓋率**: 100.0000% (`slots=4, L=6`, 34 faults, 耗時 ~18s)  
> **測試變體總數**: 55 組  

---

## 1. 實驗設計

### 測試策略

| 策略 | 前綴 | 變體數 | 說明 |
|------|------|--------|------|
| Baseline | `v00_` | 1 | 完整 34 個 fault（對照組） |
| Leave-one-out | `v01_` | 34 | 每次只移除 1 個 fault |
| By category | `v02_` | 3 | 移除整個 category |
| By cell_scope | `v03_` | 4 | 移除整個 scope 類型 |
| By fault family | `v04_` | 11 | 移除同族群 fault（如 SA、CFid、IC 等） |
| Combo | `v05_` | 2 | 只留 single-cell 或只留 two-cell |

所有測試使用相同參數：`--max-slots 4 --max-L 6`

---

## 2. 關鍵發現

### 2.1 Leave-One-Out 結果

#### 移除後**有影響**的 fault（10 個）

| Fault ID | 移除後覆蓋率 | 下降幅度 | Best Config | Category | Cell Scope |
|----------|-------------|---------|-------------|----------|------------|
| CIDCB(1,11) | 93.9394% | -6.06% | slots3_L5 | must_compute | two cell cross row |
| CIDD(0,0) | 95.4545% | -4.55% | slots3_L6 | must_read | single cell |
| CIDD(1,0) | 95.4545% | -4.55% | slots3_L6 | must_read | single cell |
| IC00 | 95.4545% | -4.55% | slots4_L5 | must_compute | single cell |
| CIDCB(0,00) | 95.4545% | -4.55% | slots4_L5 | must_compute | two cell cross row |
| CFid(↑,0) | 96.9697% | -3.03% | slots4_L6 | either_read_or_compute | two cell (row-agnostic) |
| CFid(↑,1) | 96.9697% | -3.03% | slots3_L6 | either_read_or_compute | two cell (row-agnostic) |
| CIDDB(1,1) | 96.9697% | -3.03% | slots4_L5 | either_read_or_compute | two cell cross row |
| CIDWDB | 96.9697% | -3.03% | slots4_L5 | either_read_or_compute | two cell cross row |
| CI(10,11) | 98.4848% | -1.52% | slots4_L5 | must_compute | two cell same row |

#### 移除後**無影響**的 fault（24 個）

SA0, SA1, TFu, TFd, CFid(↓,0), CFid(↓,1), CIDWD, CIDDB(0,0), CIDDB(0,1), CIDDB(1,0), CIDD(0,1), CIDD(1,1), CDCFst(00,0), CDCFst(00,1), IC01, IC10, IC11, SDC(11,01), SDC(11,10), CIDC, CI(00,11), CI(01,11), DDCB(0,11), DDCB(1,11)

### 2.2 By Category 結果

| 測試 | 移除 fault 數 | 覆蓋率 | 結論 |
|------|-------------|--------|------|
| `v02_no_either_read_or_compute` | 14 | **95.00%** ✗ | 此 category 含 Greedy 路徑引導者 |
| `v02_no_must_compute` | 14 | 100% ✓ | compute 類 fault 不影響覆蓋 |
| `v02_no_must_read` | 6 | 100% ✓ | must_read 整組移除也不影響 |

### 2.3 By Cell Scope 結果

| 測試 | 移除 fault 數 | 覆蓋率 | 結論 |
|------|-------------|--------|------|
| `v03_no_single_cell` | 16 | **94.44%** ✗ | single-cell 中有 Greedy 關鍵引導者 |
| `v03_no_two_cell_row-agnostic` | 6 | 100% ✓ | row-agnostic 移除無影響 |
| `v03_no_two_cell_cross_row` | 11 | **97.83%** ✗ | cross-row 含關鍵 fault |
| `v03_no_two_cell_same_row` | 1 | **98.48%** ✗ | 唯一的 CI(10,11) 是關鍵的 |

### 2.4 By Fault Family 結果

| 族群 | 移除 fault 數 | 覆蓋率 | 影響 |
|------|-------------|--------|------|
| **CIDD** | 4 | **93.33%** | 🔴 影響最大 |
| **CFid** | 4 | **95.00%** | 🔴 |
| **IC** | 4 | **95.00%** | 🔴 |
| **CIDCB** | 2 | **95.31%** | 🔴 |
| **CIDWD** | 2 | **96.88%** | 🟡 |
| **CI** | 3 | **98.39%** | 🟡 |
| **DDCB** | 2 | **98.44%** | 🟡 |
| CDCFst | 2 | 100% ✓ | 🟢 無影響 |
| CIDDB | 4 | 100% ✓ | 🟢 無影響 |
| SA | 2 | 100% ✓ | 🟢 無影響 |
| SDC | 2 | 100% ✓ | 🟢 無影響 |

### 2.5 Combo 結果

| 測試 | 保留 fault 數 | 覆蓋率 | 結論 |
|------|-------------|--------|------|
| 只保留 single cell | 16 | **100%** ✓ | 16 個 fault 就足以達到 100% |
| 只保留 two cell | 18 | **94.44%** ✗ | 18 個 two-cell fault 反而不夠 |

> **反直覺結論**：更少的 fault（16 個 single cell）比更多的 fault（18 個 two cell）覆蓋率還高。

---

## 3. 根因分析

### 3.1 主因：Greedy 演算法的「搭便車效應」

Greedy 搜尋器在每一步選擇覆蓋最多未覆蓋 fault 的 march element。當某些 fault 存在時，它們的 test primitive 會**驅使** Greedy 選出特定的 element，而這些 element **恰好也能附帶覆蓋其他 fault**。

```
Full set:  Fault A 的 TP → 迫使 Greedy 選 Element X → Element X 同時覆蓋 B, C, D
刪除 A:   沒有 A 的 TP → Greedy 改選 Element Y → Element Y 只覆蓋 B, C（D 落選）
```

**證據**：CFid(↑,0) 和 CFid(↑,1) 移除後覆蓋率下降，但 CFid(↓,0) 和 CFid(↓,1) 不會。兩者的差異僅在 TP 方向（`0W1D` vs `1W0D`）。↑ 類的 `0W1D` primitive 迫使 Greedy 選出「Write 0 → Read」類型的 element，而這些 element 恰好也覆蓋了其他 fault。↓ 類的 `1W0D` 則因為 TFd 已經提供類似的 op 覆蓋，移除後不影響 Greedy 路徑。

### 3.2 次因：搜尋空間收斂在次優解

觀察「有影響」case 的 best_config：

- 6/10 的 best_config 是 `slots4_L5` 或 `slots3_L5/L6`，**而非最大的 `slots4_L6`**
- 這表示 Greedy 在較小空間找到了局部最優，但全域最優可能在 `slots4_L6` 或更大的空間

Greedy 是 **deterministic single-path** 搜尋，不會回溯。一旦前幾步選錯，後面再多加 element 也不一定能補回。

### 3.3 Scorer 加權偏移

目前的 scorer 公式：

```
score = 0.9 × state_coverage + 0.5 × total_coverage − 0.01 × op_count
```

`state_coverage`（=每個 fault state 被測試的比例）權重 0.9，遠高於 `total_coverage` 0.5。當某些 fault 被移除後，state_coverage 的分佈改變，scorer 可能引導 Greedy 往「高 state 覆蓋但低 total 覆蓋」的方向走。

### 3.4 關鍵 fault 的 Primitive 特徵分析

| Fault | Primitive | 特殊性 |
|-------|-----------|--------|
| CIDD(0,0) | `< 0Ci1D/0D/-/- >` | 唯一需要 Ci=0 搭配 D=1→0 的 read |
| CIDD(1,0) | `< 1Ci1D/0D/-/- >` | 唯一需要 Ci=1 搭配 D=1→0 的 read |
| IC00 | `< AND0Ci0D/-/-/1Co >` | 唯一需要 AND(0,0)→1 的 compute |
| CIDCB(0,00) | `< 0Ci; AND0Ci0D/-/-/1Co >` | 結合 Ci 與 AND compute |
| CIDCB(1,11) | `< 1Ci; AND1Ci1D/-/-/0Co >` | 結合 Ci 與 AND compute |
| CFid(↑,0) | `< 0W1D; 1D/0D/-/- >` | 需要 W0→D1 的 coupling |
| CFid(↑,1) | `< 0W1D; 0D/1D/-/- >` | 需要 W0→D1 的 coupling |
| CIDDB(1,1) | `< 1Ci; 0D/1D/-/- >` | 需要 Ci=1 的 disturb read |
| CIDWDB | `< 1Ci; 1W0D/1D/-/- >` + `< 1Ci; 0W1D/0D/-/- >` | 雙 primitive，需要 Ci=1 的 write+read |
| CI(10,11) | `< 1Ci0D; AND1Ci1D/-/-/0Co >` | 唯一 same-row 的 compute-interference |

這些 fault 都提供了**獨特的 op 組合需求**，迫使 Greedy 選出多元化的 march element。一旦移除，Greedy 傾向選更「同質化」的 element，覆蓋面反而縮小。

---

## 4. 結果摘要矩陣

```
分類維度         有影響(✗)  無影響(✓)   影響比例
─────────────  ─────────  ─────────  ─────────
Leave-one-out     10         24        29.4%
By category        1          2        33.3%
By cell_scope      3          1        75.0%
By family          7          4        63.6%
```

---

## 5. 附錄：完整測試結果

| # | 檔案 | 剩餘 fault 數 | 覆蓋率 | Best Config | 100%? | 耗時(ms) |
|---|------|-------------|--------|-------------|-------|---------|
| 1 | v00_full.json | 34 | 100.0000% | slots4_L6 | Yes | 18413 |
| 2 | v01_drop_SA0.json | 33 | 100.0000% | slots4_L6 | Yes | 17964 |
| 3 | v01_drop_SA1.json | 33 | 100.0000% | slots4_L6 | Yes | 17878 |
| 4 | v01_drop_TFu.json | 33 | 100.0000% | slots4_L6 | Yes | 17603 |
| 5 | v01_drop_TFd.json | 33 | 100.0000% | slots4_L6 | Yes | 17800 |
| 6 | v01_drop_CFid(↓,0).json | 33 | 100.0000% | slots4_L6 | Yes | 17894 |
| 7 | v01_drop_CFid(↓,1).json | 33 | 100.0000% | slots4_L6 | Yes | 18293 |
| 8 | v01_drop_CFid(↑,0).json | 33 | **96.9697%** | slots4_L6 | No | 17480 |
| 9 | v01_drop_CFid(↑,1).json | 33 | **96.9697%** | slots3_L6 | No | 17165 |
| 10 | v01_drop_CIDWD.json | 33 | 100.0000% | slots4_L6 | Yes | 18115 |
| 11 | v01_drop_CIDDB(0,0).json | 33 | 100.0000% | slots4_L6 | Yes | 16929 |
| 12 | v01_drop_CIDDB(0,1).json | 33 | 100.0000% | slots4_L6 | Yes | 17894 |
| 13 | v01_drop_CIDDB(1,0).json | 33 | 100.0000% | slots4_L6 | Yes | 18420 |
| 14 | v01_drop_CIDDB(1,1).json | 33 | **96.9697%** | slots4_L5 | No | 19966 |
| 15 | v01_drop_CIDWDB.json | 33 | **96.9697%** | slots4_L5 | No | 19455 |
| 16 | v01_drop_CIDD(0,0).json | 33 | **95.4545%** | slots3_L6 | No | 18633 |
| 17 | v01_drop_CIDD(0,1).json | 33 | 100.0000% | slots4_L6 | Yes | 17979 |
| 18 | v01_drop_CIDD(1,0).json | 33 | **95.4545%** | slots3_L6 | No | 20292 |
| 19 | v01_drop_CIDD(1,1).json | 33 | 100.0000% | slots4_L6 | Yes | 20239 |
| 20 | v01_drop_CDCFst(00,0).json | 33 | 100.0000% | slots3_L6 | Yes | 7352 |
| 21 | v01_drop_CDCFst(00,1).json | 33 | 100.0000% | slots4_L5 | Yes | 12487 |
| 22 | v01_drop_IC00.json | 33 | **95.4545%** | slots4_L5 | No | 19321 |
| 23 | v01_drop_IC01.json | 33 | 100.0000% | slots4_L6 | Yes | 18749 |
| 24 | v01_drop_IC10.json | 33 | 100.0000% | slots4_L6 | Yes | 19642 |
| 25 | v01_drop_IC11.json | 33 | 100.0000% | slots4_L6 | Yes | 18549 |
| 26 | v01_drop_SDC(11,01).json | 33 | 100.0000% | slots4_L6 | Yes | 19783 |
| 27 | v01_drop_SDC(11,10).json | 33 | 100.0000% | slots4_L6 | Yes | 21847 |
| 28 | v01_drop_CIDC.json | 33 | 100.0000% | slots4_L6 | Yes | 18076 |
| 29 | v01_drop_CI(00,11).json | 33 | 100.0000% | slots4_L6 | Yes | 18195 |
| 30 | v01_drop_CI(01,11).json | 33 | 100.0000% | slots4_L6 | Yes | 17621 |
| 31 | v01_drop_CI(10,11).json | 33 | **98.4848%** | slots4_L5 | No | 18747 |
| 32 | v01_drop_CIDCB(0,00).json | 33 | **95.4545%** | slots4_L5 | No | 18305 |
| 33 | v01_drop_CIDCB(1,11).json | 33 | **93.9394%** | slots3_L5 | No | 17765 |
| 34 | v01_drop_DDCB(0,11).json | 33 | 100.0000% | slots4_L6 | Yes | 18120 |
| 35 | v01_drop_DDCB(1,11).json | 33 | 100.0000% | slots4_L6 | Yes | 17793 |
| 36 | v02_no_either_read_or_compute | 20 | **95.0000%** | slots4_L5 | No | 9321 |
| 37 | v02_no_must_compute | 20 | 100.0000% | slots3_L6 | Yes | 5008 |
| 38 | v02_no_must_read | 28 | 100.0000% | slots3_L5 | Yes | 2963 |
| 39 | v03_no_single_cell | 18 | **94.4444%** | slots3_L6 | No | 13138 |
| 40 | v03_no_two_cell_row-agnostic | 28 | 100.0000% | slots3_L6 | Yes | 5707 |
| 41 | v03_no_two_cell_cross_row | 23 | **97.8261%** | slots4_L5 | No | 13898 |
| 42 | v03_no_two_cell_same_row | 33 | **98.4848%** | slots4_L5 | No | 25878 |
| 43 | v04_no_family_CDCFst | 32 | 100.0000% | slots3_L6 | Yes | 6493 |
| 44 | v04_no_family_CFid | 30 | **95.0000%** | slots4_L5 | No | 18769 |
| 45 | v04_no_family_CI | 31 | **98.3871%** | slots4_L5 | No | 18392 |
| 46 | v04_no_family_CIDCB | 32 | **95.3125%** | slots3_L6 | No | 17551 |
| 47 | v04_no_family_CIDD | 30 | **93.3333%** | slots4_L5 | No | 17142 |
| 48 | v04_no_family_CIDDB | 30 | 100.0000% | slots4_L6 | Yes | 14706 |
| 49 | v04_no_family_CIDWD | 32 | **96.8750%** | slots4_L5 | No | 17826 |
| 50 | v04_no_family_DDCB | 32 | **98.4375%** | slots4_L5 | No | 18361 |
| 51 | v04_no_family_IC | 30 | **95.0000%** | slots4_L5 | No | 17163 |
| 52 | v04_no_family_SA | 32 | 100.0000% | slots4_L6 | Yes | 20903 |
| 53 | v04_no_family_SDC | 32 | 100.0000% | slots4_L6 | Yes | 23534 |
| 54 | v05_single_cell_only | 16 | 100.0000% | slots3_L5 | Yes | 1351 |
| 55 | v05_two_cell_only | 18 | **94.4444%** | slots3_L6 | No | 13400 |
