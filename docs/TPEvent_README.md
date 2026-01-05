# TPEvent 系統 — TP 生命週期追蹤

## 概述

TPEvent 系統用於追蹤每個 Test Primitive (TP) 在 March Test 模擬過程中的**生命週期事件**。每當一個 TP 在某個操作點被「觸發」，就會誕生一個 TPEvent，並隨著模擬進程更新其狀態，直到最終確定為「成功偵測」或「被遮蔽」。

---

## 核心概念

### TPEvent 是什麼？

一個 **TPEvent** 代表「一個 TP 在某個操作點開始的一次覆蓋嘗試」。

- 同一個 TP 可能在多個操作點被觸發，因此會產生**多個 TPEvent**
- 每個 TPEvent 獨立追蹤自己的生命週期
- TPEvent 記錄了「在哪個 op 發生了什麼」

---

## TPEvent 的六種最終狀態

```
┌─────────────┐
│   Stated    │  ← 誕生狀態：State Cover 成功
└──────┬──────┘
       │
       ├── 致敏失敗 ─────────────────────┐
       │                                 ▼
       │                          ┌─────────────┐
       │                          │ SensMasked  │  致敏過程被遮蔽
       │                          └─────────────┘
       │
       ▼
┌─────────────┐
│ Sensitized  │  ← 致敏成功
└──────┬──────┘
       │
       ├── 偵測失敗 ─────────────────────┐
       │                                 ▼
       │                          ┌──────────────┐
       │                          │ DetectMasked │  偵測過程被遮蔽
       │                          └──────────────┘
       │
       ▼
┌─────────────┐
│  Detected   │  ← 偵測成功（最終目標）
└─────────────┘
```

---

## TPEvent 的誕生條件

### 誕生時機：State Cover 成功

當模擬器遍歷到操作 `op_id` 時，會檢查該操作的 pre_state（執行前狀態）是否與任何 TP 的 CSS 相容。若相容，則該 TP **誕生一個 TPEvent**。

```
觸發條件：
  TP.state 與 Op.pre_state 的 6 個欄位相容
  （TP 欄位為 X 視為 wildcard，可匹配任意值）

誕生記錄：
  - tp_gid = 該 TP 的全域 ID
  - state_op = 觸發的操作索引
  - final_status = Stated
```

---

## TPEvent 的狀態更新

### 階段 1 → 2：致敏（Sensitization）

誕生後，模擬器嘗試匹配 TP 的 `ops_before_detect` 序列。

| 結果 | 狀態更新 | 記錄 |
|------|---------|------|
| 序列完整匹配 | `Stated → Sensitized` | `sens_ops` 加入結束 op |
| 序列中途不匹配 | `Stated → SensMasked` | `mask_op` = 失敗的 op |
| 無需致敏（序列為空）| 直接進入偵測階段 | `sens_ops` 保持空 |

### 階段 2 → 3：偵測（Detection）

致敏成功後，模擬器尋找符合 TP.detector 的操作。

| 結果 | 狀態更新 | 記錄 |
|------|---------|------|
| 找到匹配的偵測操作 | `Sensitized → Detected` | `det_op` = 偵測 op |
| 在找到前被 Write 覆蓋 | `Sensitized → DetectMasked` | `mask_op` = Write op |
| 找不到偵測操作 | 維持 `Sensitized` | 無更新 |

---

## TPEvent 記錄的資訊

```cpp
class TPEvent {
    TpGid tp_gid_;           // 所屬 TP 的全域 ID
    EventId id_;             // 此事件的唯一 ID
    
    OpId state_op_;          // 誕生的操作（State Cover 發生處）
    vector<OpId> sens_ops_;  // 致敏完成的操作（可能多個路徑）
    OpId det_op_;            // 偵測成功的操作（-1 表示未偵測）
    OpId mask_op_;           // 被遮蔽的操作（-1 表示未被遮蔽）
    
    Status final_status_;    // 最終狀態
};
```

---

## TPEventCenter — 事件管理中心

`TPEventCenter` 負責管理所有 TPEvent，並提供按操作索引查詢的能力。

