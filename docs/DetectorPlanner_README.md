# DetectorPlanner — 偵測策略規劃器

## 概述

`DetectorPlanner` 負責根據 Fault 的 Category 和 FaultPrimitive (FPExpr) 規劃偵測器 (Detector)，是 Test Primitive 生成框架中的核心組件。對於 `MustCompute` 和 `EitherReadOrCompute` 類別，會產生 **Compute 型偵測器**，其 CSS（Cross-State）設定規則是本文件的重點。

---

## 雙偵測器展開（Dual Detector Expansion）

### 觸發條件

當 Fault 屬於以下類別時，會產生多種偵測器：

| Category | 產生的偵測器類型 |
|----------|-----------------|
| `MustRead` | Read Detector |
| `MustCompute` | Compute Detector |
| `EitherReadOrCompute` | Read Detector + ComputeAsRead Detector |

### 偵測器結構

```cpp
struct Detector {
    Op detectOp;           // 偵測操作 (Read/ComputeAnd)
    PositionMark pos;      // 位置標記 (#/^/;)
    bool has_set_Ci;       // 是否設定了 Top/Bottom 的 Ci
    AddrOrder order;       // 地址走訪順序 (None/Ascending/Descending)
};
```

---

## Compute Detector 的 CSS 設定規則

### 核心問題

Compute 操作使用三個輸入：C_T（Top）、C_M（Middle）、C_B（Bottom）。偵測時需要決定這三個值如何設定。

### 規則總表

| 欄位 | 設定邏輯 | 說明 |
|------|---------|------|
| **C_M** | 從 `Sv.ops` 中最後一個 ComputeAnd 取 `C_M` | 代表 Victim 自身的 Ci 狀態 |
| **C_T** | 若 `a < v` 且 `canComputeSetCi()` 成立 → 設為 `Sa.Ci` | Aggressor 在上方，透過 Compute 傳遞 |
| **C_B** | 若 `a > v` 且 `canComputeSetCi()` 成立 → 設為 `Sa.Ci` | Aggressor 在下方，透過 Compute 傳遞 |

### `canComputeSetCi()` 條件

```
允許設定 Ci 的條件：
1. Sv.ops 和 Sa.ops 中沒有任何 Read 或 Write 操作（純 Compute 序列）
2. cell_scope 滿足以下之一：
   - TwoCellCrossRow：無條件允許
   - TwoCellRowAgnostic：Sa.Ci == Sv.Ci 時允許

原因：
- CrossRow：Aggressor 確定在不同列，可透過 Compute 的 C_T/C_B 傳遞
- RowAgnostic：Aggressor 可能在同列或跨列，只有當 Sa.Ci == Sv.Ci 時，
               無論哪個方向傳遞結果都相同，因此可設定
```

### 圖解：Cross-Row Compute 偵測

```
         ┌───────────┐
         │    A0     │  ← C_T = Sa.Ci (若 a < v)
         │  (Top)    │
         └─────┬─────┘
               │
    ┌──────────┼──────────┐
    │    A1    │    A3    │  ← Same Row
    │  (Left)  │  (Right) │
    └──────────┴──────────┘
               │
         ┌─────┴─────┐
         │  A2_CAS   │  ← C_M = 從 Sv.ops 最後的 ComputeAnd
         │  (Center) │
         └─────┬─────┘
               │
         ┌─────┴─────┐
         │    A4     │  ← C_B = Sa.Ci (若 a > v)
         │ (Bottom)  │
         └───────────┘
```

---

## 詳細規則說明

### 規則 1：C_M（Middle）的決定

**來源**：從 FPExpr 的 `Sv.ops` 中，取最後一個 `ComputeAnd` 操作的 `C_M` 值。

**程式碼**：
```cpp
Val m = Val::X;
for (auto it = fp.Sv.ops.rbegin(); it != fp.Sv.ops.rend(); ++it) {
    if (it->kind == OpKind::ComputeAnd) {
        m = it->C_M;
        break;
    }
}
d.detectOp.C_M = m;
```

**範例**：
- `Sv = C(1)(0)(1)` → C_M = 0
- `Sv = W0, C(1)(1)(0)` → C_M = 1
- `Sv = W0, R0` → C_M = X（無 Compute，使用預設）

---

### 規則 2：C_T 和 C_B 的決定

**條件**：只有在 `canComputeSetCi()` 為 true 時才設定。

**邏輯**：
```
if canComputeSetCi(fp, scope):
    if group == A_LT_V:   // Aggressor 地址 < Victim 地址 → Aggressor 在「上方」
        C_T = Sa.Ci
    if group == A_GT_V:   // Aggressor 地址 > Victim 地址 → Aggressor 在「下方」
        C_B = Sa.Ci
```

**程式碼**：
```cpp
void DetectorPlanner::setComputeTB(CellScope scope, const FPExpr& fp, 
                                   const OrientationPlan& plan, Detector& d) const {
    if (canComputeSetCi(fp, scope)) {
        const Val sa_ci = fp.Sa ? fp.Sa->Ci.value_or(Val::X) : Val::X;
        if (plan.group == OrientationGroup::A_LT_V) d.detectOp.C_T = sa_ci;
        if (plan.group == OrientationGroup::A_GT_V) d.detectOp.C_B = sa_ci;
        d.has_set_Ci = true;
    }
}
```

