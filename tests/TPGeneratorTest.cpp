#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <cassert>

#include "FpParserAndTpGen.hpp"

using std::cout; using std::endl; using std::string; using std::vector;

static const char* v2s(Val v){
    switch(v){ case Val::Zero: return "0"; case Val::One: return "1"; case Val::X: return "-"; } return "?"; }

static string group2s(OrientationGroup g){
    switch(g){
        case OrientationGroup::Single: return "Single";
        case OrientationGroup::A_LT_V: return "A<V";
        case OrientationGroup::A_GT_V: return "A>V";
    }
    return "?";
}

static string pos2s(PositionMark p){
    switch(p){
        case PositionMark::Adjacent: return "#";
        case PositionMark::SameElementHead: return "^";
        case PositionMark::NextElementHead: return ";";
    }
    return "?";
}

static const char* order2s(Detector::AddrOrder o){
    switch(o){
        case Detector::AddrOrder::Assending: return "a_first";
        case Detector::AddrOrder::Decending: return "v_first";
        case Detector::AddrOrder::None: return "-";
    }
    return "?";
}

// ========== Unit Tests ==========
static bool test_OrientationSelector(){
    cout << "[Unit] OrientationSelector\n";
    OrientationSelector sel;
    {
        FPExpr fp; // Single
        auto plans = sel.plans(CellScope::SingleCell, fp);
        assert(plans.size()==1);
    }
    {
        FPExpr fp; // row-agnostic two plans
        auto plans = sel.plans(CellScope::TwoCellRowAgnostic, fp);
        assert(plans.size()==2);
    }
    {
        FPExpr fp; // if Sa has ops, pivot becomes Aggressor
        fp.Sa = SSpec{}; Op w; w.kind=OpKind::Write; w.value=Val::One; fp.Sa->ops.push_back(w);
        fp.s_has_any_op = true;
        auto plans = sel.plans(CellScope::TwoCellSameRow, fp);
        // should produce two plans with some nonPivotSlots; we just assert non-empty
        assert(!plans.empty());
    }
    cout << "  - OK\n";
    return true;
}

static bool test_DetectorPlanner(){
    cout << "[Unit] DetectorPlanner\n";
    DetectorPlanner dp;

    // Build a fault and FPExpr
    Fault fault; fault.fault_id = "F1"; fault.cell_scope = CellScope::TwoCellCrossRow;

    // Case 1: EitherReadOrCompute -> two detectors
    fault.category = Category::EitherReadOrCompute;
    FPExpr fp1; fp1.Sv.last_D = Val::One; fp1.R.RD.reset(); // ensure detection needed
    OrientationPlan plan1{OrientationGroup::A_LT_V, WhoIsPivot::Victim, {Slot::A0}};
    auto dets1 = dp.plan(fault, fp1, plan1);
    assert(dets1.size()==2);
    assert(dets1[0].kind == DetectKind::Read);
    assert(dets1[1].kind == DetectKind::ComputeAnd);

    // Case 2: MustRead -> one read
    fault.category = Category::MustRead;
    auto dets2 = dp.plan(fault, fp1, plan1);
    assert(dets2.size()==1 && dets2[0].kind==DetectKind::Read);

    // Case 3: MustCompute -> one compute, CM from Sv last compute
    fault.category = Category::MustCompute;
    FPExpr fp3; // add a compute op at Sv
    {
        Op c; c.kind=OpKind::ComputeAnd; c.C_M = Val::Zero; fp3.Sv.ops.push_back(c);
        fp3.Sv.last_D = Val::One;
    }
    fp3.R.RD.reset();
    auto dets3 = dp.plan(fault, fp3, plan1);
    assert(dets3.size()==1 && dets3[0].kind==DetectKind::ComputeAnd);
    // CM equals last compute CM (0)
    assert(dets3[0].C_M == Val::Zero);

    cout << "  - OK\n";
    return true;
}

