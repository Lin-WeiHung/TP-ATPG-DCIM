# FaultSimulationEvent 技術文件

## 1. 系統概述

`FaultSimulationEvent.cpp` 實現了一個基於事件驅動的 **March Test 故障模擬器**，用於評估 CIM (Computing-In-Memory) 陣列的故障覆蓋率。本系統採用三階段流水線模擬架構：**State → Sensitize → Detect**，並透過 LUT (Look-Up Table) 加速狀態匹配，達到高效能的故障模擬。

### 系統架構圖

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         FaultSimulationEvent 系統                        │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│   ┌──────────────┐     ┌──────────────────┐     ┌──────────────────┐   │
│   │ Faults.json  │────▶│ FaultsJsonParser │────▶│  FaultNormalizer │   │
│   └──────────────┘     └──────────────────┘     └────────┬─────────┘   │
│                                                          │              │
│                                                          ▼              │
│   ┌──────────────┐     ┌──────────────────┐     ┌──────────────────┐   │
│   │ MarchTest.json│───▶│MarchTestJsonParser│    │   TPGenerator    │   │
│   └──────────────┘     └────────┬─────────┘     └────────┬─────────┘   │
│                                 │                        │              │
│                                 ▼                        ▼              │
│                        ┌──────────────────┐     ┌──────────────────┐   │
│                        │MarchTestNormalizer│    │ TestPrimitive[]  │   │
│                        └────────┬─────────┘     └────────┬─────────┘   │
│                                 │                        │              │
│                                 ▼                        │              │
│                        ┌──────────────────┐              │              │
│                        │ OpTableBuilder   │              │              │
│                        └────────┬─────────┘              │              │
│                                 │                        │              │
│                                 ▼                        ▼              │
│                        ┌─────────────────────────────────┐              │
│                        │      FaultSimulatorEvent        │              │
│                        │  ┌────────────────────────────┐ │              │
│                        │  │   StateCoverEngine (LUT)   │ │              │
│                        │  ├────────────────────────────┤ │              │
│                        │  │       SensEngine           │ │              │
│                        │  ├────────────────────────────┤ │              │
│                        │  │      DetectEngine          │ │              │
│                        │  └────────────────────────────┘ │              │
│                        └────────────────┬───────────────┘              │
│                                         │                               │
│                                         ▼                               │
│                               ┌─────────────────┐                       │
│                               │ TPEventCenter   │                       │
│                               │ (Event Tracking)│                       │
│                               └─────────────────┘                       │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## 2. 核心資料結構

### 2.1 三值邏輯與操作類型

```cpp
enum class Val { Zero, One, X };  // 三值邏輯：0, 1, 不關心(don't care)
enum class OpKind { Write, Read, ComputeAnd };  // 操作種類
```

**Val** 表示 CIM 陣列中儲存的三值邏輯：
- `Zero` (0)：邏輯低電位
- `One` (1)：邏輯高電位  
- `X`：不關心或未知狀態

**OpKind** 定義了 March Test 支援的三種基本操作：
- `Write`：將值寫入記憶體單元
- `Read`：讀取記憶體單元的值並比較
- `ComputeAnd`：執行 CIM 的 AND 運算

### 2.2 操作結構 (Op)

```cpp
struct Op {
    OpKind kind;              // 操作類型
    Val value{Val::X};        // Write/Read 操作的值 (0/1)
    Val C_T{Val::X};          // Compute 頂部輸入
    Val C_M{Val::X};          // Compute 中間輸入 (傳遞 D fault)
    Val C_B{Val::X};          // Compute 底部輸入
};
```

對於 `Write`/`Read` 操作，使用 `value` 欄位表示操作值。
對於 `ComputeAnd` 操作，使用 `(C_T, C_M, C_B)` 三元組表示計算輸入，對應 Cross-shape 架構的上/中/下三個輸入。

### 2.3 Cross-State 五欄位架構