---

### 規則 3：`canComputeSetCi()` 詳細邏輯

```cpp
bool DetectorPlanner::canComputeSetCi(const FPExpr& fp, CellScope scope) const {
    // 條件 1：若有任何 Read/Write 操作 → false
    if (fp.s_has_any_op) {
        for (const auto& op : fp.Sv.ops) {
            if (op.kind == OpKind::Read || op.kind == OpKind::Write) return false;
        }
        if (fp.Sa.has_value()) {
            for (const auto& op : fp.Sa->ops) {
                if (op.kind == OpKind::Read || op.kind == OpKind::Write) return false;
            }
        }
    }
    // 條件 2：scope 限制
    if (scope == CellScope::TwoCellCrossRow) return true;       // 跨列：無條件可
    if (scope == CellScope::TwoCellRowAgnostic) {               // 列無關：Sa.Ci == Sv.Ci 時可
        Val sa_ci = fp.Sa.has_value() ? fp.Sa->Ci.value_or(Val::X) : Val::X;
        Val sv_ci = fp.Sv.Ci.value_or(Val::X);
        if (sa_ci == Val::X || sv_ci == Val::X) return true;    // 任一為 X 視為相容
        return (sa_ci == sv_ci);
    }
    return false; // SingleCell：不適用
}
```

**限制原因**：
- `RowAgnostic`：Aggressor 可能在同列或跨列。當 `Sa.Ci == Sv.Ci` 時，無論實際方向為何，傳遞的值都相同，因此可設定
- `CrossRow`：Aggressor 確定在不同列，可透過 Compute 的 C_T/C_B 傳遞

---

## 完整範例

### 範例 1：Cross-Row Compute 偵測（可設 Ci）

**Fault Primitive**：
```
< Sa(1Di, C(1)(0)(1)) / Sv(0Di, C(0)(1)(0)) / 1D / 0D / - >
```

**解析**：
- `Sa.Ci = 0`（從 C(1)(0)(1) 的 M 值）
- `Sv.ops` 最後的 ComputeAnd = C(0)(1)(0) → `C_M = 1`
- `cell_scope = TwoCellCrossRow`
- 無 Read/Write → `canComputeSetCi() = true`

**產生的 Detector (a < v)**：
```
detectOp = ComputeAnd
C_T = 0     ← 從 Sa.Ci
C_M = 1     ← 從 Sv 最後的 Compute
C_B = X     ← 未設定（Aggressor 不在下方）
```

**產生的 Detector (a > v)**：
```
detectOp = ComputeAnd
C_T = X     ← 未設定（Aggressor 不在上方）
C_M = 1     ← 從 Sv 最後的 Compute
C_B = 0     ← 從 Sa.Ci
```

---

### 範例 2：Cross-Row 但含 Write（不可設 Ci）

**Fault Primitive**：
```
< Sa(1Di, W0) / Sv(0Di, C(0)(1)(0)) / 1D / 0D / - >
```

**解析**：
- `Sa.ops` 包含 `W0`（Write）
- `canComputeSetCi() = false`

**產生的 Detector**：
```
detectOp = ComputeAnd
C_T = X     ← 不設定
C_M = 1     ← 從 Sv 最後的 Compute
C_B = X     ← 不設定
has_set_Ci = false
```

---

## StateAssembler 與 CSS 填充

當 Detector 設定了 `has_set_Ci = true` 時，`StateAssembler::fill_non_pivot()` 會將非 pivot 的 Aggressor 的 C 設為 X（因為 Ci 已透過 Detector 傳遞）：

```cpp
if (detector.detectOp.kind == OpKind::ComputeAnd && detector.has_set_Ci) {
    ref.C = Val::X;  // Aggressor 的 C 不需在 CSS 中指定
} else {
    ref.C = fp.Sa->Ci.value_or(Val::X);
}
```

---

## 演算法總結

### Algorithm: Compute Detector CSS 設定

```
Input:  FPExpr fp, CellScope scope, OrientationPlan plan
Output: Detector d with C_T, C_M, C_B configured

1:  d.detectOp.kind ← ComputeAnd
2:  
3:  // 設定 C_M：從 Sv 的最後一個 Compute 取 C_M
4:  d.C_M ← last_compute_C_M(fp.Sv.ops) or X
5:  
6:  // 判斷是否可設定 C_T/C_B（canComputeSetCi 邏輯）
7:  if any op in (fp.Sv.ops ∪ fp.Sa.ops) is Read or Write then
8:      return d  // 含 R/W 操作，不設定
9:  if scope = RowAgnostic and Sa.Ci ≠ Sv.Ci then
10:     return d  // 列無關但 Ci 不同，不設定
11: 
12: // 設定 C_T 或 C_B（根據方向群組）
13: sa_ci ← fp.Sa.Ci or X
14: if plan.group = A_LT_V then d.C_T ← sa_ci
15: if plan.group = A_GT_V then d.C_B ← sa_ci
16: d.has_set_Ci ← true
17: 
18: return d
```

---

## 相關類別

- **TPGenerator**：呼叫 DetectorPlanner 產生偵測器，組合成 TestPrimitive
- **StateAssembler**：根據 Detector 填充 CSS
- **OrientationSelector**：決定方向群組（A_LT_V / A_GT_V）
