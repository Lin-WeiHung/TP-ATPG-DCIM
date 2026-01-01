# Greedy Template Search 技術文件

## 1. 系統概述

本文件描述基於模板的貪婪搜尋演算法，用於自動生成高覆蓋率的 March Test 序列。系統採用模板抽象層將「結構」與「值」分離，透過約束剪枝與評分函數引導搜尋方向。

### 系統架構

```
┌─────────────────────────────────────────────────────────────────────────┐
│                     Greedy Template Search 系統                          │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│   ┌──────────────┐     ┌──────────────────┐     ┌──────────────────┐   │
│   │TemplateLibrary│────▶│ValueExpandingGen │────▶│ MarchElement[]   │   │
│   │ (模板庫)      │     │ (展開生成器)     │     │ (候選元素)       │   │
│   └──────────────┘     └──────────────────┘     └────────┬─────────┘   │
│                                                          │              │
│                                                          ▼              │
│   ┌──────────────┐     ┌──────────────────┐     ┌──────────────────┐   │
│   │ ConstraintSet│────▶│   allow() 過濾   │────▶│ 有效候選元素     │   │
│   │ (約束集合)   │     │                  │     │                  │   │
│   └──────────────┘     └──────────────────┘     └────────┬─────────┘   │
│                                                          │              │
│                                                          ▼              │
│                        ┌─────────────────────────────────────────┐      │
│                        │          FaultSimulator (黑盒子)         │      │
│                        │  ┌─────────────────────────────────────┐│      │
│                        │  │ 輸入: MarchTest, Fault[], TP[]      ││      │
│                        │  │ 輸出: SimulationResult              ││      │
│                        │  │   - state_coverage                  ││      │
│                        │  │   - total_coverage                  ││      │
│                        │  │   - cover_lists (含 disrupted 資訊) ││      │
│                        │  └─────────────────────────────────────┘│      │
│                        └────────────────────┬────────────────────┘      │
│                                             │                           │
│                                             ▼                           │
│                        ┌──────────────────────────────────────┐         │
│                        │         ScoreFunc (評分函數)          │         │
│                        │  score = Δstate + Δtotal - ops - Δmask│         │
│                        └────────────────────┬─────────────────┘         │
│                                             │                           │
│                                             ▼                           │
│                        ┌──────────────────────────────────────┐         │
│                        │      Greedy Selection (貪婪選擇)      │         │
│                        │      選擇最高分的候選元素             │         │
│                        └──────────────────────────────────────┘         │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## 2. 核心資料結構

### 2.1 模板槽位與操作類型

```cpp
enum class TemplateOpKind { None, Read, Write, Compute };

struct TemplateSlot {
    TemplateOpKind kind{TemplateOpKind::None};
};
```

模板槽位定義了 March Element 中每個位置可能的操作類型：
- `None`：空槽位（無操作）
- `Read`：讀取操作
- `Write`：寫入操作
- `Compute`：計算操作（AND 運算）

### 2.2 元素模板 (ElementTemplate)

```cpp
class ElementTemplate {
    AddrOrder order;              // 地址走訪方向 (Up/Down/Any)
    vector<TemplateSlot> slots_;  // 槽位序列
    
