// DetectorPlanner 驗證程式
// g++ -std=c++20 -O2 -Wall -Wextra -Iinclude tests/DetectorPlannerDemo.cpp -o build/DetectorPlannerDemo && ./build/DetectorPlannerDemo

#include <iostream>
#include <iomanip>
#include <string>
#include "../include/FpParserAndTpGen.hpp"

using std::cout;
using std::endl;
using std::string;

// Helper functions
static const char* v2s(Val v) { 
    switch(v) { 
        case Val::Zero: return "0"; 
        case Val::One: return "1"; 
        case Val::X: return "X"; 
    } 
    return "?"; 
}

static const char* group2s(OrientationGroup g) {
    switch(g) {
        case OrientationGroup::Single: return "Single";
        case OrientationGroup::A_LT_V: return "a<v";
        case OrientationGroup::A_GT_V: return "a>v";
    }
    return "?";
}

static const char* scope2s(CellScope s) {
    switch(s) {
        case CellScope::SingleCell: return "SingleCell";
        case CellScope::TwoCellSameRow: return "TwoCellSameRow";
        case CellScope::TwoCellRowAgnostic: return "TwoCellRowAgnostic";
        case CellScope::TwoCellCrossRow: return "TwoCellCrossRow";
    }
    return "?";
}

static const char* cat2s(Category c) {
    switch(c) {
        case Category::EitherReadOrCompute: return "EitherReadOrCompute";
        case Category::MustRead: return "MustRead";
        case Category::MustCompute: return "MustCompute";
    }
    return "?";
}

static string op2s(const Op& op) {
    if (op.kind == OpKind::Write) {
        return string("W") + (op.value == Val::Zero ? "0" : op.value == Val::One ? "1" : "X");
    } else if (op.kind == OpKind::Read) {
        return string("R") + (op.value == Val::Zero ? "0" : op.value == Val::One ? "1" : "X");
    } else {
        return string("C(") + v2s(op.C_T) + ")(" + v2s(op.C_M) + ")(" + v2s(op.C_B) + ")";
    }
}

static string detector2s(const Detector& d) {
    string out;
    if (d.detectOp.kind == OpKind::Read) {
        out = "Read(expect=" + string(v2s(d.detectOp.value)) + ")";
    } else if (d.detectOp.kind == OpKind::ComputeAnd) {
        out = "Compute(T=" + string(v2s(d.detectOp.C_T)) + 
              ", M=" + string(v2s(d.detectOp.C_M)) + 
              ", B=" + string(v2s(d.detectOp.C_B)) + ")";
    }
    out += " has_set_Ci=" + string(d.has_set_Ci ? "true" : "false");
    return out;
}

void print_separator(const string& title) {
    cout << "\n" << string(70, '=') << "\n";
    cout << "  " << title << "\n";
    cout << string(70, '=') << "\n";
}

void print_test_case(int num, const string& desc, const Fault& fault, const FPExpr& fp) {
    cout << "\n【測試 " << num << "】" << desc << "\n";
    cout << "  CellScope: " << scope2s(fault.cell_scope) << "\n";
    cout << "  Category:  " << cat2s(fault.category) << "\n";
    
    // 印出 Sa
    if (fp.Sa.has_value()) {
        cout << "  Sa: pre_D=" << v2s(fp.Sa->pre_D.value_or(Val::X)) 
             << ", Ci=" << v2s(fp.Sa->Ci.value_or(Val::X)) << ", ops=[";
        for (size_t i = 0; i < fp.Sa->ops.size(); ++i) {
            if (i > 0) cout << ", ";
            cout << op2s(fp.Sa->ops[i]);
        }
        cout << "]\n";
    }
    
    // 印出 Sv
    cout << "  Sv: pre_D=" << v2s(fp.Sv.pre_D.value_or(Val::X)) 
         << ", Ci=" << v2s(fp.Sv.Ci.value_or(Val::X)) << ", ops=[";
    for (size_t i = 0; i < fp.Sv.ops.size(); ++i) {
        if (i > 0) cout << ", ";
        cout << op2s(fp.Sv.ops[i]);
    }
    cout << "]\n";
}

void run_detector_planner(const Fault& fault, const FPExpr& fp, const OrientationPlan& plan) {
    DetectorPlanner planner;
    auto detectors = planner.plan(fault, fp, plan);
    
    cout << "  方向群組: " << group2s(plan.group) << "\n";
    cout << "  產生的偵測器 (" << detectors.size() << " 個):\n";
    for (size_t i = 0; i < detectors.size(); ++i) {
        cout << "    [" << i << "] " << detector2s(detectors[i]) << "\n";
    }
}

// ========================================
// 測試案例
// ========================================

