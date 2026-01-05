# OpTableBuilder 類別文件

## 概述

`OpTableBuilder` 負責將 March Test 結構展開為一張操作表（Op Table），並為每個操作推導其執行前的 Cross-State（CSS）。這張表是 Fault Simulator 進行 State Cover、Sensitization、Detection 三階段模擬的核心資料結構。

---

## 類別結構

```cpp
class OpTableBuilder {
public:
    vector<OpContext> build(const MarchTest& mt);
private:
    vector<Val> D2_sentinel;           // 每個 element 結束時的 D2 值
    vector<array<Val,3>> C_sentinel;   // 每個 element 的 (C0, C2, C4) 哨兵
    vector<AddrOrder> elem_orders;     // 快取各 element 的走訪順序

    void flatten(const MarchTest& mt, vector<OpContext>& opt) const;
    void build_neighbors(const MarchTest& mt, vector<OpContext>& opt) const;
    void build_D2_sentinels(const MarchTest& mt);
    void build_C_sentinels(const MarchTest& mt);
    void derive_pre_state_in_same_row(vector<OpContext>& opt) const;
};
```

---

## 核心資料結構：OpContext

```cpp
struct OpContext {
    Op op;                    // 操作本身 (Read/Write/ComputeAnd)
    int elem_index;           // 所屬 MarchElement 索引
    int index_within_elem;    // 在該 element 內的操作索引
    AddrOrder order;          // 該 element 的地址走訪順序 (Up/Down/Any)

    CrossState pre_state;     // 此操作執行「前」的 Cross-State
    size_t pre_state_key;     // pre_state 編碼後的 key (0..728)

    OpId next_op_index;       // 同 element 內下一個操作的索引，-1 表示無
    OpId head_same;           // 同 element 第一個操作的索引
    OpId head_next;           // 下一個 element 第一個操作的索引，-1 表示無
};
```

---

## 建構流程：`build()` 方法

```cpp
vector<OpContext> OpTableBuilder::build(const MarchTest& mt) {
    vector<OpContext> opt;
    // 1) 平展所有 element 的操作
    flatten(mt, opt);
    // 2) 建立鄰接指標 (next_op_index, head_same, head_next)
    build_neighbors(mt, opt);
    // 3) 計算 D2 哨兵（每個 element 結束時 D2 的值）
    build_D2_sentinels(mt);
    // 4) 計算 C 哨兵（每個 element 結束時 Compute 輸入的值）
    build_C_sentinels(mt);
    // 5) 快取 element 順序
    elem_orders.clear();
    for(const auto& e: mt.elements) elem_orders.push_back(e.order);
    // 6) 推導每個操作的 pre_state
    derive_pre_state_in_same_row(opt);
    return opt;
}
```

---

## 各步驟詳細邏輯

### 1. `flatten()` — 平展操作序列

**功能**：將 MarchTest 中所有 MarchElement 的操作展開成一維陣列。

**邏輯**：
```
for each element[i] in MarchTest:
    for each op[j] in element[i].ops:
        建立 OpContext:
            - elem_index = i
            - index_within_elem = j
            - op = element[i].ops[j]
            - order = element[i].order
        加入 opt 陣列
```

**輸出**：`opt` 陣列包含所有操作，按 element 順序排列。

---

### 2. `build_neighbors()` — 建立鄰接指標

**功能**：為每個操作建立三種跳轉指標，供後續 Sensitization 和 Detection 階段使用。

**三種指標**：
| 指標 | 符號 | 意義 |
|------|------|------|
| `next_op_index` | `#` | 同 element 內的下一個操作 |
| `head_same` | `^` | 同 element 的第一個操作 |
| `head_next` | `;` | 下一個 element 的第一個操作 |