    bool is_valid() const;        // 驗證模板有效性
    bool has_hole() const;        // 檢查是否有空洞 (None-Op-None)
    bool has_multiple_rw() const; // 檢查是否有重複 R/W
};
```

**模板有效性規則**：
1. **無空洞規則**：操作之間不可有 `None` 槽位（如 `W-None-R` 無效）
2. **單一 R/W 規則**：最多一個 Read 和一個 Write 操作

### 2.3 模板庫 (TemplateLibrary)

```cpp
class TemplateLibrary {
    vector<ElementTemplate> templates_;
public:
    static TemplateLibrary make_bruce(size_t slot_count);  // 暴力枚舉所有有效模板
    const ElementTemplate& at(TemplateId id) const;
    TemplateId size() const;
};
```

**暴力枚舉演算法**：
給定 $N$ 個槽位，共有 $4^N$ 種組合（每槽位 4 種類型）。對每種組合：
1. 分別產生 `Up` 和 `Down` 兩個方向
2. 驗證有效性（無空洞、無重複 R/W）
3. 有效則加入模板庫

| 槽位數 | 原始組合 | 過濾後模板數（約）|
|---|---|---|
| 3 | $2 \times 4^3 = 128$ | ~60 |
| 4 | $2 \times 4^4 = 512$ | ~200 |
| 5 | $2 \times 4^5 = 2048$ | ~600 |

---

## 3. 值展開生成器 (ValueExpandingGenerator)

將抽象模板展開為具體的 `MarchElement` 候選者。

```cpp
class ValueExpandingGenerator : public ICandidateGenerator {
    vector<MarchElement> generate(const TemplateLibrary& lib, 
                                  TemplateId tid) const override;
};
```

### 3.1 展開邏輯

- `Read`/`Write`：展開為 0 和 1 兩種值（1 bit）
- `Compute`：展開 C_T、C_M、C_B 三個參數（3 bits，共 8 種組合）

### 3.2 展開數量計算

設模板有 $r$ 個 Read、$w$ 個 Write、$c$ 個 Compute：

$$\text{展開數} = 2^r \times 2^w \times 8^c = 2^{r+w+3c}$$

### 3.3 展開範例

**模板** `[Write, Read, Compute]`：
- 總 bit 數 = $1 + 1 + 3 = 5$
- 展開數 = $2^5 = 32$ 種候選元素

```
mask = 00000 → W0, R0, C(0)(0)(0)
mask = 00001 → W1, R0, C(0)(0)(0)
mask = 00010 → W0, R1, C(0)(0)(0)
...
mask = 11111 → W1, R1, C(1)(1)(1)
```

### 3.4 展開演算法

```
輸入: ElementTemplate et (含 slots 與 order)
輸出: vector<MarchElement>

1. 計算每個槽位的 bit 位置
   cursor = 0
   對於每個 slot[i]:
       specs[i] = {kind: slot[i].kind, base: cursor}
       switch slot[i].kind:
           None:    cursor += 0
           Read:    cursor += 1
           Write:   cursor += 1
           Compute: cursor += 3

2. 總 bit 數 total_bits = cursor
   若 total_bits == 0:
       回傳 [空元素(只有order)]

3. 對於 mask ∈ [0, 2^total_bits):
   3.1 建立 MarchElement e，設定 e.order
   
   3.2 對於每個 specs[i]:
       switch specs[i].kind:
           None: 跳過
           Read:
               bit = (mask >> base) & 1
               e.ops.push_back(Op{Read, bit ? One : Zero})
           Write:
               bit = (mask >> base) & 1
               e.ops.push_back(Op{Write, bit ? One : Zero})
           Compute:
               bT = (mask >> base+0) & 1
               bM = (mask >> base+1) & 1
               bB = (mask >> base+2) & 1
               e.ops.push_back(Op{ComputeAnd, C_T=bT, C_M=bM, C_B=bB})
   
   3.3 out.push_back(e)

4. 回傳 out
```

---

## 4. 序列約束系統 (Sequence Constraints)

### 4.1 前綴狀態 (PrefixState)

```cpp
struct PrefixState {
    Val D{Val::X};        // 當前記憶體資料狀態 (0/1/未知)
    size_t length{0};     // 已選擇的元素數量
};
```

追蹤搜尋過程中的累積狀態，供約束判斷使用。

### 4.2 約束介面 (ISequenceConstraint)

```cpp
class ISequenceConstraint {
    // 判斷在當前 prefix 狀態下，候選 elem 是否允許放在位置 pos
    virtual bool allow(const PrefixState& prefix,
                       const MarchElement& elem,
                       size_t pos) const = 0;
    