```cpp
struct DC { Val D{Val::X}; Val C{Val::X}; };  // 每個位置的 Data 與 Compute 值

struct CrossState {
    DC A0;      // 頂部 (Top)
    DC A1;      // 左側 (Left)  
    DC A2_CAS;  // 中心/受測單元 (Center/CAS - Cell Address Strobe)
    DC A3;      // 右側 (Right)
    DC A4;      // 底部 (Bottom)
};
```

**Cross-Shape 佈局示意圖：**

```
         A0 (Top)
           │
           ▼
    A1 ──▶ A2_CAS ◀── A3
  (Left)  (Center)   (Right)
           │
           ▼
         A4 (Bottom)
```

此結構模擬 CIM 陣列中一個 5-cell 的十字形區域，其中：
- `A2_CAS` 是受測單元 (Cell Under Test)
- `A0`, `A4` 是同列的上下鄰居 (用於跨列 CFds 故障偵測)
- `A1`, `A3` 是同行的左右鄰居 (用於同列 CFds 故障偵測)

### 2.4 故障原語表達式 (FPExpr)

```cpp
struct FPExpr {
    optional<SSpec> Sa;      // Aggressor 側規格 (two-cell 故障時存在)
    SSpec Sv;                // Victim 側規格
    FSpec F;                 // Faulty D 值
    RSpec R;                 // 期望讀取的 D 值
    CSpec C;                 // Compute 結果 Co
    bool s_has_any_op;       // 是否有任何操作序列
};
```

**五段式格式**：`< Sa / Sv / F / R / C >`

- **Sa (Side-a)**：Aggressor 單元的操作序列，用於激發故障
- **Sv (Side-v)**：Victim 單元的操作序列，用於被故障影響
- **F**：故障發生後的錯誤 D 值
- **R**：期望透過 Read 偵測到的值
- **C**：期望透過 Compute 偵測到的 Co 值

### 2.5 故障結構 (Fault)

```cpp
enum class CellScope { SingleCell, TwoCellRowAgnostic, TwoCellSameRow, TwoCellCrossRow };
enum class Category { EitherReadOrCompute, MustRead, MustCompute };

struct Fault {
    string fault_id;              // 故障識別碼 (如 "SA0", "CFds_01")
    Category category;            // 偵測方式類別
    CellScope cell_scope;         // 單元範圍
    vector<FPExpr> primitives;    // 故障原語列表
};
```

**CellScope 說明**：
| 值 | 說明 | 覆蓋率計算 |
|---|---|---|
| `SingleCell` | 單一單元故障 (如 SA0/SA1) | 100% 或 0% |
| `TwoCellRowAgnostic` | 雙單元故障，方向無關 | A<V 50% + A>V 50% |
| `TwoCellSameRow` | 雙單元故障，同行 | A<V 50% + A>V 50% |
| `TwoCellCrossRow` | 雙單元故障，跨列 | A<V 50% + A>V 50% |

**Category 說明**：
| 值 | 說明 |
|---|---|
| `EitherReadOrCompute` | 可用 Read 或 Compute 偵測 |
| `MustRead` | 必須使用 Read 偵測 |
| `MustCompute` | 必須使用 Compute 偵測 |

---

## 3. 測試原語生成 (Test Primitive Generation)

### 3.1 測試原語結構 (TestPrimitive)

```cpp
struct TestPrimitive {
    string parent_fault_id;           // 所屬故障 ID
    size_t parent_fp_index;           // 對應的原語索引
    OrientationGroup group;           // 方向群組 (Single/A_LT_V/A_GT_V)
    CrossState state;                 // 必要的初始狀態
    vector<Op> ops_before_detect;     // 偵測前的致敏操作序列
    Detector detector;                // 偵測器規格
    bool F_has_value;                 // F 欄位是否有具體值
    bool R_has_value;                 // R 欄位是否有具體值
    bool C_has_value;                 // C 欄位是否有具體值
};
```

