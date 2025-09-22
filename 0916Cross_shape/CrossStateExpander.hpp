// CrossStateExpander.hpp — header-only Cross-shaped State 產生器（Pivot 版）
// 規則：
// - 五格索引: 0 1 2 3 4 對應 {D0C0,D1C1,D2C2,D3C3,D4C4}
// - index=2 (pivot) 代表「觸發 ops 的 cell」。
//   * 若 fault primitive 中 aggressor 有 ops (aggressor_ops 非空) 則 pivot=aggressor。
//   * 否則 pivot=victim（維持舊行為）。
// - 將 pivot 的 init (D / Ci) 寫入 index2；另一個 cell 的 init 於展開 case 時再放入左右 / 上下側。
// - 不變式（套用於 pivot-centered 座標系）：
//     D0 = D1,  D3 = D4,  C1 = C2 = C3  （若群組內有具體 0/1 則整組覆蓋；全 X 保持 X）
// - CellScope 展開：
//   A) Single: 只有 pivot 本身。
//   B) TwoRowAgnostic: 另一 cell 在 pivot 同列左右之一 → 產生 L / R。
//       * 若 pivot=victim → L/R 放 aggressor init。
//       * 若 pivot=aggressor → L/R 放 victim init。
//   C) TwoCrossRow: 另一 cell 在 pivot 上 / 下 → 產生 Top / Bottom （暫不回復 Both 變體）。
//       * 若 pivot=victim → Top/Bottom 放 aggressor init。
//       * 若 pivot=aggressor → Top/Bottom 放 victim init。
// - optional<int> 無值 → X (-1)。
// - 此重構保持 API: expand(prim, scope) 介面不變。

#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "Parser.hpp" // 使用 FaultPrimitive, CellScope 等型別

// ---- 精確 using ----
using std::array;
using std::optional;
using std::string;
using std::vector;

struct CrossCell {
    // D, C ∈ { -1:X, 0, 1 }
    int D;
    int C;
};

struct CrossState {
    // 固定 5 格
    array<CrossCell,5> cells{};
    string case_name; // 例如 "single" / "L" / "R" / "Top" / "Bottom" / "Both"
};

class CrossStateExpander {
public:
    // 將一個 FaultPrimitive 展開為 Cross-shaped states
    vector<CrossState> expand(const FaultPrimitive& prim, CellScope scope) const;

private:
    static int bit_or_X(optional<int> v); // optional -> {-1,0,1}
    static void apply_invariants(CrossState& st);
    static bool pivot_is_aggressor(const FaultPrimitive& prim); // 決定 pivot
    static CrossState make_base(const FaultPrimitive& prim, bool pivotAggressor); // 建立全 X 並寫入 pivot init
};

/* ======================= 實作 ======================= */

inline int CrossStateExpander::bit_or_X(optional<int> v) {
    if (!v.has_value()) return -1; // X
    return (v.value() == 0) ? 0 : 1;
}

inline bool CrossStateExpander::pivot_is_aggressor(const FaultPrimitive& prim) {
    // 若 aggressor 有 ops 則視為觸發來源；否則 victim
    return !prim.S.aggressor_ops.empty();
}

inline CrossState CrossStateExpander::make_base(const FaultPrimitive& prim, bool pivotAggressor) {
    CrossState st;
    for (auto &c : st.cells) { c.D = -1; c.C = -1; }
    if (pivotAggressor) {
        st.cells[2].D = bit_or_X(prim.S.aggressor.D);
        st.cells[2].C = bit_or_X(prim.S.aggressor.Ci);
    } else {
        st.cells[2].D = bit_or_X(prim.S.victim.D);
        st.cells[2].C = bit_or_X(prim.S.victim.Ci);
    }
    st.case_name = "base";
    return st;
}

inline void CrossStateExpander::apply_invariants(CrossState& st) {
    // D0=D1：若其中有 0/1 -> 同側覆蓋
    int leftD = (st.cells[0].D != -1) ? st.cells[0].D : st.cells[1].D;
    if (leftD != -1) { st.cells[0].D = leftD; st.cells[1].D = leftD; }
    // D3=D4
    int rightD = (st.cells[3].D != -1) ? st.cells[3].D : st.cells[4].D;
    if (rightD != -1) { st.cells[3].D = rightD; st.cells[4].D = rightD; }
    // C1=C2=C3：找第一個具體值
    int rowC = st.cells[1].C;
    if (rowC == -1) rowC = st.cells[2].C;
    if (rowC == -1) rowC = st.cells[3].C;
    if (rowC != -1) { st.cells[1].C = rowC; st.cells[2].C = rowC; st.cells[3].C = rowC; }
}

inline vector<CrossState> CrossStateExpander::expand(const FaultPrimitive& prim, CellScope scope) const {
    vector<CrossState> out;
    const bool pivotAgg = pivot_is_aggressor(prim);
    const int agD  = bit_or_X(prim.S.aggressor.D);
    const int agCi = bit_or_X(prim.S.aggressor.Ci);
    const int viD  = bit_or_X(prim.S.victim.D);
    const int viCi = bit_or_X(prim.S.victim.Ci);

    auto add_case = [&](CrossState& st, const string& name){ st.case_name = name; apply_invariants(st); out.push_back(st); };

    // A) single
    if (scope == CellScope::Single) {
        CrossState base = make_base(prim, pivotAgg);
        add_case(base, "single");
        return out;
    }

    // B) TwoRowAgnostic (左右)
    if (scope == CellScope::TwoRowAgnostic) {
        // L case: 另一 cell 放在 index1 (左群組)，R case: index3
        // 依 pivot 決定填哪個初值來源
        if (true) {
            CrossState L = make_base(prim, pivotAgg);
            if (pivotAgg) { // victim 放左側
                if (viD != -1) L.cells[1].D = viD;
                if (viCi != -1) L.cells[1].C = viCi; // row C 由不變式擴散
            } else {        // aggressor 放左側
                if (agD != -1) L.cells[1].D = agD;
                if (agCi != -1) L.cells[1].C = agCi;
            }
            add_case(L, "L");
        }
        if (true) {
            CrossState R = make_base(prim, pivotAgg);
            if (pivotAgg) { // victim 放右側
                if (viD != -1) R.cells[3].D = viD;
                if (viCi != -1) R.cells[3].C = viCi;
            } else {        // aggressor 放右側
                if (agD != -1) R.cells[3].D = agD;
                if (agCi != -1) R.cells[3].C = agCi;
            }
            add_case(R, "R");
        }
        return out;
    }

    // C) TwoCrossRow (上下)
    if (scope == CellScope::TwoCrossRow) {
        // Top: index0, Bottom: index4 放另一 cell
        CrossState Top = make_base(prim, pivotAgg);
        if (pivotAgg) {
            if (viD != -1) Top.cells[0].D = viD;
            if (viCi != -1) Top.cells[0].C = viCi;
        } else {
            if (agD != -1) Top.cells[0].D = agD;
            if (agCi != -1) Top.cells[0].C = agCi;
        }
        add_case(Top, "Top");

        CrossState Bottom = make_base(prim, pivotAgg);
        if (pivotAgg) {
            if (viD != -1) Bottom.cells[4].D = viD;
            if (viCi != -1) Bottom.cells[4].C = viCi;
        } else {
            if (agD != -1) Bottom.cells[4].D = agD;
            if (agCi != -1) Bottom.cells[4].C = agCi;
        }
        add_case(Bottom, "Bottom");
        return out;
    }

    return out;
}