    // 選擇 elem 後更新 prefix 狀態
    virtual void update(PrefixState& prefix,
                        const MarchElement& elem,
                        size_t pos) const;
};
```

### 4.3 FirstElementWriteOnlyConstraint

**目的**：確保第一個元素只包含 Write 操作，用於初始化記憶體狀態。

```cpp
class FirstElementWriteOnlyConstraint : public ISequenceConstraint {
    bool allow(const PrefixState& prefix,
               const MarchElement& elem,
               size_t pos) const override {
        // 只限制第一個元素
        if (pos != 0 && prefix.length != 0) return true;
        
        bool has_write = false;
        for (const auto& op : elem.ops) {
            if (op.kind == OpKind::Write) {
                has_write = true;
            } else if (op.kind == OpKind::Read || op.kind == OpKind::ComputeAnd) {
                return false;  // 禁止第一個元素有 R/C
            }
        }
        return has_write;  // 必須至少有一個 Write
    }
};
```

**邏輯說明**：

| 條件 | 結果 |
|---|---|
| `pos == 0` 且只有 `Write` | ✅ 允許 |
| `pos == 0` 且有 `Read` 或 `Compute` | ❌ 禁止 |
| `pos == 0` 且無任何操作 | ❌ 禁止 |
| `pos > 0` | ✅ 允許（不限制）|

**設計理由**：
- March Test 開始前記憶體狀態未知
- 必須先用 Write 初始化為已知狀態 (0 或 1)
- 才能進行有意義的 Read 或 Compute 操作

### 4.4 DataReadPolarityConstraint

**目的**：確保 Read 操作讀取的值與記憶體當前狀態一致，避免無意義的錯誤讀取。

```cpp
class DataReadPolarityConstraint : public ISequenceConstraint {
    bool allow(const PrefixState& prefix,
               const MarchElement& elem,
               size_t pos) const override {
        if (prefix.D == Val::X) return true;  // 未知狀態允許任何讀取
        
        for (const auto& op : elem.ops) {
            if (op.kind != OpKind::Read) continue;
            // D=0 時禁止 R1；D=1 時禁止 R0
            if (prefix.D == Val::Zero && op.value == Val::One) return false;
            if (prefix.D == Val::One && op.value == Val::Zero) return false;
        }
        return true;
    }
    
    void update(PrefixState& prefix,
                const MarchElement& elem,
                size_t pos) const override {
        // 追蹤最後一次 Write 的值
        for (const auto& op : elem.ops) {
            if (op.kind == OpKind::Write) {
                prefix.D = op.value;  // 更新 D 狀態
            }
        }
        ++prefix.length;
    }
};
```

**邏輯說明**：

| 當前 D 狀態 | 允許的 Read | 禁止的 Read |
|---|---|---|
| `X` (未知) | R0, R1 | 無 |
| `Zero` | R0 | R1 |
| `One` | R1 | R0 |

**狀態更新規則**：
- 遇到 `Write` 操作時，將 `D` 更新為寫入的值
- 確保後續元素的 Read 極性正確

**設計理由**：
- 在無故障的情況下，讀取應得到記憶體的實際值
- R1 期望讀到 1，若 D=0 則必然失敗（或偵測到故障）
- 約束確保生成的 March Test 在正常情況下可執行

### 4.5 約束集合 (SequenceConstraintSet)

```cpp
class SequenceConstraintSet {
    vector<shared_ptr<ISequenceConstraint>> constraints_;
public:
    void add(shared_ptr<ISequenceConstraint> c);
    
    bool allow(const PrefixState& prefix,
               const MarchElement& elem,
               size_t pos) const {
        for (const auto& c : constraints_) {
            if (!c->allow(prefix, elem, pos)) return false;
        }
        return true;  // 所有約束都通過
    }
    
    void update(PrefixState& prefix,
                const MarchElement& elem,
                size_t pos) const {
        for (const auto& c : constraints_) {
            c->update(prefix, elem, pos);
        }
    }
};
```

約束集合採用「全部通過」(AND) 語意：只有當所有約束都允許時，候選元素才被接受。

---

## 5. 評分函數 (Score Function)

### 5.1 評分公式

$$\text{Score} = w_s \cdot \Delta\text{state} + w_t \cdot \Delta\text{total} - w_{op} \cdot N_{ops} - w_d \cdot \Delta\text{disrupt}$$

其中：
- $\Delta\text{state}$：state coverage 的變化量（相對於前一步）
- $\Delta\text{total}$：total coverage 的變化量
- $N_{ops}$：當前 March Test 的總操作數
- $\Delta\text{disrupt}$：被干擾 TP 數量的變化量
- $w_s, w_t, w_{op}, w_d$：各項權重參數

### 5.2 評分函數工廠

```cpp
using ScoreFunc = std::function<double(const SimulationResult&, const MarchTest&)>;