### 3.2 方向群組 (OrientationGroup)

```cpp
enum class OrientationGroup { Single, A_LT_V, A_GT_V };
```

- `Single`：單一單元故障，無方向性
- `A_LT_V`：Aggressor 地址 < Victim 地址
- `A_GT_V`：Aggressor 地址 > Victim 地址

對於 two-cell 故障，覆蓋率需要同時測試兩個方向才能達到 100%。

### 3.3 偵測器結構 (Detector)

```cpp
enum class PositionMark { Adjacent, SameElementHead, NextElementHead };

struct Detector {
    Op detectOp;                    // 偵測操作 (Read 或 ComputeAnd)
    PositionMark pos;               // 位置標記
    bool has_set_Ci;                // 是否需要設定 Ci
    AddrOrder order;                // 地址走訪順序
};
```

**PositionMark 說明**：
| 符號 | 值 | 說明 |
|---|---|---|
| `#` | `Adjacent` | 緊鄰在致敏操作後 |
| `^` | `SameElementHead` | 回到同 Element 的開頭 |
| `;` | `NextElementHead` | 跳到下一 Element 的開頭 |

### 3.4 TPGenerator 生成流程

```cpp
class TPGenerator {
    vector<TestPrimitive> generate(const Fault& fault);
private:
    OrientationSelector orientation_selector_;  // 決定方向組合
    DetectorPlanner detector_planner_;          // 規劃偵測策略
    StateAssembler state_assembler_;            // 組裝初始狀態
};
```

**生成流程**：
1. **OrientationSelector**：根據 `CellScope` 決定需要測試的方向組合
2. **DetectorPlanner**：根據 `Category` 和 `FPExpr` 規劃偵測策略
3. **StateAssembler**：組裝每個 TP 所需的 `CrossState` 初始條件

---

## 4. March Test 解析與操作表建構

### 4.1 March Test 結構

```cpp
enum class AddrOrder { Up, Down, Any };  // 地址走訪順序

struct MarchElement {
    AddrOrder order;   // 走訪方向
    vector<Op> ops;    // 操作序列
};

struct MarchTest {
    string name;
    vector<MarchElement> elements;
};
```

**March Test 語法範例**：
```
⇑(W0);⇓(R0,W1);⇑(R1,W0,R0);⇓(R0)
```
- `⇑` / `a`：地址遞增 (Up)
- `⇓` / `d`：地址遞減 (Down)  
- `⇕` / `b`：任意方向 (Any/Both)

### 4.2 操作上下文 (OpContext)

```cpp
struct OpContext {
    Op op;                     // 操作內容
    int elem_index;            // 所屬 Element 索引
    int index_within_elem;     // Element 內的位置
    AddrOrder order;           // 走訪方向
    
    CrossState pre_state;      // 此操作執行前的狀態
    size_t pre_state_key;      // 狀態的雜湊鍵值 (0..728)
    
    OpId next_op_index;        // 同 Element 下一操作 (#)
    OpId head_same;            // 同 Element 開頭 (^)
    OpId head_next;            // 下一 Element 開頭 (;)
};
```

### 4.3 OpTableBuilder 建構流程

```cpp
class OpTableBuilder {
    vector<OpContext> build(const MarchTest& mt);
private:
    void flatten(const MarchTest& mt, vector<OpContext>& opt);       // 1) 平展操作
    void build_neighbors(const MarchTest& mt, vector<OpContext>& opt); // 2) 建立鄰接關係
    void build_D2_sentinels(const MarchTest& mt);                     // 3) D2 哨兵
    void build_C_sentinels(const MarchTest& mt);                      // 4) C 哨兵
    void derive_pre_state_in_same_row(vector<OpContext>& opt);        // 5) 推導 pre_state
};
```

**狀態推導規則**：

1. **D2 (中心單元 D 值)**：
   - Element 開頭 = 上一 Element 結束時的 D2
   - 遇 Write 操作後更新為寫入值