static bool test_StateAssembler(){
    cout << "[Unit] StateAssembler\n";
    StateAssembler sa;

    // Pivot Victim: pivot uses Sv, non-pivot uses Sa
    FPExpr fp; fp.Sa = SSpec{}; fp.Sa->pre_D = Val::One; fp.Sa->Ci = Val::Zero; fp.Sv.pre_D=Val::Zero; fp.Sv.Ci=Val::One;
    OrientationPlan plan{OrientationGroup::A_LT_V, WhoIsPivot::Victim, {Slot::A0}};

    Detector d; d.kind=DetectKind::Read; // simple read
    CrossState st = sa.assemble(fp, plan, d);
    // CAS
    assert(st.A2_CAS.D == Val::Zero && st.A2_CAS.C == Val::One);
    // A0 from Sa
    assert(st.A0.D == Val::One);

    // When detector is Compute and sets top/bottom C, non-pivot Aggressor Ci becomes X per rule
    Detector d2; d2.kind=DetectKind::ComputeAnd; d2.C_T = Val::One; d2.has_set_Ci = true; // set top Ci implies Sa.Ci is controlled by detector
    CrossState st2 = sa.assemble(fp, plan, d2);
    assert(st2.A0.C == Val::X);

    // ops_before_detect: if MustCompute, drop last compute
    {
        FPExpr fpx; Op w; w.kind=OpKind::Write; w.value=Val::Zero; fpx.Sv.ops.push_back(w);
        Op c; c.kind=OpKind::ComputeAnd; c.C_M=Val::One; fpx.Sv.ops.push_back(c);
        auto ops1 = sa.ops_before_detect(fpx, Category::EitherReadOrCompute); // keep all
        auto ops2 = sa.ops_before_detect(fpx, Category::MustCompute); // drop last compute
        assert(ops1.size()==2);
        assert(ops2.size()==1 && ops2[0].kind==OpKind::Write);
    }

    cout << "  - OK\n";
    return true;
}

// ========== E2E: JSON -> Normalize -> TP -> Markdown ==========
static void write_state_md(std::ofstream& ofs, const CrossState& s){
    ofs << "| A0(D,C) | A1(D,C) | CAS(D,C) | A3(D,C) | A4(D,C) |\n";
    ofs << "|---|---|---|---|---|\n";
    ofs << "| "<<v2s(s.A0.D)<<","<<v2s(s.A0.C)
        << " | "<<v2s(s.A1.D)<<","<<v2s(s.A1.C)
        << " | "<<v2s(s.A2_CAS.D)<<","<<v2s(s.A2_CAS.C)
        << " | "<<v2s(s.A3.D)<<","<<v2s(s.A3.C)
        << " | "<<v2s(s.A4.D)<<","<<v2s(s.A4.C)
        << " |\n";
}

static void write_ops_md(std::ofstream& ofs, const vector<Op>& ops){
    if (ops.empty()) { ofs << "- ops_before_detect: (none)\n"; return; }
    ofs << "- ops_before_detect:" << "\n";
    for (const auto& op : ops){
        switch(op.kind){
            case OpKind::Write: ofs << "  - Write("<<v2s(op.value)<<")\n"; break;
            case OpKind::Read: ofs << "  - Read("<<v2s(op.value)<<")\n"; break;
            case OpKind::ComputeAnd:
                ofs << "  - AND(T="<<v2s(op.C_T)<<",M="<<v2s(op.C_M)<<",B="<<v2s(op.C_B)<<")\n"; break;
        }
    }
}

static void write_detector_md(std::ofstream& ofs, const Detector& d){
    ofs << "- detector:" << "\n";
    ofs << "  - kind: " << (d.kind == DetectKind::Read ? "Read" : "Compute(AND)") << "\n";
    if (d.kind == DetectKind::Read) {
        ofs << "  - read_expect: " << v2s(d.read_expect) << "\n";
    } else {
        ofs << "  - C_T: " << v2s(d.C_T) << "\n";
        ofs << "  - C_M: " << v2s(d.C_M) << "\n";
        ofs << "  - C_B: " << v2s(d.C_B) << "\n";
    }
    ofs << "  - pos: " << pos2s(d.pos) << "\n";
    if (d.pos == PositionMark::SameElementHead || d.pos == PositionMark::NextElementHead) {
        ofs << "  - order: " << order2s(d.order) << "\n";
    }
}