ScoreFunc make_score_state_total_ops_disrupt(
    double w_state,         // state coverage 權重
    double w_total,         // total coverage 權重
    double op_penalty,      // 每個操作的懲罰
    double disrupt_penalty  // disrupt 變化量的懲罰
) {
    return [=](const SimulationResult& sim, const MarchTest& mt) -> double {
        size_t ops_count = 0;
        for (const auto& e : mt.elements) 
            ops_count += e.ops.size();
        
        // 從 sim.cover_lists 統計 disrupted 數量
        size_t disrupt_count = 0;
        for (const auto& cl : sim.cover_lists)
            disrupt_count += cl.disrupted.size();
            
        return w_state * sim.state_coverage
             + w_total * sim.total_coverage
             - op_penalty * static_cast<double>(ops_count)
             - disrupt_penalty * static_cast<double>(disrupt_count);
    };
}
```

### 5.3 權重參數選擇指南

| 參數 | 建議值 | 說明 |
|---|---|---|
| $w_s$ (state) | 0.9 ~ 1.0 | 優先最大化 state coverage |
| $w_t$ (total) | 0.5 ~ 1.0 | 次要考慮 total coverage |
| $w_{op}$ (op_penalty) | 0.01 ~ 0.05 | 鼓勵簡潔的序列 |
| $w_d$ (disrupt_penalty) | 0.1 ~ 0.5 | 避免產生過多干擾 |

### 5.4 權重效果分析

| 調整方向 | 效果 |
|---|---|
| 增大 $w_s$ | 偏好能快速達到高 state coverage 的元素 |
| 增大 $w_t$ | 偏好能提升最終偵測覆蓋率的元素 |
| 增大 $w_{op}$ | 產生更短的 March Test |
| 增大 $w_d$ | 避免選擇會造成干擾的元素 |

### 5.5 變化量計算

在 Greedy 搜尋中，變化量的計算需要前一步的基準值：

```
prev_state = 前一步的 state_coverage (初始為 0)
prev_total = 前一步的 total_coverage (初始為 0)
prev_disrupt  = 前一步的 disrupt_count (初始為 0)

Δstate = sim.state_coverage - prev_state
Δtotal = sim.total_coverage - prev_total
Δdisrupt  = current_disrupt_count - prev_disrupt
```

---

## 6. 與 FaultSimulator 的互動（黑盒子視角）

Greedy 搜尋器將 `FaultSimulator` 視為黑盒子，僅透過其輸入/輸出介面互動。

### 6.1 輸入

```cpp
SimulationResult simulate(
    const MarchTest& mt,           // 待模擬的 March Test
    const vector<Fault>& faults,   // 故障列表
    const vector<TestPrimitive>& tps  // 測試原語列表
);
```

| 輸入 | 說明 |
|---|---|
| `MarchTest` | 包含多個 `MarchElement` 的完整測試序列 |
| `Fault[]` | 從 JSON 解析並正規化的故障列表 |
| `TestPrimitive[]` | 由 TPGenerator 產生的測試原語 |

### 6.2 輸出

```cpp
struct SimulationResult {
    double state_coverage;   // State 階段覆蓋率 [0, 1]
    double sens_coverage;    // Sensitization 階段覆蓋率 [0, 1]
    double detect_coverage;  // Detection 階段覆蓋率 [0, 1]
    double total_coverage;   // 總覆蓋率 (= detect_coverage)
    
    vector<RawCoverLists> cover_lists;  // 每操作的覆蓋資訊
};

struct RawCoverLists {
    vector<TpGid> state_cover;   // 此操作 state-covered 的 TP
    vector<TpGid> sens_cover;    // 此操作 sensitized 的 TP
    vector<TpGid> det_cover;     // 此操作 detected 的 TP
    vector<DisruptOutcome> disrupted;  // 此操作干擾的 TP
};

struct DisruptOutcome {
    TpGid tp_gid;           // 被干擾的 TP ID
    enum class Status { AllDisrupted, PartDisrupted, NoDisruption };
    Status status;          // 干擾狀態
};
```

### 6.3 黑盒子使用模式

```
對於每個候選元素:
    trial_mt = prefix_mt + candidate_element
    
    ┌─────────────────────────────────────┐
    │         FaultSimulator              │
    │                                     │
    │  trial_mt ──────▶ [黑盒子] ──────▶ SimulationResult
    │  faults   ──────▶           │
    │  tps      ──────▶           │
    └─────────────────────────────────────┘
    
    score = ScoreFunc(SimulationResult, trial_mt)