2. **D1/D3 (左右鄰居)**：
   - `Up` 方向：D1 = 本 Element 哨兵 D2
   - `Down` 方向：D3 = 本 Element 哨兵 D2
   - 其他方向：使用上一 Element 的 D2

3. **C0/C2/C4 (Compute 值)**：
   - 由前一個 ComputeAnd 操作的 (C_T, C_M, C_B) 決定
   - 根據走訪方向調整對應位置

---

## 5. 狀態覆蓋引擎 (State Cover Engine)

### 5.1 729-LUT 加速機制

系統使用 $3^6 = 729$ 大小的查找表進行狀態匹配加速。

**編碼函式**：
```cpp
inline size_t encode_to_key(const CrossState& input) {
    size_t key = 0;
    auto valto3 = [](Val v){ return (v==Val::Zero)?0u : (v==Val::One)?1u : 2u; };
    key = key * 3 + valto3(input.A1.D);      // D1
    key = key * 3 + valto3(input.A2_CAS.D);  // D2
    key = key * 3 + valto3(input.A3.D);      // D3
    key = key * 3 + valto3(input.A0.C);      // C0
    key = key * 3 + valto3(input.A2_CAS.C);  // C2
    key = key * 3 + valto3(input.A4.C);      // C4
    return key;  // 0..728
};
```

**6 個關鍵欄位**：
| 位元 | 欄位 | 說明 |
|---|---|---|
| 0 | D1 (A1.D) | 左側鄰居 Data |
| 1 | D2 (A2_CAS.D) | 中心單元 Data |
| 2 | D3 (A3.D) | 右側鄰居 Data |
| 3 | C0 (A0.C) | 頂部 Compute |
| 4 | C2 (A2_CAS.C) | 中心 Compute |
| 5 | C4 (A4.C) | 底部 Compute |

### 5.2 CoverLUT 預計算

```cpp
class CoverLUT {
    vector<size_t> compatible_tp_keys[729];  // 預計算的相容 TP 鍵值
public:
    CoverLUT();  // 建構時預計算 729×729 相容矩陣
    const vector<size_t>& get_compatible_tp_keys(const CrossState& op_css) const;
};
```