**邏輯**：
```
current_id = 0
for each element[i] in MarchTest:
    if element[i].ops is empty: continue
    
    this_head_id = current_id
    next_head_id = current_id + len(element[i].ops)
    if next_head_id >= total_ops: next_head_id = -1
    
    for j = 0 to len(element[i].ops) - 1:
        opt[current_id].next_op_index = (j+1 < len) ? current_id+1 : -1
        opt[current_id].head_same = this_head_id
        opt[current_id].head_next = next_head_id
        current_id++
```

---

### 3. `build_D2_sentinels()` — 建立 D2 哨兵陣列

**功能**：計算每個 element 結束時，目標 cell 的 D 面值（D2）。

**概念**：D2 哨兵記錄的是「當走訪完整個 element 後，每個 cell 的 D 面被寫成什麼值」。

**邏輯**：
```
D2_sentinel[0] = X  // 初始未知

for i = 0 to num_elements - 1:
    D2_sentinel[i] = D2_sentinel[i]  // 繼承前值（若有）
    
    for each op in element[i].ops:
        if op is Write(value):
            D2_sentinel[i] = value  // 追蹤最後寫入值
    
    D2_sentinel[i+1] = D2_sentinel[i]  // 傳遞給下一 element
```

**重點**：只追蹤 Write 操作，因為只有 Write 會改變 D 面的值。

---

### 4. `build_C_sentinels()` — 建立 C 哨兵陣列

**功能**：計算每個 element 結束時，Compute 操作的輸入狀態 (C0, C2, C4)。

**邏輯**：
```
C_sentinel[0] = (X, X, X)  // 初始未知

for i = 0 to num_elements - 1:
    // 從前一 element 繼承 C2 值作為初始
    C0 = (i > 0) ? C_sentinel[i-1][1] : X
    C2 = C0
    C4 = C0
    
    for each op in element[i].ops:
        if op is ComputeAnd(T, M, B):
            C0 = T    // Top
            C2 = M    // Middle (自己)
            C4 = B    // Bottom
    
    C_sentinel[i] = (C0, C2, C4)
```

**重點**：
- 繼承規則：下一 element 的初始 C 值等於上一 element 的 C2（中間值）
- 只有 ComputeAnd 操作會更新 C 值

---

### 5. `derive_pre_state_in_same_row()` — 推導 Pre-State

**功能**：這是最核心的邏輯，為每個操作計算其執行前的 Cross-State (CSS)。

#### Cross-State 結構
```
CrossState = {
    A1.D  (D1) = 左鄰居的 D 面
    A2.D  (D2) = 目標 cell 的 D 面
    A3.D  (D3) = 右鄰居的 D 面
    A0.C  (C0) = 上鄰居的 C 面
    A2.C  (C2) = 目標 cell 的 C 面
    A4.C  (C4) = 下鄰居的 C 面
}
```

#### Wavefront（波前）概念

在 March Test 中，地址按特定順序走訪（升序或降序）。這形成一個「波前」：
- **升序 (Up)**：從低地址往高地址走，左邊是已處理的 cell，右邊是未處理的 cell
- **降序 (Down)**：從高地址往低地址走，左邊是未處理的 cell，右邊是已處理的 cell

#### D1/D3 的推導邏輯

```
baseD2_up   = D2_sentinel[elem]       // 本 element 結束時的 D2
baseD2_prev = D2_sentinel[elem - 1]   // 上一 element 結束時的 D2（若無則為 X）

if order == Up or order == Any:
    D1_init = baseD2_up     // 左鄰居已被處理，使用本 element 的最終值
else:
    D1_init = baseD2_prev   // 左鄰居尚未處理，使用上一 element 的值

if order == Down:
    D3_init = baseD2_up     // 右鄰居已被處理，使用本 element 的最終值
else:
    D3_init = baseD2_prev   // 右鄰居尚未處理，使用上一 element 的值
```

#### C0/C2/C4 的推導邏輯

C 面的值也需根據走訪順序進行調整：

