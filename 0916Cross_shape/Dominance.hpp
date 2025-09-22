// Dominance.hpp — header-only dominance checker for faults
// 規則摘要：
// 1) Category 範圍：
//    - 同一 Category：允許比較支配。
//    - 跨 Category：僅允許 must_read 或 must_compute 支配 either_read_or_compute；
//      反向或其他跨向不允許。
// 2) Cell Scope 範圍：
//    - TwoRowAgnostic 支配 Single
//    - TwoCrossRow   支配 Single
//    - TwoRowAgnostic 支配 TwoCrossRow
//    - 同 Scope 允許；其餘方向不允許
// 3) 逐欄位完整支配（primitive）：
//    - 比較欄位（此實作：S.init 的 Ci/D 與 ops 序列）
//    - A 要支配 B：每一欄需「相同」或「A 覆蓋 B」。覆蓋定義（單欄）：
//      0/1 可覆蓋 N/A（未指定），反之不成立；相同值成立。
//    - ops 無 N/A，需完全相同。

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "Parser.hpp" // 取得 Fault / FaultPrimitive / CellScope 等型別

// ---- 精確 using ----
using std::optional;
using std::string;
using std::vector;

/* =====================================
 * 介面區（宣告）
 * ===================================== */
class Dominance {
public:
    // fault 層級：A 是否支配 B
    bool dominates(const Fault& A, const Fault& B) const;

    // primitive 層級：A 是否支配 B
    bool dominates_primitive(const FaultPrimitive& A, const FaultPrimitive& B) const;

private:
    // Category 與 Scope 的允許性檢查
    bool category_allowed(const string& catA, const string& catB) const;
    bool scope_allowed(CellScope sA, CellScope sB) const;

    // 欄位覆蓋：optional<int> 覆蓋規則（0/1 覆蓋 N/A；相同值成立）
    static bool covers_bit(optional<int> a, optional<int> b);

    // ops 完全相等
    static bool equal_ops(const vector<Operation>& a, const vector<Operation>& b);
};

/* =====================================
 * 實作區（定義）
 * ===================================== */

inline bool Dominance::covers_bit(optional<int> a, optional<int> b) {
    if (a.has_value() && b.has_value()) return a.value() == b.value();
    if (a.has_value() && !b.has_value()) return true;   // 0/1 覆蓋 N/A
    if (!a.has_value() && b.has_value()) return false;  // N/A 不覆蓋 0/1
    return true; // 都未指定，視為相同
}

inline bool Dominance::equal_ops(const vector<Operation>& a, const vector<Operation>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].op != b[i].op || a[i].val != b[i].val) return false;
    }
    return true;
}

inline bool Dominance::category_allowed(const string& catA, const string& catB) const {
    if (catA == catB) return true; // 同 Category 可比
    const bool A_is_MR = (catA == "must_read");
    const bool A_is_MC = (catA == "must_compute");
    const bool B_is_ER = (catB == "either_read_or_compute");
    // 只允許 MR->ER 或 MC->ER
    if ((A_is_MR || A_is_MC) && B_is_ER) return true;
    return false;
}

inline bool Dominance::scope_allowed(CellScope sA, CellScope sB) const {
    if (sA == sB) return true; // 同 Scope 可比
    // TwoRowAgnostic 支配 Single / TwoCrossRow
    if (sA == CellScope::TwoRowAgnostic && (sB == CellScope::Single || sB == CellScope::TwoCrossRow)) return true;
    // TwoCrossRow 支配 Single
    if (sA == CellScope::TwoCrossRow && sB == CellScope::Single) return true;
    return false; // 其他方向不允許
}

inline bool Dominance::dominates_primitive(const FaultPrimitive& A, const FaultPrimitive& B) const {
    // init bits 覆蓋：aggressor/victim Ci/D
    if (!covers_bit(A.S.aggressor.Ci, B.S.aggressor.Ci)) return false;
    if (!covers_bit(A.S.aggressor.D,  B.S.aggressor.D )) return false;
    if (!covers_bit(A.S.victim.Ci,    B.S.victim.Ci   )) return false;
    if (!covers_bit(A.S.victim.D,     B.S.victim.D    )) return false;
    // ops 需完全相等（本版未定義 ops 覆蓋關係）
    if (!equal_ops(A.S.aggressor_ops, B.S.aggressor_ops)) return false;
    if (!equal_ops(A.S.victim_ops,    B.S.victim_ops   )) return false;
    return true;
}

inline bool Dominance::dominates(const Fault& A, const Fault& B) const {
    if (!category_allowed(A.category, B.category)) return false;
    if (!scope_allowed(A.cell_scope, B.cell_scope)) return false;

    // 對 B 的每個 primitive，需存在 A 的某個 primitive 可支配
    for (const auto& pb : B.primitives) {
        bool covered = false;
        for (const auto& pa : A.primitives) {
            if (dominates_primitive(pa, pb)) { covered = true; break; }
        }
        if (!covered) return false;
    }
    return true;
}
