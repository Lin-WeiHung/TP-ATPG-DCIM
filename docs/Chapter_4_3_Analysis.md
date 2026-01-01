# Chapter 4.3 程式碼分析報告：ATG & Fault Simulation

> **術語定義**：
> - "C" 或 "Ci" 指的是 **Computing Input**，而非 Carry
> - 系統涉及 "C-System"（Computing-in-Memory 結構），其中 Computing Inputs 相互作用

---

## 4.3.1 CSS Space Reduction (10→6 有效性)

### 論文邏輯
搜尋空間從 10 個 slot 縮減為 6-slot 模型。

### 程式碼驗證

#### 1. CrossState 定義 — 5-Slot 模型

```cpp
// FpParserAndTpGen.hpp (Lines 553-566)
struct CrossState {
    DC A0, A1, A2_CAS, A3, A4;  // 5 個 Slot：上、左、中(pivot)、右、下
};
```

**Cross-Shape 5-Slot 模型佈局**：

```
        ┌────┐
        │ A0 │  (Top - 上方)
        └────┘
           │
┌────┐  ┌────────┐  ┌────┐
│ A1 │──│ A2_CAS │──│ A3 │  (Left - Center/Pivot - Right)
└────┘  └────────┘  └────┘
           │
        ┌────┐
        │ A4 │  (Bottom - 下方)
        └────┘
```

這對應論文中 10→6 的縮減：原本需考慮多個時間步驟的狀態，現在透過 **Cross-Shape 空間模型** 將相關 cell 的 D/C 狀態收斂至 5 個位置。

#### 2. 邊界條件處理 — `enforceDCrule()`

```cpp
// FpParserAndTpGen.hpp (Lines 567-584)
void enforceDCrule() {
    // D 值合併規則：同側必須一致
    Val targetDl = mergeD(A0.D, A1.D);  // 左側 D 收斂
    Val targetDr = mergeD(A3.D, A4.D);  // 右側 D 收斂
    A0.D = A1.D = targetDl;
    A3.D = A4.D = targetDr;
    
    // C 值（Computing Input）合併規則：同列 Ci 必須一致
    Val targetC = mergeC(A1.C, A2_CAS.C, A3.C);  // C1=C2=C3
    A1.C = A2_CAS.C = A3.C = targetC;
}
```

**關鍵約束**：
- $D_{left} = D_{A0} = D_{A1}$（左側 D 一致性）
- $D_{right} = D_{A3} = D_{A4}$（右側 D 一致性）
- $C_{row} = C_{A1} = C_{A2} = C_{A3}$（同列 Computing Input 一致性）

這些物理約束有效地將狀態空間從理論上的多維組合縮減至可管理的範圍。

---

## 4.3.2 Pivot Rule (Aggressor vs. Victim 映射)

### 論文邏輯
- **Aggressor Pivot**：當 Aggressor 側有操作時（如 `CFid(↑,0)` 的 `0W1D`）
- **Victim Pivot**：當 Aggressor 側僅設置狀態而無操作時（如 `CIDDB(1,0)` 的 `1Ci`）

### 目標 Fault 原始定義

| Fault ID | Category | Cell Scope | Fault Primitive |
|----------|----------|------------|-----------------|
| `CFid(↑,0)` | either_read_or_compute | two cell (row-agnostic) | `< 0W1D; 1D/0D/-/- >` |
| `CIDDB(1,0)` | either_read_or_compute | two cell cross row | `< 1Ci; 1D/0D/-/- >` |

### 程式碼驗證

#### 1. Pivot 決策函式 — `decidePivot()`

```cpp
// FpParserAndTpGen.hpp (Lines 709-714)
inline WhoIsPivot OrientationSelector::decidePivot(const FPExpr& fp) const {
    // 若 Sa（Aggressor side）有 Op，則 Pivot 必須是 Aggressor
    if (fp.Sa.has_value() && fp.Sa->has_ops()) 
        return WhoIsPivot::Aggressor;
    // 否則預設 Victim
    return WhoIsPivot::Victim;
}
```

#### 2. 兩個 Fault 的 Pivot 決策分析

**CFid(↑,0): `< 0W1D; 1D/0D/-/- >`**