void test_case_1() {
    // Cross-Row, 純 Compute，可設定 Ci
    print_separator("Case 1: Cross-Row + 純 Compute → 可設定 C_T/C_B");
    
    Fault fault;
    fault.fault_id = "CFd_crossrow";
    fault.category = Category::MustCompute;
    fault.cell_scope = CellScope::TwoCellCrossRow;
    
    FPExpr fp;
    // Sa(1Di, Ci=0, C(1)(0)(1))
    fp.Sa = SSpec{};
    fp.Sa->pre_D = Val::One;
    fp.Sa->Ci = Val::Zero;
    fp.Sa->ops.push_back(Op{OpKind::ComputeAnd, Val::X, Val::One, Val::Zero, Val::One});
    
    // Sv(0Di, Ci=1, C(0)(1)(0))
    fp.Sv.pre_D = Val::Zero;
    fp.Sv.Ci = Val::One;
    fp.Sv.ops.push_back(Op{OpKind::ComputeAnd, Val::X, Val::Zero, Val::One, Val::Zero});
    fp.Sv.last_D = Val::Zero;
    fp.s_has_any_op = true;
    
    fault.primitives.push_back(fp);
    print_test_case(1, "Sa(1Di,Ci=0,C(1)(0)(1)) / Sv(0Di,Ci=1,C(0)(1)(0))", fault, fp);
    
    // a < v
    OrientationPlan plan_lt;
    plan_lt.group = OrientationGroup::A_LT_V;
    plan_lt.pivot = WhoIsPivot::Victim;
    plan_lt.nonPivotSlots = {Slot::A0};
    
    cout << "\n  === 測試 a < v (Aggressor 在上方) ===\n";
    run_detector_planner(fault, fp, plan_lt);
    cout << "  期望: C_T = Sa.Ci = 0, C_M = 1 (從 Sv 的 C(0)(1)(0)), C_B = X\n";
    
    // a > v
    OrientationPlan plan_gt;
    plan_gt.group = OrientationGroup::A_GT_V;
    plan_gt.pivot = WhoIsPivot::Victim;
    plan_gt.nonPivotSlots = {Slot::A4};
    
    cout << "\n  === 測試 a > v (Aggressor 在下方) ===\n";
    run_detector_planner(fault, fp, plan_gt);
    cout << "  期望: C_T = X, C_M = 1, C_B = Sa.Ci = 0\n";
}

void test_case_2() {
    // Cross-Row, 但含 Write，不可設定 Ci
    print_separator("Case 2: Cross-Row + 含 Write → 不可設定 C_T/C_B");
    
    Fault fault;
    fault.fault_id = "CFd_crossrow_write";
    fault.category = Category::MustCompute;
    fault.cell_scope = CellScope::TwoCellCrossRow;
    
    FPExpr fp;
    // Sa(1Di, W0)  ← 含 Write
    fp.Sa = SSpec{};
    fp.Sa->pre_D = Val::One;
    fp.Sa->Ci = Val::Zero;
    fp.Sa->ops.push_back(Op{OpKind::Write, Val::Zero, Val::X, Val::X, Val::X});
    
    // Sv(0Di, C(0)(1)(0))
    fp.Sv.pre_D = Val::Zero;
    fp.Sv.Ci = Val::One;
    fp.Sv.ops.push_back(Op{OpKind::ComputeAnd, Val::X, Val::Zero, Val::One, Val::Zero});
    fp.Sv.last_D = Val::Zero;
    fp.s_has_any_op = true;
    
    fault.primitives.push_back(fp);
    print_test_case(2, "Sa(1Di,W0) / Sv(0Di,C(0)(1)(0))", fault, fp);
    
    OrientationPlan plan_lt;
    plan_lt.group = OrientationGroup::A_LT_V;
    plan_lt.pivot = WhoIsPivot::Victim;
    plan_lt.nonPivotSlots = {Slot::A0};
    
    cout << "\n  === 測試 a < v ===\n";
    run_detector_planner(fault, fp, plan_lt);
    cout << "  期望: C_T = X, C_M = 1, C_B = X (因為 Sa 含 Write，不可設 Ci)\n";
}

void test_case_3() {
    // Same-Row，不可設定 Ci（硬體限制）
    print_separator("Case 3: Same-Row → 不可設定 C_T/C_B（硬體限制）");
    
    Fault fault;
    fault.fault_id = "CFd_samerow";
    fault.category = Category::MustCompute;
    fault.cell_scope = CellScope::TwoCellSameRow;
    
    FPExpr fp;
    // Sa(1Di, Ci=0, C(1)(0)(1))
    fp.Sa = SSpec{};
    fp.Sa->pre_D = Val::One;
    fp.Sa->Ci = Val::Zero;
    fp.Sa->ops.push_back(Op{OpKind::ComputeAnd, Val::X, Val::One, Val::Zero, Val::One});
    
    // Sv(0Di, Ci=1, C(1)(1)(1))
    fp.Sv.pre_D = Val::Zero;
    fp.Sv.Ci = Val::One;
    fp.Sv.ops.push_back(Op{OpKind::ComputeAnd, Val::X, Val::One, Val::One, Val::One});
    fp.Sv.last_D = Val::Zero;
    fp.s_has_any_op = true;
    
    fault.primitives.push_back(fp);
    print_test_case(3, "Sa(1Di,Ci=0,C(1)(0)(1)) / Sv(0Di,Ci=1,C(1)(1)(1))", fault, fp);
    
    OrientationPlan plan_lt;
    plan_lt.group = OrientationGroup::A_LT_V;
    plan_lt.pivot = WhoIsPivot::Victim;
    plan_lt.nonPivotSlots = {Slot::A1};
    
    cout << "\n  === 測試 a < v ===\n";
    run_detector_planner(fault, fp, plan_lt);
    cout << "  期望: C_T = X, C_M = 1 (從 Sv 的 C(1)(1)(1)), C_B = X (Same-Row 不可設 Ci)\n";
}