### 資料結構

```cpp
class TPEventCenter {
    vector<TPEvent> events_;           // 所有事件
    
    // Per-Op Buckets（按操作索引分桶）
    vector<vector<EventId>> state_begins_;   // [op] → 在此 op 誕生的事件
    vector<vector<EventId>> sens_done_;      // [op] → 在此 op 完成致敏的事件
    vector<vector<EventId>> detect_done_;    // [op] → 在此 op 偵測成功的事件
    vector<vector<EventId>> sens_masked_;    // [op] → 在此 op 致敏被遮蔽的事件
    vector<vector<EventId>> detect_masked_;  // [op] → 在此 op 偵測被遮蔽的事件
    
    // Per-TP 索引
    vector<vector<EventId>> tp2events_;      // [tp_gid] → 該 TP 的所有事件
};
```

### 用途

1. **視覺化報告**：顯示每個操作的「發生了什麼」
2. **Coverage 分析**：追蹤哪些 TP 已被偵測
3. **Masking 診斷**：找出造成覆蓋失敗的操作

---

## 完整生命週期範例

### 範例：TP 成功被偵測

```
March Test: a(W0); a(W1, R1)

TP: state = (D=0), ops_before_detect = [W1], detector = R1

模擬流程：
┌────────────────────────────────────────────────────────────────┐
│ Op 0: W0                                                       │
│   └── TP 誕生：state_op = 0, status = Stated                  │
│                                                                │
│ Op 1: W1                                                       │
│   └── 致敏成功：sens_ops = [1], status = Sensitized           │
│                                                                │
│ Op 2: R1                                                       │
│   └── 偵測成功：det_op = 2, status = Detected                 │
└────────────────────────────────────────────────────────────────┘

最終 TPEvent:
  tp_gid = 42
  state_op = 0
  sens_ops = [1]
  det_op = 2
  mask_op = -1
  final_status = Detected
```

### 範例：TP 被遮蔽

```
March Test: a(W0, W1, R0)

TP: state = (D=0), ops_before_detect = [], detector = R0

模擬流程：
┌────────────────────────────────────────────────────────────────┐
│ Op 0: W0                                                       │
│   └── TP 誕生：state_op = 0, status = Stated                  │
│       └── 無需致敏，直接進入偵測                               │
│                                                                │
│ Op 1: W1                                                       │
│   └── 偵測被遮蔽：mask_op = 1, status = DetectMasked          │
│       （在找到 R0 之前，W1 已覆蓋 D 面）                       │
│                                                                │
│ Op 2: R0                                                       │
│   └── 已被遮蔽，不再處理                                       │
└────────────────────────────────────────────────────────────────┘

最終 TPEvent:
  tp_gid = 42
  state_op = 0
  sens_ops = []
  det_op = -1
  mask_op = 1
  final_status = DetectMasked
```

---

## 與 Coverage 計算的關係

TPEventCenter 提供累積查詢方法，用於計算到某個操作為止的覆蓋狀況：

```cpp
// 取得到 op_idx 為止，所有已達到特定階段的 TP GID
vector<TpGid> accumulate_tp_gids_upto(size_t op_idx, Stage s) const;

// Stage 可為：State, Sens, Detect
```

這可用於：
- 繪製 Coverage 成長曲線
- 分析各階段的瓶頸
- 識別關鍵操作

---

## 設計優勢

| 特性 | 說明 |
|------|------|
| **事件驅動** | 只在狀態變化時記錄，避免冗餘資料 |
| **多路徑追蹤** | 同一 TP 可有多個 Event，捕捉所有覆蓋機會 |
| **雙向索引** | 可從 Op 查 Event，也可從 TP 查 Event |
| **不可變狀態** | 事件一旦終結（Detected/Masked），狀態不再改變 |

---

## 相關類別

- **FaultSimulatorEvent**：驅動模擬並產生 TPEvent
- **GroupIndex**：管理 TP 的群組覆蓋狀態
- **SimulationEventResult**：封裝模擬結果（包含 TPEventCenter）