```

### 6.4 重要假設

1. **無副作用**：每次 `simulate()` 呼叫獨立，不影響後續呼叫
2. **確定性**：相同輸入產生相同輸出
3. **完整模擬**：每次傳入完整的 `prefix_mt`，而非增量式

---

## 7. Greedy 搜尋演算法

### 7.1 演算法虛擬碼

```
輸入: 
    TemplateLibrary lib      // 模板庫
    Fault[] faults           // 故障列表
    TP[] tps                 // 測試原語
    size_t L                 // 目標序列長度
    ScoreFunc scorer         // 評分函數
    ConstraintSet constraints // 約束集合

輸出: CandidateResult (最佳 March Test)

演算法:

1. 初始化
   prefix_mt = 空的 MarchTest
   prefix_state = PrefixState{D=X, length=0}
   chosen_ids = []
   best_overall = {score = -∞}

2. 對於每個位置 pos ∈ [0, L):
   
   2.1 初始化此步最佳
       best_score = -∞
       best_elem = null
       best_tid = 0
       best_sim = null
       
   2.2 遍歷所有模板
       對於每個 tid ∈ [0, lib.size()):
           
           2.2.1 展開模板為候選元素
                 candidates = ValueExpandingGenerator.generate(lib, tid)
                 
           2.2.2 對於每個候選元素 elem ∈ candidates:
                 
                 a) 約束檢查
                    if NOT constraints.allow(prefix_state, elem, pos):
                        continue  // 跳過不合法的候選
                 
                 b) 構建試驗 MarchTest
                    trial_mt = prefix_mt.copy()
                    trial_mt.elements.append(elem)
                 
                 c) 模擬（黑盒子呼叫）
                    sim_result = FaultSimulator.simulate(trial_mt, faults, tps)
                 
                 d) 計算分數
                    score = scorer(sim_result, trial_mt)
                 
                 e) 更新此步最佳
                    if score > best_score:
                        best_score = score
                        best_tid = tid
                        best_elem = elem
                        best_sim = sim_result

   2.3 提交最佳選擇
       if best_score == -∞:
           break  // 無有效候選，終止
       
       prefix_mt.elements.append(best_elem)
       chosen_ids.append(best_tid)
       constraints.update(prefix_state, best_elem, pos)
       
   2.4 更新全域最佳
       current_result = CandidateResult{
           sequence: chosen_ids,
           march_test: prefix_mt,
           sim_result: best_sim,
           score: scorer(best_sim, prefix_mt)
       }
       if current_result.score > best_overall.score:
           best_overall = current_result

3. 返回 best_overall
```

### 7.2 複雜度分析

設：
- $L$ = 目標序列長度
- $K$ = 模板庫大小
- $E$ = 平均每模板展開數
- $S$ = 單次模擬時間
- $C$ = 約束檢查時間（通常 $O(1)$）

| 階段 | 複雜度 |
|---|---|
| 外層迴圈 | $O(L)$ |
| 模板遍歷 | $O(K)$ per level |
| 值展開 | $O(E)$ per template |
| 約束檢查 | $O(C)$ per candidate |
| 模擬呼叫 | $O(S)$ per candidate |
| **總體** | $O(L \times K \times E \times (C + S))$ |

**實際數據估算**：

| 參數 | 典型值 |
|---|---|
| $L$ (序列長度) | 6 |
| $K$ (模板數) | 60 |
| $E$ (展開數) | 32 |
| $S$ (模擬時間) | 1 ms |

$$\text{總模擬次數} = 6 \times 60 \times 32 = 11{,}520$$
$$\text{預估時間} \approx 11.5 \text{ 秒}$$

### 7.3 搜尋流程圖

```
開始
  │
  ▼
┌─────────────────────────────┐
│ 載入 Faults JSON 並正規化   │
│ 生成所有 TestPrimitive      │
└──────────────┬──────────────┘
               │
               ▼