```
cSent = C_sentinel[elem]  // 本 element 的 (C0, C2, C4)

if order == Down:
    // 降序：上鄰居已處理，下鄰居未處理
    C0 = cSent[0]  // Top
    C2 = cSent[2]  // Bottom -> 自己
    C4 = cSent[1]  // Middle -> 下鄰居
else:
    // 升序/Any：上鄰居未處理，下鄰居已處理
    C0 = cSent[1]  // Middle -> 上鄰居
    C2 = cSent[0]  // Top -> 自己
    C4 = cSent[2]  // Bottom
```

#### Element 內部遍歷

```
for each element:
    // 初始化本 element 的 D1, D3, D2, C0, C2, C4
    curD2 = baseD2_prev  // 目標 cell 的 D 面從上一 element 繼承
    
    for each op in element:
        // 1) 填入 pre_state
        pre_state.D1 = D1_init   // 固定（element 內不變）
        pre_state.D2 = curD2     // 動態更新
        pre_state.D3 = D3_init   // 固定（element 內不變）
        pre_state.C0 = c0        // 動態更新
        pre_state.C2 = c2        // 動態更新
        pre_state.C4 = c4        // 動態更新
        
        // 2) 計算 pre_state_key 用於 LUT 查詢
        pre_state_key = encode_to_key(pre_state)
        
        // 3) 更新狀態供下一操作使用
        if op is Write(value):
            curD2 = value
        else if op is ComputeAnd(T, M, B):
            c0 = T
            c2 = M
            c4 = B
```

---

## CSS Pre-State 決定規則

### 核心概念：Wavefront（波前）

當 March Test 以特定順序走訪地址時，會形成一個「波前」：
- **升序 (Up/Any)**：從低地址往高地址走 → 左鄰居**已處理**，右鄰居**未處理**
- **降序 (Down)**：從高地址往低地址走 → 左鄰居**未處理**，右鄰居**已處理**

### 各欄位決定規則

#### Dcenter（目標 cell 的 D 面）
- **Element 開頭**：繼承「上一個 element 結束時」的值（第一個 element = X）
- **Element 內部**：每遇到 `Write(d)` 就更新為 `d`

#### Dleft（左鄰居的 D 面）— Element 內固定
| 走訪順序 | 左鄰居狀態 | 使用的值 |
|---------|-----------|---------|
| Up / Any | 已處理 | 本 element 結束時的 D 值 |
| Down | 未處理 | 上一 element 結束時的 D 值 |

#### Dright（右鄰居的 D 面）— Element 內固定
| 走訪順序 | 右鄰居狀態 | 使用的值 |
|---------|-----------|---------|
| Up / Any | 未處理 | 上一 element 結束時的 D 值 |
| Down | 已處理 | 本 element 結束時的 D 值 |

#### C triplet（C0, C2, C4）
- **Element 開頭**：繼承上一 element 的 C 值，並根據 order 重映射位置
- **Element 內部**：每遇到 `Compute(T, M, B)` 就更新為 `(T, M, B)`

---

### 具體範例

```
a(W0, C(1)(1)(1)); a(W1, C(0)(1)(0), W0);
```

**Element 0**: `a(W0, C(1)(1)(1))`
- Order = Up
- 本 element 結束時 D = 0（最後的 Write 是 W0）
- 上一 element 結束時 D = X（沒有上一個）

| Op | Dcenter | Dleft | Dright | C triplet |
|----|---------|-------|--------|-----------|
| W0 | X（繼承自前 element）| 0（本 element 最終值）| X（前 element 值）| (X, X, X) |
| C(1)(1)(1) | 0（被 W0 更新）| 0 | X | (X, X, X)（此 op 執行後才變 (1,1,1)）|

**Element 1**: `a(W1, C(0)(1)(0), W0)`
- Order = Up
- 本 element 結束時 D = 0（最後的 Write 是 W0）
- 上一 element 結束時 D = 0（Element 0 的最終值）