void test_case_4() {
    // EitherReadOrCompute: 產生雙偵測器
    print_separator("Case 4: EitherReadOrCompute → 產生 Read + ComputeAsRead 偵測器");
    
    Fault fault;
    fault.fault_id = "SA0";
    fault.category = Category::EitherReadOrCompute;
    fault.cell_scope = CellScope::SingleCell;
    
    FPExpr fp;
    // Sv(0Di, W1) → 期望讀到 0
    fp.Sv.pre_D = Val::Zero;
    fp.Sv.ops.push_back(Op{OpKind::Write, Val::One, Val::X, Val::X, Val::X});
    fp.Sv.last_D = Val::One;
    fp.s_has_any_op = true;
    
    fault.primitives.push_back(fp);
    print_test_case(4, "Sv(0Di, W1) → F=0D", fault, fp);
    
    OrientationPlan plan;
    plan.group = OrientationGroup::Single;
    plan.pivot = WhoIsPivot::Victim;
    plan.nonPivotSlots = {};
    
    run_detector_planner(fault, fp, plan);
    cout << "  期望: 2 個偵測器\n";
    cout << "    - Read(expect=1) ← 從 Sv.last_D\n";
    cout << "    - Compute(T=X, M=1, B=X) ← ComputeAsRead，C_M 固定為 1\n";
}

void test_case_5() {
    // 多個 Compute，取最後一個的 C_M
    print_separator("Case 5: 多個 Compute → 取最後一個的 C_M");
    
    Fault fault;
    fault.fault_id = "MultiCompute";
    fault.category = Category::MustCompute;
    fault.cell_scope = CellScope::TwoCellCrossRow;
    
    FPExpr fp;
    fp.Sa = SSpec{};
    fp.Sa->pre_D = Val::One;
    fp.Sa->Ci = Val::One;
    fp.Sa->ops.push_back(Op{OpKind::ComputeAnd, Val::X, Val::One, Val::One, Val::One});
    
    // Sv: C(1)(1)(1), C(0)(0)(0) ← 最後一個的 C_M = 0
    fp.Sv.pre_D = Val::Zero;
    fp.Sv.Ci = Val::One;
    fp.Sv.ops.push_back(Op{OpKind::ComputeAnd, Val::X, Val::One, Val::One, Val::One});
    fp.Sv.ops.push_back(Op{OpKind::ComputeAnd, Val::X, Val::Zero, Val::Zero, Val::Zero});
    fp.Sv.last_D = Val::Zero;
    fp.s_has_any_op = true;
    
    fault.primitives.push_back(fp);
    print_test_case(5, "Sv: C(1)(1)(1), C(0)(0)(0)", fault, fp);
    
    OrientationPlan plan_lt;
    plan_lt.group = OrientationGroup::A_LT_V;
    plan_lt.pivot = WhoIsPivot::Victim;
    plan_lt.nonPivotSlots = {Slot::A0};
    
    run_detector_planner(fault, fp, plan_lt);
    cout << "  期望: C_M = 0 (取最後一個 Compute 的 C_M)\n";
}

int main() {
    cout << "\n";
    cout << "╔════════════════════════════════════════════════════════════════╗\n";
    cout << "║     DetectorPlanner — Compute Detector CSS 設定規則驗證       ║\n";
    cout << "╚════════════════════════════════════════════════════════════════╝\n";
    
    test_case_1();
    test_case_2();
    test_case_3();
    test_case_4();
    test_case_5();
    
    print_separator("驗證完成");
    cout << "\n規則總結:\n";
    cout << "  1. C_M: 從 Sv.ops 最後一個 ComputeAnd 取 C_M\n";
    cout << "  2. C_T: 若 a<v 且 canComputeSetCi() → 設為 Sa.Ci\n";
    cout << "  3. C_B: 若 a>v 且 canComputeSetCi() → 設為 Sa.Ci\n";
    cout << "  4. canComputeSetCi() = (CrossRow) && (無 Read/Write)\n";
    cout << "\n";
    
    return 0;
}