┌─────────────────────────────┐
│ 建立 TemplateLibrary        │
│ (暴力枚舉所有有效模板)      │
└──────────────┬──────────────┘
               │
               ▼
┌─────────────────────────────┐
│ 初始化約束集合              │
│ + FirstElementWriteOnly     │
│ + DataReadPolarity          │
└──────────────┬──────────────┘
               │
               ▼
      ┌────────┴────────┐
      │  pos = 0        │
      └────────┬────────┘
               │
               ▼
      ┌────────────────────────────────────────┐
      │           Greedy 迴圈                  │
      │  ┌──────────────────────────────────┐  │
      │  │ 對每個模板 tid:                  │  │
      │  │   展開為候選元素                 │  │
      │  │   ┌────────────────────────────┐ │  │
      │  │   │ 對每個候選:                │ │  │
      │  │   │   約束檢查                 │ │  │
      │  │   │   if 通過:                 │ │  │
      │  │   │     模擬 → 評分            │ │  │
      │  │   │     更新 best              │ │  │
      │  │   └────────────────────────────┘ │  │
      │  └──────────────────────────────────┘  │
      │                                        │
      │  提交 best_elem                        │
      │  更新 prefix_state                     │
      │  pos++                                 │
      └───────────────┬────────────────────────┘
                      │
                      ▼
               pos < L ?
              ╱         ╲
           是             否
            │              │
            └──────┬───────┘
                   │
                   ▼
         ┌─────────────────┐
         │ 輸出最佳結果    │
         │ CandidateResult │
         └─────────────────┘
                   │
                   ▼
                 結束
```

---

## 8. CandidateResult 結構

```cpp
struct CandidateResult {
    vector<TemplateId> sequence;    // 選擇的模板 ID 序列
    MarchTest march_test;           // 完整的 March Test
    SimulationResult sim_result;    // 模擬結果
    double score;                   // 最終評分
};
```

| 欄位 | 說明 |
|---|---|
| `sequence` | 長度為 $L$ 的模板 ID 陣列，記錄每步選擇 |
| `march_test` | 可直接執行的 March Test 結構 |
| `sim_result` | 最終模擬結果，包含覆蓋率等指標 |
| `score` | 由評分函數計算的最終分數 |

---

## 9. 使用範例

```cpp
// 1. 載入故障並生成 TP
auto faults = load_faults("faults.json");
auto tps = gen_tps(faults);

// 2. 建立模板庫 (4 槽位)
auto lib = TemplateLibrary::make_bruce(4);

// 3. 設定約束
SequenceConstraintSet constraints;
constraints.add(make_shared<FirstElementWriteOnlyConstraint>());
constraints.add(make_shared<DataReadPolarityConstraint>());

// 4. 設定評分函數
auto scorer = make_score_state_total_ops_disrupt(
    0.9,   // w_state: state coverage 權重
    0.5,   // w_total: total coverage 權重
    0.01,  // op_penalty: 操作數懲罰
    0.1    // disrupt_penalty: 干擾懲罰
);

// 5. 執行 Greedy 搜尋
FaultSimulator sim;
GreedyTemplateSearcher searcher(
    sim, lib, faults, tps,
    make_unique<ValueExpandingGenerator>(),
    scorer,
    &constraints
);

CandidateResult result = searcher.run(6);  // 搜尋長度 6 的序列

// 6. 輸出結果
cout << "Best score: " << result.score << endl;
cout << "State coverage: " << result.sim_result.state_coverage * 100 << "%" << endl;
cout << "Total coverage: " << result.sim_result.total_coverage * 100 << "%" << endl;