static void write_fp_brief(std::ofstream& ofs, const FPExpr& fp){
    auto optv = [](const std::optional<Val>& ov){ return ov.has_value()? v2s(ov.value()):"-"; };
    ofs << "- FP spec:" << "\n";
    if (fp.Sa.has_value())
        ofs << "  - Sa: pre_D="<< optv(fp.Sa->pre_D) << ", Ci=" << optv(fp.Sa->Ci) << "\n";
    else
        ofs << "  - Sa: (none)\n";
    ofs << "  - Sv: pre_D="<< optv(fp.Sv.pre_D) << ", Ci=" << optv(fp.Sv.Ci) << "\n";
    ofs << "  - F: " << optv(fp.F.FD) << ", R: " << optv(fp.R.RD) << ", C: " << optv(fp.C.Co) << "\n";
}

static void generate_markdown_report(const string& jsonPath, const string& outPath){
    FaultsJsonParser parser; FaultNormalizer norm; TPGenerator gen;
    auto raws = parser.parse_file(jsonPath);

    std::ofstream ofs(outPath);
    ofs << "# TP Report\n\n";

    size_t totalFaults = raws.size();
    size_t totalFPs = 0;
    size_t totalTPs = 0;

    // Normalize all first for counting
    vector<Fault> faults; faults.reserve(raws.size());
    for (const auto& rf : raws) faults.push_back(norm.normalize(rf));
    for (const auto& f : faults) totalFPs += f.primitives.size();

    for (size_t fi = 0; fi < faults.size(); ++fi){
        const auto& rf = raws[fi];
        const auto& fault = faults[fi];

        ofs << "## Fault: "<<fault.fault_id<<"\n\n";
        ofs << "- scope: " << rf.cell_scope << "\n";
        ofs << "- category: " << rf.category << "\n";
        ofs << "- primitives: " << fault.primitives.size() << "\n\n";

        auto tps = gen.generate(fault);
        totalTPs += tps.size();

        // group by FP index
        vector<vector<const TestPrimitive*>> by_fp(fault.primitives.size());
        for (const auto& tp : tps){
            if (tp.parent_fp_index < by_fp.size()) by_fp[tp.parent_fp_index].push_back(&tp);
        }

        for (size_t fpi = 0; fpi < fault.primitives.size(); ++fpi){
            ofs << "### FP["<<fpi<<"]\n\n";
            write_fp_brief(ofs, fault.primitives[fpi]);
            ofs << "\n";
            const auto& list = by_fp[fpi];
            ofs << "- TP count: " << list.size() << "\n\n";

            size_t local = 0;
            for (const auto* tp : list){
                ofs << "#### TP["<<fpi<<":"<<local++<<"] (group="<<group2s(tp->group)<<")\n\n";
                if (!tp->R_has_value) {
                    ofs << "- detector: (none; detection not necessary)\n";
                } else {
                    write_detector_md(ofs, tp->detector);
                }
                ofs << "\n";
                write_state_md(ofs, tp->state);
                ofs << "\n";
                write_ops_md(ofs, tp->ops_before_detect);
                ofs << "\n";
            }
        }
    }

    ofs << "---\n\n";
    ofs << "## Summary\n\n";
    ofs << "- total faults: " << totalFaults << "\n";
    ofs << "- total primitives (FPs): " << totalFPs << "\n";
    ofs << "- total TPs: " << totalTPs << "\n";
    ofs.close();
}

int main(){
    try{
        bool ok1 = test_OrientationSelector();
        bool ok2 = test_DetectorPlanner();
        bool ok3 = test_StateAssembler();
        if (ok1 && ok2 && ok3) cout << "All unit tests passed.\n";

        // E2E markdown generation
        string inJson = "0916Cross_shape/faults.json"; // run from repo root
        string outMd  = "output/TP_Report.md";
        generate_markdown_report(inJson, outMd);
        cout << "Markdown report written to: " << outMd << endl;
    } catch (const std::exception& ex) {
        cout << "Exception: " << ex.what() << endl; return 1;
    }
    return 0;
}