```
解析結果：
  Sa = "0W1D" → pre_D=0, ops=[Write(1)], last_D=1
  Sv = "1D"   → pre_D=1, ops=[], last_D=1
  
decidePivot() 執行：
  fp.Sa.has_value() = true
  fp.Sa->has_ops() = true (有 Write 操作)
  → return WhoIsPivot::Aggressor ✓
```

**CIDDB(1,0): `< 1Ci; 1D/0D/-/- >`**

```
解析結果：
  Sa = "1Ci" → Ci=1, ops=[], last_D=X (僅設置 Ci，無操作)
  Sv = "1D"  → pre_D=1, ops=[], last_D=1
  
decidePivot() 執行：
  fp.Sa.has_value() = true
  fp.Sa->has_ops() = false (ops 為空)
  → return WhoIsPivot::Victim ✓
```

#### 3. 方向規劃差異

**CFid(↑,0) — Row-Agnostic + Aggressor Pivot**

```cpp
// FpParserAndTpGen.hpp (Lines 687-696)
inline void OrientationSelector::plan_rowAgnostic(...) const {
    WhoIsPivot pivot = decidePivot(fp);  // → Aggressor
    if (pivot == WhoIsPivot::Aggressor) {
        // Aggressor 在中心 (A2_CAS)，Victim 分佈在右下或左上
        plans.push_back({A_LT_V, Aggressor, {Slot::A3, Slot::A4}});
        plans.push_back({A_GT_V, Aggressor, {Slot::A0, Slot::A1}});
    }
}
```

**CIDDB(1,0) — Cross-Row + Victim Pivot**

```cpp
// FpParserAndTpGen.hpp (Lines 699-708)
inline void OrientationSelector::plan_crossRow(...) const {
    WhoIsPivot pivot = decidePivot(fp);  // → Victim
    if (pivot == WhoIsPivot::Victim) {
        // Victim 在中心 (A2_CAS)，Aggressor 在上方或下方
        plans.push_back({A_LT_V, Victim, {Slot::A0}});
        plans.push_back({A_GT_V, Victim, {Slot::A4}});
    }
}
```

### Pivot Rule 導致的 TP 展開差異

| 特性 | CFid(↑,0) | CIDDB(1,0) |
|------|-----------|------------|
| **Pivot** | Aggressor | Victim |
| **中心 Cell (A2_CAS)** | Aggressor 狀態 | Victim 狀態 |
| **NonPivotSlots (A<V)** | {A3, A4} | {A0} |
| **NonPivotSlots (A>V)** | {A0, A1} | {A4} |
| **TP 數量** | 16 個 | 4 個 |

---

## 4.3.3 Positional Expansion (偵測器配置)

### 論文邏輯

- **Case A (Aggressor Pivot, Non-C-System)**：偵測狀態不變，期望 `R1` 和 `C{X,1,X}`
- **Case B (Victim Pivot, C-System)**：偵測狀態依拓撲關係變化
  - $A < V$：`C{1,1,X}`
  - $A > V$：`C{X,1,1}`

### 程式碼驗證

#### 1. CFid(↑,0) 的展開邏輯 — Non-C-System

**為何是 Non-C-System？**

```cpp
// FpParserAndTpGen.hpp (Lines 834-850)
inline bool DetectorPlanner::canComputeSetCi(...) const {
    // ...
    if (scope == CellScope::TwoCellRowAgnostic) return false; 
    // Row-agnostic 不經 compute 傳 Ci → Non-C-System
    if (scope == CellScope::TwoCellSameRow) return false;
    return true; // 僅 Cross-row 可經 compute 傳 Ci
}
```

CFid(↑,0) 是 `TwoCellRowAgnostic`，所以 `canComputeSetCi()` 返回 `false`，偵測器的 C_T 和 C_B **不會被設置**。

**位置展開邏輯**（Aggressor Pivot 特有）：

```cpp
// FpParserAndTpGen.hpp (Lines 794-820)
inline vector<Detector> DetectorPlanner::expandPosVariants(...) const {
    // pivot 為 Aggressor：針對 ^ 與 ;，各產生多個變體
    if (plan.pivot == WhoIsPivot::Aggressor) {
        vector<Detector> out;
        // SameElementHead (^) 變體
        { d.pos = PositionMark::SameElementHead; d.order = Ascending/Descending; out.push_back(d); }
        { d.pos = PositionMark::SameElementHead; ... }
        // NextElementHead (;) 變體
        { d.pos = PositionMark::NextElementHead; ... }
        { d.pos = PositionMark::NextElementHead; ... }
        return out;  // 共 4 個位置變體
    }
    // ...
}
```