// 7. 輸出 March Test 結構
for (const auto& elem : result.march_test.elements) {
    cout << (elem.order == AddrOrder::Up ? "↑" : "↓") << "(";
    for (size_t i = 0; i < elem.ops.size(); ++i) {
        if (i > 0) cout << ", ";
        cout << op_to_str(elem.ops[i]);
    }
    cout << "); ";
}
```

---

## 10. 設計決策與權衡

### 10.1 為何使用模板抽象？

| 方法 | 優點 | 缺點 |
|---|---|---|
| 直接枚舉所有操作組合 | 完整搜尋空間 | $O(4^{N \times L})$ 爆炸 |
| **模板 + 值展開** | 有效剪枝 + 可控展開 | 需預定義有效模板規則 |

模板層將「結構」與「值」分離：
- **結構**由模板庫限制（~幾十種有效模板）
- **值**由展開器枚舉（每模板~幾十種）

總候選數從指數級降到多項式級。

### 10.2 貪婪 vs 全域最優

貪婪演算法每步選擇局部最優，可能錯過全域最優解：

| 特性 | 貪婪 | 全域最優 |
|---|---|---|
| 複雜度 | $O(L \times K \times E)$ | $O(K^L \times E^L)$ |
| 解品質 | 局部最優 | 全域最優 |
| 實用性 | 可接受（通常 90%+ 覆蓋率）| 不可行 |

**緩解策略**：
- 可與 Beam Search 互補：Beam 保留多條路徑
- 調整評分函數權重以引導搜尋方向
- 多次執行不同起始點

### 10.3 約束系統的可擴展性

約束系統設計為可插拔式，便於新增規則：

```cpp
// 新增自定義約束
class MyCustomConstraint : public ISequenceConstraint {
    bool allow(const PrefixState& prefix,
               const MarchElement& elem,
               size_t pos) const override {
        // 自定義允許邏輯
        return /* ... */;
    }
    
    void update(PrefixState& prefix,
                const MarchElement& elem,
                size_t pos) const override {
        // 自定義狀態更新
    }
};

// 使用
constraints.add(make_shared<MyCustomConstraint>());
```

### 10.4 評分函數的可配置性

評分函數透過工廠模式支援參數化：

```cpp
// 偏好短序列
auto short_scorer = make_score_state_total_ops_disrupt(0.5, 0.5, 0.1, 0.1);

// 偏好高覆蓋率
auto cov_scorer = make_score_state_total_ops_disrupt(1.0, 1.0, 0.001, 0.01);

// 避免干擾
auto safe_scorer = make_score_state_total_ops_disrupt(0.8, 0.8, 0.01, 0.5);
```

---

## 附錄 A：類別參照表

| 類別 | 檔案 | 功能 |
|---|---|---|
| `TemplateOpKind` | TemplateSearchers.hpp | 模板操作類型枚舉 |
| `TemplateSlot` | TemplateSearchers.hpp | 單一模板槽位 |
| `ElementTemplate` | TemplateSearchers.hpp | 元素模板 (方向 + 槽位) |
| `TemplateLibrary` | TemplateSearchers.hpp | 模板庫容器 |
| `ICandidateGenerator` | TemplateSearchers.hpp | 候選生成器介面 |
| `ValueExpandingGenerator` | TemplateSearchers.hpp | 值展開生成器 |
| `PrefixState` | TemplateSearchers.hpp | 前綴狀態追蹤 |
| `ISequenceConstraint` | TemplateSearchers.hpp | 約束介面 |
| `FirstElementWriteOnlyConstraint` | TemplateSearchers.hpp | 首元素僅寫約束 |
| `DataReadPolarityConstraint` | TemplateSearchers.hpp | 讀取極性約束 |
| `SequenceConstraintSet` | TemplateSearchers.hpp | 約束集合 |
| `ScoreFunc` | TemplateSearchers.hpp | 評分函數型別 |
| `CandidateResult` | TemplateSearchers.hpp | 搜尋結果容器 |
| `GreedyTemplateSearcher` | TemplateSearchers.hpp | 貪婪搜尋器 |

---

## 附錄 B：術語表

| 術語 | 說明 |
|---|---|
| Template | 抽象的操作結構，定義槽位類型但不含具體值 |
| Slot | 模板中的一個位置，可放置 None/Read/Write/Compute |
| Expansion | 將抽象模板展開為具體 MarchElement 的過程 |
| Constraint | 限制候選元素是否可選的規則 |
| PrefixState | 搜尋過程中累積的狀態 (D 值、長度等) |
| ScoreFunc | 評估候選結果品質的函數 |
| Greedy | 每步選擇局部最優的搜尋策略 |
| Coverage | 故障偵測覆蓋率 |
| Disruption | 故障偵測過程被後續操作干擾的現象 |

---

*文件版本：1.0*  
*最後更新：2025-12-22*