**相容性判斷規則**：
- TP 的狀態要求中，若某欄位為 `X` (don't care)，則可匹配任何值
- 若為 `0` 或 `1`，則必須完全匹配

### 5.3 StateCoverEngine

```cpp
class StateCoverEngine {
    CoverLUT lut;
    array<vector<TpGid>, 729> tp_buckets;  // TP 按狀態鍵值分桶
public:
    void build_tp_buckets(const vector<TestPrimitive>& tps);  // 建立 TP 桶
    vector<TpGid> cover(size_t op_css_key);                   // 查詢匹配的 TP
};
```

**複雜度分析**：
- 預處理：$O(729^2)$ 一次性建表
- 每次查詢：$O(k)$，其中 $k$ 是匹配的 TP 數量

---

## 6. 致敏引擎 (Sensitization Engine)

### 6.1 SensEngine

```cpp
struct SensOutcome {
    enum class Status { SensAll, SensPartial, SensNone, DontNeedSens };
    Status status;
    int sens_end_op;     // 致敏完成的操作索引
    int sens_mask_at_op; // 致敏被遮蔽的操作索引
};

class SensEngine {
    SensOutcome cover(const vector<OpContext>& opt, OpId opt_begin, 
                      const TestPrimitive& tp) const;
private:
    bool op_match(const OpContext& march_op, const Op& tp_op) const;
};
```

### 6.2 致敏匹配規則

致敏階段驗證 March Test 的操作序列是否能滿足 TP 的 `ops_before_detect` 要求：

1. **操作類型匹配**：`march_op.kind == tp_op.kind`
2. **值匹配**（Read/Write）：`tp_op.value == X` 或 `tp_op.value == march_op.value`
3. **Compute 匹配**：每個 `C_T`, `C_M`, `C_B` 需獨立匹配（X 可通配）

### 6.3 致敏結果

| Status | 說明 |
|---|---|
| `SensAll` | 所有致敏操作成功匹配 |
| `SensPartial` | 部分匹配後被遮蔽 |
| `SensNone` | 完全無法匹配 |
| `DontNeedSens` | TP 不需要致敏（靜態故障） |

---

## 7. 偵測引擎 (Detection Engine)

### 7.1 DetectEngine

```cpp
struct DetectOutcome {
    enum class Status { Found, MaskedOnD, NoDetectorReachable };
    Status status;
    int det_op;      // 偵測成功的操作索引
    int mask_at_op;  // 被遮蔽的操作索引
};

class DetectEngine {
    DetectOutcome cover(const vector<OpContext>& opt, OpId sens_end_id, 
                        const TestPrimitive& tp) const;
protected:
    bool detect_match(const OpContext& op, const Detector& dec) const;
    bool is_masking_on_D(const OpContext& op) const;
};
```

### 7.2 偵測定位規則

根據 `Detector.pos` 決定從哪個操作開始尋找偵測點：

1. **Adjacent (#)**：
   - 無致敏序列：與 state 同一操作
   - 有致敏序列：致敏結束後的下一操作

2. **SameElementHead (^)**：回到同 Element 開頭

3. **NextElementHead (;)**：跳到下一 Element 開頭

### 7.3 偵測掃描邏輯

若 TP 的 `F_has_value == true`（故障產生具體的錯誤值）：
1. 從錨點開始向後掃描
2. 遇到 Write 操作 → 故障效應被洗掉 → `MaskedOnD`
3. 找到匹配的偵測操作 → `Found`

---

## 8. 事件追蹤系統 (Event Tracking)

### 8.1 TPEvent 事件狀態機

```cpp
class TPEvent {
public:
    enum class Status { 
        Stated,       // 狀態匹配成功
        Sensitized,   // 致敏完成
        Detected,     // 偵測成功
        StateMasked,  // 狀態階段被遮蔽
        SensMasked,   // 致敏階段被遮蔽
        DetectMasked  // 偵測階段被遮蔽
    };
private:
    TpGid tp_gid_;                  // 對應的 TP ID
    EventId id_;                    // 事件 ID
    OpId state_op_;                 // 狀態匹配的操作
    vector<OpId> sens_ops_;         // 致敏完成的操作列表
    OpId det_op_;                   // 偵測成功的操作
    OpId mask_op_;                  // 遮蔽發生的操作
    Status final_status_;           // 最終狀態
};
```

**狀態轉移圖**：

```
                    ┌───────────────┐
                    │    Stated     │
                    └───────┬───────┘
                            │
            ┌───────────────┼───────────────┐
            ▼               │               ▼
    ┌──────────────┐        │       ┌──────────────┐
    │ StateMasked  │        │       │              │
    └──────────────┘        ▼       │              │
                    ┌───────────────┐              │
                    │  Sensitized   │              │
                    └───────┬───────┘              │
                            │                      │
            ┌───────────────┼───────────────┐      │
            ▼               │               ▼      │
    ┌──────────────┐        │       ┌──────────────┤
    │  SensMasked  │        │       │              │
    └──────────────┘        ▼       │              │
                    ┌───────────────┐              │
                    │   Detected    │◀─────────────┘
                    └───────────────┘      (R_has_value)
                            │
                            ▼
                    ┌───────────────┐
                    │ DetectMasked  │
                    └───────────────┘
```

### 8.2 TPEventCenter 事件中心

```cpp
class TPEventCenter {
    vector<TPEvent> events_;                     // 所有事件
    vector<vector<EventId>> state_begins_;       // 每操作的狀態事件
    vector<vector<EventId>> sens_done_;          // 每操作的致敏完成事件
    vector<vector<EventId>> detect_done_;        // 每操作的偵測完成事件
    vector<vector<EventId>> sens_masked_;        // 每操作的致敏遮蔽事件
    vector<vector<EventId>> detect_masked_;      // 每操作的偵測遮蔽事件
    vector<vector<EventId>> tp2events_;          // 每 TP 的事件列表
public:
    void init(size_t op_count, size_t tp_count);
    EventId start_state(TpGid tp, OpId op);
    void add_sens_complete(EventId id, OpId op);
    void mask_sens(EventId id, OpId op);
    void set_detect(EventId id, OpId op);
    void mask_detect(EventId id, OpId op);
};
```

---

## 9. 覆蓋群組管理 (Group Index)

### 9.1 GroupIndex

```cpp
struct GroupKey {
    string fault_id;
    OrientationGroup og;
};

class GroupIndex {
    vector<int> tp2group_;           // TP → 群組映射
    vector<bool> group_covered_;     // 群組是否已覆蓋
    vector<GroupKey> group_meta_;    // 群組元資料
    vector<size_t> group_sizes_;     // 每群組的 TP 數量
public:
    void build(const vector<TestPrimitive>& tps);
    int group_of_tp(size_t tp_gid) const;
    bool mark_covered_if_new(int tp_gid);
    size_t uncovered_groups() const;
};
```

**群組機制說明**：

同一故障的同方向 TP 歸為一個群組。只要群組中任一 TP 被偵測到，整個群組視為已覆蓋。

| Fault ID | Orientation | Group ID |
|---|---|---|
| SA0 | Single | 0 |
| CFds_01 | A_LT_V | 1 |
| CFds_01 | A_GT_V | 2 |
| SA1 | Single | 3 |

---

## 10. FaultSimulatorEvent 主流程

```cpp
class FaultSimulatorEvent {
public:
    SimulationEventResult simulate(
        const MarchTest& mt,
        const vector<Fault>& faults,
        const vector<TestPrimitive>& tps
    );
private:
    OpTableBuilder op_table_builder_;
    StateCoverEngine state_cover_engine_;
    SensEngine sens_engine_;
    DetectEngine detect_engine_;
};
```

### 10.1 模擬演算法

```
輸入: MarchTest mt, Fault[] faults, TestPrimitive[] tps
輸出: SimulationEventResult

1. 建立操作表
   op_table = OpTableBuilder.build(mt)
   
2. 建立 TP 狀態桶
   StateCoverEngine.build_tp_buckets(tps)
   
3. 初始化事件中心
   events.init(op_table.size(), tps.size())
   
4. 對每個操作 op_id ∈ [0, op_table.size()):
   
   4.1 State Cover (狀態覆蓋)
       state_tps = StateCoverEngine.cover(op_table[op_id].pre_state_key)
       
   4.2 對每個匹配的 tp_gid ∈ state_tps:
       
       4.2.1 建立事件
             evt = events.start_state(tp_gid, op_id)
             
       4.2.2 Sensitization (致敏)
             sens_res = SensEngine.cover(op_table, op_id, tps[tp_gid])
             
             if sens_res.status == SensNone:
                 continue  // 無法致敏
                 
             if sens_res.status == SensPartial:
                 events.mask_sens(evt, sens_res.sens_mask_at_op)
                 continue  // 致敏被遮蔽
                 
             events.add_sens_complete(evt, sens_res.sens_end_op)
             
       4.2.3 Detection (偵測)
             det_res = DetectEngine.cover(op_table, sens_res.sens_end_op, tps[tp_gid])
             
             if det_res.status == Found:
                 events.set_detect(evt, det_res.det_op)
                 GroupIndex.mark_covered(tp_gid)
                 
             else if det_res.status == MaskedOnD:
                 events.mask_detect(evt, det_res.mask_at_op)

5. 返回結果
   return SimulationEventResult{op_table, events, tp_group}
```

### 10.2 複雜度分析

設：
- $N_{op}$ = 操作數量
- $N_{tp}$ = TP 數量
- $k$ = 每操作平均匹配的 TP 數量
- $L_{sens}$ = 平均致敏序列長度

| 階段 | 複雜度 |
|---|---|
| OpTable 建構 | $O(N_{op})$ |
| TP 分桶 | $O(N_{tp})$ |
| State Cover (所有操作) | $O(N_{op} \cdot k)$ |
| Sensitization | $O(L_{sens})$ per match |
| Detection | $O(N_{op})$ worst case |
| **總體** | $O(N_{op} \cdot k \cdot L_{sens})$ |

---

## 11. 覆蓋率計算

### 11.1 單一故障覆蓋率

```cpp
double compute_fault_coverage(const Fault& fault, 
                              const set<TpGid>& detected_tps) {
    if (fault.cell_scope == CellScope::SingleCell) {
        // 單元故障：任一 TP 被偵測即 100%
        for (tp_gid in detected_tps) {
            if (tps[tp_gid].parent_fault_id == fault.fault_id)
                return 1.0;
        }
        return 0.0;
    } else {
        // 雙元故障：兩個方向各 50%
        bool has_lt = false, has_gt = false;
        for (tp_gid in detected_tps) {
            if (tps[tp_gid].parent_fault_id != fault.fault_id) continue;
            if (tps[tp_gid].group == A_LT_V) has_lt = true;
            if (tps[tp_gid].group == A_GT_V) has_gt = true;
        }
        return (has_lt ? 0.5 : 0.0) + (has_gt ? 0.5 : 0.0);
    }
}
```

### 11.2 總體覆蓋率

$$\text{Total Coverage} = \frac{\sum_{f \in \text{Faults}} \text{Coverage}(f)}{|\text{Faults}|}$$

---

## 12. 主程式執行流程

```cpp
int main(int argc, char** argv) {
    // 1. 解析命令列參數
    //    ./FaultSimulationEvent <faults.json> <MarchTest.json> <output.html>
    
    // 2. 載入並解析故障列表
    FaultsJsonParser fparser;
    FaultNormalizer fnorm;
    auto raw_faults = fparser.parse_file(faults_json);
    vector<Fault> faults;
    for (auto& rf : raw_faults)
        faults.push_back(fnorm.normalize(rf));
    
    // 3. 生成測試原語
    TPGenerator tpg;
    vector<TestPrimitive> all_tps;
    for (auto& f : faults) {
        auto tps = tpg.generate(f);
        all_tps.insert(all_tps.end(), tps.begin(), tps.end());
    }
    
    // 4. 載入並解析 March Test
    MarchTestJsonParser mparser;
    MarchTestNormalizer mnorm;
    auto raw_mts = mparser.parse_file(march_json);
    vector<MarchTest> marchTests;
    for (auto& r : raw_mts)
        marchTests.push_back(mnorm.normalize(r));
    
    // 5. 對每個 March Test 執行模擬
    FaultSimulatorEvent simulator;
    for (auto& mt : marchTests) {
        auto sim = simulator.simulate(mt, faults, all_tps);
        // 計算覆蓋率並輸出結果
    }
    
    // 6. 產生 HTML 報告
}
```

---

## 13. 輸入輸出格式

### 13.1 Faults JSON 格式

```json
[
  {
    "fault_id": "SA0",
    "category": "either_read_or_compute",
    "cell_scope": "single cell",
    "fault_primitives": ["<0D/0D/-/->"]
  },
  {
    "fault_id": "CFds_01",
    "category": "must_read",
    "cell_scope": "two-cell same-row",
    "fault_primitives": ["<1W0D/1D/0D/1D/->"]
  }
]
```

### 13.2 MarchTest JSON 格式

```json
[
  {
    "March_test": "March C-",
    "Pattern": "b(W0);a(R0,W1);a(R1,W0);d(R0,W1);d(R1,W0);b(R0)"
  },
  {
    "March_test": "MATS+",
    "Pattern": "b(W0);a(R0,W1);d(R1,W0)"
  }
]
```

---

## 14. 關鍵設計決策

### 14.1 為何使用 729-LUT？

CIM Cross-shape 架構需要追蹤 6 個關鍵狀態位元，每個位元有 3 種可能值 (0/1/X)，因此狀態空間為 $3^6 = 729$。預計算所有狀態組合的相容關係可將每次狀態匹配從 $O(N_{tp})$ 降低到 $O(k)$。

### 14.2 事件驅動 vs 批次模擬

事件驅動架構允許：
- 追蹤每個 TP 的完整生命週期
- 精確定位故障遮蔽發生的時機
- 支援增量式結果分析
- 便於除錯和視覺化

### 14.3 群組覆蓋機制

同一故障的同方向 TP 形成一個等價群組。只要群組內任一 TP 被偵測，整個群組視為覆蓋。這反映了實際測試中的等價性：相同故障的相同方向 TP 具有相同的偵測能力。

---

## 15. 效能考量

| 優化策略 | 說明 |
|---|---|
| 729-LUT 預計算 | 一次性建表，後續 $O(1)$ 查表 |
| TP 分桶 | 避免遍歷所有 TP |
| 鄰接索引預建 | `head_same`, `head_next` 避免重複計算 |
| 狀態鍵值快取 | `pre_state_key` 避免重複編碼 |
| 群組覆蓋標記 | 早期終止已覆蓋群組的處理 |

---

## 16. 擴展點

1. **新增 Compute 類型**：在 `OpKind` 加入 `ComputeOr`, `ComputeXor` 等
2. **多位元故障**：擴展 `CrossState` 支援更大的鄰域
3. **並行模擬**：對不同 March Test 進行平行處理
4. **增量更新**：支援 March Test 修改後的差異化重模擬

---

## 附錄 A：類別參照表

| 類別 | 檔案 | 功能 |
|---|---|---|
| `FaultsJsonParser` | FpParserAndTpGen.hpp | 解析故障 JSON |
| `FaultNormalizer` | FpParserAndTpGen.hpp | 正規化故障資料 |
| `TPGenerator` | FpParserAndTpGen.hpp | 生成測試原語 |
| `MarchTestJsonParser` | FaultSimulator.hpp | 解析 March Test JSON |
| `MarchTestNormalizer` | FaultSimulator.hpp | 正規化 March Test |
| `OpTableBuilder` | FaultSimulator.hpp | 建構操作表 |
| `CoverLUT` | FaultSimulator.hpp | 狀態相容性查找表 |
| `StateCoverEngine` | FaultSimulator.hpp | 狀態覆蓋引擎 |
| `SensEngine` | FaultSimulator.hpp | 致敏引擎 |
| `DetectEngine` | FaultSimulator.hpp | 偵測引擎 |
| `TPEvent` | FaultSimulator.hpp | 事件狀態追蹤 |
| `TPEventCenter` | FaultSimulator.hpp | 事件中心管理 |
| `GroupIndex` | FaultSimulator.hpp | 群組覆蓋管理 |
| `FaultSimulatorEvent` | FaultSimulator.hpp | 事件驅動模擬器 |

---

## 附錄 B：術語表

| 術語 | 說明 |
|---|---|
| TP (Test Primitive) | 測試原語，描述偵測特定故障所需的條件 |
| FP (Fault Primitive) | 故障原語，描述故障的激發與觀測條件 |
| CAS (Cell Address Strobe) | 受測單元，Cross-shape 的中心 |
| Aggressor | 觸發故障的單元 |
| Victim | 受故障影響的單元 |
| Sensitization | 致敏，激發故障效應的過程 |
| Detection | 偵測，觀測故障效應的過程 |
| Masking | 遮蔽，故障效應被後續操作覆蓋 |
| March Element | March Test 的基本單位，包含方向和操作序列 |


---

*文件版本：1.0*  
*最後更新：2025-01-22*