**結果**：CFid(↑,0) 產生 **16 個 TP**：
- 2 方向 (A<V, A>V) × 2 偵測類型 (Read, ComputeAnd) × 4 位置變體 (^, ^, ;, ;) = 16

#### 2. CIDDB(1,0) 的展開邏輯 — C-System

**為何是 C-System？**

CIDDB(1,0) 是 `TwoCellCrossRow`，且 Sa 僅設置 Ci 而無 Read/Write 操作：

```cpp
inline bool DetectorPlanner::canComputeSetCi(...) const {
    if (fp.s_has_any_op) {
        // 檢查是否有 Read/Write 操作
        for (const auto& op : fp.Sv.ops) {
            if (op.kind == OpKind::Read || op.kind == OpKind::Write) 
                return false;  // 有 Read/Write 則不可設置
        }
    }
    // ...
    return true; // Cross-row 且無 Read/Write → 可設置 Ci
}
```

**C_T/C_B 設置邏輯**：

```cpp
// FpParserAndTpGen.hpp (Lines 785-792)
inline void DetectorPlanner::setComputeTB(...) const {
    if (canComputeSetCi(fp, scope)) {
        const Val sa_ci = fp.Sa ? fp.Sa->Ci.value_or(Val::X) : Val::X;
        // sa_ci = 1 (來自 "1Ci")
        
        if (plan.group == OrientationGroup::A_LT_V) 
            d.detectOp.C_T = sa_ci;  // A<V → C_T=1 → C{1,1,X}
        
        if (plan.group == OrientationGroup::A_GT_V) 
            d.detectOp.C_B = sa_ci;  // A>V → C_B=1 → C{X,1,1}
        
        d.has_set_Ci = true;
    }
}
```

**位置展開邏輯**（Victim Pivot）：

```cpp
inline vector<Detector> DetectorPlanner::expandPosVariants(...) const {
    if (plan.pivot != WhoIsPivot::Aggressor) {
        // Victim Pivot：僅產生 # 位置，單一變體
        Detector d = base;
        d.pos = PositionMark::Adjacent; // #
        d.order = Detector::AddrOrder::None;
        return { d };  // 僅 1 個位置變體
    }
    // ...
}
```