| Op | Dcenter | Dleft | Dright | C triplet |
|----|---------|-------|--------|-----------|
| W1 | 0（繼承自 Element 0）| 0（本 element 最終值）| 0（前 element 值）| (1, 1, 1)（繼承自 Element 0）|
| C(0)(1)(0) | 1（被 W1 更新）| 0 | 0 | (1, 1, 1) |
| W0 | 1 | 0 | 0 | (0, 1, 0)（被上一個 Compute 更新）|

---

### Algorithm 0: CSS Pre-State Derivation (Corrected)

```
Input:  March test M = [e₀, ..., e_{L-1}], each element eₖ has order ∈ {Up, Down, Any}
Output: For each operation, its CSS pre-state = (Dleft, Dcenter, Dright, C0, C2, C4)

Define: finalD(k) = last Write value in element eₖ, propagated from previous if no Write
        finalC(k) = last Compute values in element eₖ, inherited from previous if no Compute

1:  for each element eₖ with order do
2:      // Initialize neighbor D values (fixed within element)
3:      if order ∈ {Up, Any} then Dleft ← finalD(k), Dright ← finalD(k-1)
4:      else                      Dleft ← finalD(k-1), Dright ← finalD(k)
5:      // Initialize center D and C triplet
6:      Dcenter ← finalD(k-1)
7:      (C0, C2, C4) ← remap finalC(k-1) based on order
8:      // Process each operation
9:      for each op in eₖ do
10:         Record CSS = (Dleft, Dcenter, Dright, C0, C2, C4)
11:         if op is Write(d) then Dcenter ← d
12:         if op is Compute(T,M,B) then (C0, C2, C4) ← (T, M, B)
13: return all CSS pre-states
```

---

## 原始 Pseudo Code 的錯誤分析

| 錯誤位置 | 原始描述 | 正確邏輯 |
|---------|---------|---------|
| Line 1-2 | 初始化 DLeft/DRight 為 Unknown | Dleft/Dright 由 order 與「已處理/未處理」邏輯決定 |
| Line 3-7 | 逐操作更新所有狀態 | Dleft/Dright 在 element 內是固定的，只有 Dcenter 和 C 需要逐操作更新 |
| Line 9-15 | 在 element boundary 更新鄰居 | 鄰居狀態應在 element 開始時根據 order 一次決定 |
| Line 10-15 | last_written_value / initial_value 模糊 | 應明確區分「本 element 最終值」與「上一 element 最終值」|
| 整體 | 沒有區分 C 面的重映射 | C triplet 需根據 order 進行位置調整 |

---

## 時間複雜度分析

| 步驟 | 複雜度 | 說明 |
|------|--------|------|
| flatten | O(N) | N = 總操作數 |
| build_neighbors | O(N) | 單次遍歷 |
| build_D2_sentinels | O(N) | 遍歷所有操作 |
| build_C_sentinels | O(N) | 遍歷所有操作 |
| derive_pre_state | O(N) | 遍歷所有操作 |
| **總計** | **O(N)** | 線性複雜度 |

---

## 使用範例

```cpp
MarchTestJsonParser parser;
MarchTestNormalizer normalizer;
OpTableBuilder builder;

auto raw_tests = parser.parse_file("input/MarchTest.json");
auto march_test = normalizer.normalize(raw_tests[0]);
auto op_table = builder.build(march_test);

// 使用 op_table 進行模擬
for (const auto& op_ctx : op_table) {
    size_t key = op_ctx.pre_state_key;
    // 利用 key 查詢 CoverLUT 獲取可覆蓋的 TP...
}
```

---

## 相關類別

- `MarchTestNormalizer`：將原始 March Test 字串解析為結構化格式
- `CoverLUT`：使用 pre_state_key 進行 729×729 的相容性查詢
- `StateCoverEngine`：利用 Op Table 進行 State Cover 階段
- `SensEngine` / `DetectEngine`：利用鄰接指標進行 Sensitization 和 Detection