**結果**：CIDDB(1,0) 產生 **4 個 TP**：
- 2 方向 (A<V, A>V) × 2 偵測類型 (Read, ComputeAnd) × 1 位置變體 (#) = 4

### 關鍵差異對比

| 特性 | CFid(↑,0) | CIDDB(1,0) |
|------|-----------|------------|
| **Cell Scope** | Row-Agnostic | Cross-Row |
| **C-System** | ❌ 否 | ✅ 是 |
| **canComputeSetCi()** | `false` | `true` |
| **Detector C_T/C_B** | 始終 X | 依方向設置 |
| **A<V 時 Compute** | `C{X,1,X}` | `C{1,1,X}` |
| **A>V 時 Compute** | `C{X,1,X}` | `C{X,1,1}` |
| **Position 展開** | ^, ;（4 變體） | # 僅（1 變體） |
| **has_set_Ci** | `false` | `true` |

---

## 4.3.4 Irreversibility Check (TP 有效性)

### 論文邏輯

`CIDWDB` 特殊：有 `sensitization` 階段（`ops_before_detect`）**且** 涉及 C-System，需檢查 Ci 是否可逆/一致。

### 目標 Fault 原始定義

| Fault ID | Category | Cell Scope | Fault Primitives |
|----------|----------|------------|------------------|
| `CIDWDB` | either_read_or_compute | two cell cross row | `< 1Ci; 1W0D/1D/-/- >`, `< 1Ci; 0W1D/0D/-/- >` |
| `CIDDB(1,0)` | either_read_or_compute | two cell cross row | `< 1Ci; 1D/0D/-/- >` |

### 程式碼驗證

#### 1. 敏化操作提取 — `ops_before_detect()`

```cpp
// FpParserAndTpGen.hpp (Lines 875-896)
inline vector<Op> StateAssembler::ops_before_detect(...) const {
    vector<Op> out;
    if (fp.Sa.has_value()) {
        out.insert(out.end(), fp.Sa->ops.begin(), fp.Sa->ops.end());
    }
    out.insert(out.end(), fp.Sv.ops.begin(), fp.Sv.ops.end());
    // ...
    return out;
}
```

**CIDWDB 第一個 primitive `< 1Ci; 1W0D/1D/-/- >`**：
```
Sa = "1Ci" → Ci=1, ops=[]
Sv = "1W0D" → pre_D=1, ops=[Write(0)], last_D=0

ops_before_detect = [] + [Write(0)] = [Write(0)]
```

**CIDDB(1,0) `< 1Ci; 1D/0D/-/- >`**：
```
Sa = "1Ci" → Ci=1, ops=[]
Sv = "1D" → pre_D=1, ops=[], last_D=1

ops_before_detect = [] + [] = []
```

#### 2. 衝突檢測機制

CIDWDB 因含有 Write 操作，`canComputeSetCi()` 返回 `false`：

```cpp
inline bool DetectorPlanner::canComputeSetCi(...) const {
    if (fp.s_has_any_op) {
        for (const auto& op : fp.Sv.ops) {
            if (op.kind == OpKind::Write) return false;  // ← CIDWDB 觸發此條件
        }
    }
    // ...
}
```

**結果**：CIDWDB 的偵測器 **無法透過 Compute 設置 C_T/C_B**（`has_set_Ci=false`），但 Aggressor 的 Ci 仍記錄在 `cross_state`。

#### 3. 一致性檢查 — `mergeC()`

```cpp
// FpParserAndTpGen.hpp (Lines 576-584)
Val mergeC(Val a, Val b, Val c) {
    // 0 與 1 衝突時拋出異常
    if ((a == Val::Zero && b == Val::One) || ...) {
        throw runtime_error("Conflicting Ci values in mergeC");
    }
    // ...
}
```

CIDWDB 在 `enforceDCrule()` 中通過一致性檢查，因為 Ci 值不衝突（僅 A0 或 A4 設置 Ci=1，其他為 X）。

### CIDWDB vs CIDDB(1,0) 複雜度對比

| 特性 | CIDDB(1,0) | CIDWDB |
|------|------------|--------|
| **ops_before_detect** | `[]` (空) | `[Write(0)]` 或 `[Write(1)]` |
| **敏化操作** | 無 | 有 (需先寫入) |
| **canComputeSetCi()** | `true` | `false` (因有 Write) |
| **Detector C_T/C_B** | 依方向設置 | 始終 X |
| **has_set_Ci** | `true` | `false` |
| **Primitives 數量** | 1 | 2 |
| **生成 TP 數量** | 4 | 8 |

---

## 4.3.5 Group Coverage

### 程式碼驗證

程式碼透過 **`OrientationGroup` 機制** 實現分組：

```cpp
// FpParserAndTpGen.hpp (Lines 548)
enum class OrientationGroup { Single, A_LT_V, A_GT_V };
```

**分組策略**：
1. **Single**：單 cell fault，不需分組
2. **A_LT_V**（$A < V$）：Aggressor 地址 < Victim 地址
3. **A_GT_V**（$A > V$）：Aggressor 地址 > Victim 地址

每個 fault 會被展開為多個 TP，根據 `OrientationGroup` 分類，相容的 TP 可並行模擬。

---

## 完整 TP 展開表格

### CFid(↑,0) — 16 個 TP

**原始定義**：`< 0W1D; 1D/0D/-/- >` (two cell row-agnostic)

| # | Group | A0.D | A0.C | A1.D | A1.C | A2.D | A2.C | A3.D | A3.C | A4.D | A4.C | Detector | Position | Order | ops_before | has_set_Ci |
|---|-------|------|------|------|------|------|------|------|------|------|------|----------|----------|-------|------------|------------|
| 1 | A<V | X | X | X | X | 0 | X | 1 | X | 1 | X | Read(1) | ^ | Asc | [W1] | false |
| 2 | A<V | X | X | X | X | 0 | X | 1 | X | 1 | X | Read(1) | ^ | Asc | [W1] | false |
| 3 | A<V | X | X | X | X | 0 | X | 1 | X | 1 | X | Read(1) | ; | Asc | [W1] | false |
| 4 | A<V | X | X | X | X | 0 | X | 1 | X | 1 | X | Read(1) | ; | Asc | [W1] | false |
| 5 | A<V | X | X | X | X | 0 | X | 1 | X | 1 | X | C{X,1,X} | ^ | Asc | [W1] | false |
| 6 | A<V | X | X | X | X | 0 | X | 1 | X | 1 | X | C{X,1,X} | ^ | Asc | [W1] | false |
| 7 | A<V | X | X | X | X | 0 | X | 1 | X | 1 | X | C{X,1,X} | ; | Asc | [W1] | false |
| 8 | A<V | X | X | X | X | 0 | X | 1 | X | 1 | X | C{X,1,X} | ; | Asc | [W1] | false |
| 9 | A>V | 1 | X | 1 | X | 0 | X | X | X | X | X | Read(1) | ^ | Desc | [W1] | false |
| 10 | A>V | 1 | X | 1 | X | 0 | X | X | X | X | X | Read(1) | ^ | Desc | [W1] | false |
| 11 | A>V | 1 | X | 1 | X | 0 | X | X | X | X | X | Read(1) | ; | Desc | [W1] | false |
| 12 | A>V | 1 | X | 1 | X | 0 | X | X | X | X | X | Read(1) | ; | Desc | [W1] | false |
| 13 | A>V | 1 | X | 1 | X | 0 | X | X | X | X | X | C{X,1,X} | ^ | Desc | [W1] | false |
| 14 | A>V | 1 | X | 1 | X | 0 | X | X | X | X | X | C{X,1,X} | ^ | Desc | [W1] | false |
| 15 | A>V | 1 | X | 1 | X | 0 | X | X | X | X | X | C{X,1,X} | ; | Desc | [W1] | false |
| 16 | A>V | 1 | X | 1 | X | 0 | X | X | X | X | X | C{X,1,X} | ; | Desc | [W1] | false |

**關鍵觀察**：
- **Pivot = Aggressor**：A2_CAS 存放 Aggressor 的狀態 (pre_D=0)
- **NonPivotSlots**：存放 Victim 狀態 (D=1)
- **Non-C-System**：所有 C 欄位為 X，`has_set_Ci=false`
- **Position 展開**：因 Aggressor Pivot，產生 ^/; 位置變體

---

### CIDDB(1,0) — 4 個 TP

**原始定義**：`< 1Ci; 1D/0D/-/- >` (two cell cross row)

| # | Group | A0.D | A0.C | A1.D | A1.C | A2.D | A2.C | A3.D | A3.C | A4.D | A4.C | Detector | Position | Order | ops_before | has_set_Ci |
|---|-------|------|------|------|------|------|------|------|------|------|------|----------|----------|-------|------------|------------|
| 1 | A<V | X | **1** | X | X | 1 | X | X | X | X | X | Read(1) | # | None | [] | false |
| 2 | A<V | X | X | X | X | 1 | X | X | X | X | X | **C{1,1,X}** | # | None | [] | **true** |
| 3 | A>V | X | X | X | X | 1 | X | X | X | X | **1** | Read(1) | # | None | [] | false |
| 4 | A>V | X | X | X | X | 1 | X | X | X | X | X | **C{X,1,1}** | # | None | [] | **true** |

**關鍵觀察**：
- **Pivot = Victim**：A2_CAS 存放 Victim 的狀態 (pre_D=1)
- **NonPivotSlots**：存放 Aggressor 狀態 (Ci=1)
- **C-System**：
  - TP#1: A0.C=1 (Read 偵測，Ci 記錄在 cross_state)
  - TP#2: Detector C_T=1 → `C{1,1,X}` (A<V)
  - TP#3: A4.C=1 (Read 偵測)
  - TP#4: Detector C_B=1 → `C{X,1,1}` (A>V)
- **Position 固定**：因 Victim Pivot，僅產生 # 位置

---

### CIDWDB — 8 個 TP

**原始定義**：
- Primitive 1: `< 1Ci; 1W0D/1D/-/- >`
- Primitive 2: `< 1Ci; 0W1D/0D/-/- >`

#### Primitive 1: `< 1Ci; 1W0D/1D/-/- >`

| # | Group | A0.D | A0.C | A1.D | A1.C | A2.D | A2.C | A3.D | A3.C | A4.D | A4.C | Detector | Position | Order | ops_before | has_set_Ci |
|---|-------|------|------|------|------|------|------|------|------|------|------|----------|----------|-------|------------|------------|
| 1 | A<V | X | **1** | X | X | 1 | X | X | X | X | X | Read(0) | # | None | **[W0]** | false |
| 2 | A<V | X | **1** | X | X | 1 | X | X | X | X | X | C{X,1,X} | # | None | **[W0]** | **false** |
| 3 | A>V | X | X | X | X | 1 | X | X | X | X | **1** | Read(0) | # | None | **[W0]** | false |
| 4 | A>V | X | X | X | X | 1 | X | X | X | X | **1** | C{X,1,X} | # | None | **[W0]** | **false** |

#### Primitive 2: `< 1Ci; 0W1D/0D/-/- >`

| # | Group | A0.D | A0.C | A1.D | A1.C | A2.D | A2.C | A3.D | A3.C | A4.D | A4.C | Detector | Position | Order | ops_before | has_set_Ci |
|---|-------|------|------|------|------|------|------|------|------|------|------|----------|----------|-------|------------|------------|
| 5 | A<V | X | **1** | X | X | 0 | X | X | X | X | X | Read(1) | # | None | **[W1]** | false |
| 6 | A<V | X | **1** | X | X | 0 | X | X | X | X | X | C{X,1,X} | # | None | **[W1]** | **false** |
| 7 | A>V | X | X | X | X | 0 | X | X | X | X | **1** | Read(1) | # | None | **[W1]** | false |
| 8 | A>V | X | X | X | X | 0 | X | X | X | X | **1** | C{X,1,X} | # | None | **[W1]** | **false** |

**關鍵觀察**：
- **Pivot = Victim**：因 Sa 僅有 Ci 設置，無操作
- **ops_before_detect 非空**：需先執行 Write 操作進行敏化
- **has_set_Ci = false**：
  - 雖然是 Cross-Row，但因 Sv 含有 Write 操作
  - `canComputeSetCi()` 返回 `false`
  - **偵測器無法透過 Compute 傳遞 Ci**
- **Ci 仍記錄在 cross_state**：A0.C=1 (A<V) 或 A4.C=1 (A>V)
- **與 CIDDB(1,0) 的關鍵差異**：
  - CIDDB(1,0): `has_set_Ci=true`，偵測器為 `C{1,1,X}` 或 `C{X,1,1}`
  - CIDWDB: `has_set_Ci=false`，偵測器固定為 `C{X,1,X}`

---

## 總結表格

| 論文概念 | 程式碼實現 | 關鍵函式/結構 |
|---------|-----------|---------------|
| 10→6 Space Reduction | 5-Slot Cross-Shape + 約束規則 | `CrossState`, `enforceDCrule()` |
| Pivot Rule | `s_has_any_op` 判斷 | `decidePivot()` |
| Aggressor Mapping (^, ;) | `expandPosVariants()` | `PositionMark::SameElementHead/NextElementHead` |
| Victim Mapping (#) | 預設單一變體 | `PositionMark::Adjacent` |
| C{1,1,X} for A<V | `C_T = sa_ci` | `setComputeTB()` |
| C{X,1,1} for A>V | `C_B = sa_ci` | `setComputeTB()` |
| Irreversibility Check | Ci 衝突檢測 | `mergeC()`, `canComputeSetCi()` |
| Group Coverage | OrientationGroup 分類 | `A_LT_V`, `A_GT_V`, `Single` |

### Fault 展開統計

| Fault | Primitives | Directions | Detectors | Positions | Total TPs |
|-------|------------|------------|-----------|-----------|-----------|
| CFid(↑,0) | 1 | 2 (A<V, A>V) | 2 (Read, Compute) | 4 (^^;;) | **16** |
| CIDDB(1,0) | 1 | 2 (A<V, A>V) | 2 (Read, Compute) | 1 (#) | **4** |
| CIDWDB | 2 | 2 (A<V, A>V) | 2 (Read, Compute) | 1 (#) | **8** |
