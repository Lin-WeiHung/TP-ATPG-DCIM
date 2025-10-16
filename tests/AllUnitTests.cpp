// AllUnitTests.cpp
// Minimal but effective unit tests for each class in the repo.
// Build: g++ -std=c++17 -O2 -Wall -Wextra -Iinclude tests/AllUnitTests.cpp -o tests/all_tests

#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <cassert>

#include "FpParserAndTpGen.hpp"
#include "FaultSimulator.hpp"
#include "MarchSynth.hpp"

using std::cout; using std::endl; using std::string; using std::vector;

// Tiny CHECK helper (non-fatal)
static int g_checks=0, g_fails=0;
static void CHECK(bool cond, const char* expr, const char* hint=nullptr){ ++g_checks; if(!cond){ ++g_fails; cout<<"[FAIL] "<<expr; if(hint) cout<<" | "<<hint; cout<<"\n"; } }

// ---------- Helpers ----------
static MarchTest mk_simple_march(){ RawMarchTest r{"UT","a(W0,R0);d(C(1)(1)(1),R1)"}; MarchTestNormalizer n; return n.normalize(r); }

static vector<Fault> load_faults(const string& path){ FaultsJsonParser p; FaultNormalizer n; auto raws = p.parse_file(path); vector<Fault> out; for(const auto& rf: raws) out.push_back(n.normalize(rf)); return out; }
static vector<TestPrimitive> gen_tps(const vector<Fault>& faults){ TPGenerator g; vector<TestPrimitive> tps; for(const auto& f: faults){ auto v=g.generate(f); tps.insert(tps.end(), v.begin(), v.end()); } return tps; }

// ---------- Tests per class ----------
static void test_FaultsJsonParser(){
    cout << "[Class] FaultsJsonParser\n";
    FaultsJsonParser p; auto raws = p.parse_file("input/S_C_faults.json");
    CHECK(!raws.empty(), "parse_file returns non-empty");
    CHECK(!raws[0].fault_id.empty(), "first fault has id");
}

static void test_FaultNormalizer(){
    cout << "[Class] FaultNormalizer\n";
    FaultsJsonParser p; auto raws = p.parse_file("input/S_C_faults.json");
    FaultNormalizer n; auto f = n.normalize(raws[0]);
    CHECK(!f.fault_id.empty(), "fault normalized has id");
    CHECK(!f.primitives.empty(), "fault has primitives");
}

static void test_TPGenerator(){
    cout << "[Class] TPGenerator\n";
    FaultsJsonParser p; auto raws = p.parse_file("input/S_C_faults.json");
    FaultNormalizer n; auto f = n.normalize(raws[0]);
    TPGenerator g; auto tps = g.generate(f);
    CHECK(!tps.empty(), "generate produces TPs");
}

static void test_MarchTestJson_and_Normalizer(){
    cout << "[Class] MarchTestJsonParser & MarchTestNormalizer\n";
    MarchTestJsonParser mp; auto raws = mp.parse_file("input/MarchTest.json");
    CHECK(!raws.empty(), "parse march tests json non-empty");
    MarchTestNormalizer n; auto mt = n.normalize(raws[0]);
    CHECK(!mt.elements.empty(), "normalized test has elements");
}

static void test_RawMarchEditor(){
    cout << "[Class] RawMarchEditor\n";
    RawMarchEditor ed("UT");
    ed.set_current_order(AddrOrder::Up);
    ed.append_op_to_current_element(GenOp::W0);
    ed.append_op_to_current_element(GenOp::R0);
    ed.close_and_start_new_element(AddrOrder::Down);
    ed.append_op_to_current_element(GenOp::C_1_1_1);
    const auto& mt = ed.get_cur_mt();
    CHECK(mt.elements.size()==2, "two elements after close/start");
    CHECK(mt.elements[0].ops.size()==2 && mt.elements[1].ops.size()==1, "op counts per element match");
}

static void test_OpTableBuilder(){
    cout << "[Class] OpTableBuilder\n";
    auto mt = mk_simple_march(); OpTableBuilder b; auto opt = b.build(mt);
    CHECK(!opt.empty(), "build produces op table");
}

static void test_CoverLUT(){
    cout << "[Class] CoverLUT\n";
    CoverLUT lut; CrossState cs{}; cs.enforceDCrule();
    auto& keys = lut.get_compatible_tp_keys(cs);
    CHECK(!keys.empty(), "some compatible keys exist for default state");
}

static void test_StateCoverEngine(){
    cout << "[Class] StateCoverEngine\n";
    StateCoverEngine eng; CrossState s{}; s.enforceDCrule();
    TestPrimitive tp; tp.state = s; vector<TestPrimitive> tps{tp};
    eng.build_tp_buckets(tps);
    auto ids = eng.cover(s);
    CHECK(ids.size()==1 && ids[0]==0, "exact state hit");
}

static void test_SensEngine(){
    cout << "[Class] SensEngine\n";
    auto mt = mk_simple_march(); OpTableBuilder b; auto opt=b.build(mt);
    TestPrimitive tp; tp.ops_before_detect = { opt[0].op };
    SensEngine s; int end = s.cover(opt, 0, tp);
    CHECK(end==0, "single-step sens matches at 0");
}

static void test_DetectEngine(){
    cout << "[Class] DetectEngine\n";
    auto mt = mk_simple_march(); OpTableBuilder b; auto opt=b.build(mt);
    // First, compute a valid sensitization end using SensEngine
    TestPrimitive tpSens; tpSens.ops_before_detect = { opt[0].op, opt[1].op }; // W0, R0
    SensEngine s; int end = s.cover(opt, 0, tpSens);
    CHECK(end==1, "sens end at index 1");

    // Then detect compute C(1)(1)(1) at index 2 using NextElementHead (';') anchor
    DetectEngine d; TestPrimitive tpDet; tpDet.R_has_value=false; tpDet.ops_before_detect = tpSens.ops_before_detect;
    tpDet.detector.detectOp.kind=OpKind::ComputeAnd; tpDet.detector.detectOp.C_T=Val::One; tpDet.detector.detectOp.C_M=Val::One; tpDet.detector.detectOp.C_B=Val::One; tpDet.detector.pos=PositionMark::NextElementHead;
    int id = d.cover(opt, end, tpDet);
    CHECK(id==2, ";-anchor detect compute at index 2");
}

static void test_Reporter(){
    cout << "[Class] Reporter\n";
    Reporter r; SimulationResult res; CoverLists cl; cl.det_cover.push_back(SensDetHit{.tp_gid=0,.sens_id=0,.det_id=0}); res.cover_lists={cl};
    Fault f; f.fault_id="F1"; f.cell_scope=CellScope::SingleCell; f.category=Category::MustRead; vector<Fault> faults{f};
    TestPrimitive tp; tp.parent_fault_id="F1"; vector<TestPrimitive> tps{tp};
    r.build(tps, faults, res);
    CHECK(res.fault_detail_map.count("F1")==1, "fault in map");
}

static void test_FaultSimulator(){
    cout << "[Class] FaultSimulator\n";
    auto faults = load_faults("input/S_C_faults.json"); auto tps = gen_tps(faults);
    FaultSimulator sim; auto mt = mk_simple_march();
    auto res = sim.simulate(mt, faults, tps);
    CHECK(res.op_table.size()==mt.elements[0].ops.size()+mt.elements[1].ops.size(), "op table size matches ops");
}

static void test_SimulatorAdaptor(){
    cout << "[Class] SimulatorAdaptor\n";
    auto faults = load_faults("input/S_C_faults.json"); auto tps = gen_tps(faults);
    SimulatorAdaptor sa(faults, tps);
    auto res = sa.run(mk_simple_march());
    CHECK(res.cover_lists.size()==res.op_table.size(), "adaptor returns sim result");
}

static void test_DiffScorer(){
    cout << "[Class] DiffScorer\n";
    auto faults = load_faults("input/S_C_faults.json"); auto tps = gen_tps(faults);
    SimulatorAdaptor sa(faults, tps);
    SynthConfig cfg; DiffScorer scorer(cfg);
    MarchTest a = mk_simple_march();
    MarchTest b = a; // append one op to increase table length
    if (!b.elements.empty()) { Op w; w.kind=OpKind::Write; w.value=Val::Zero; b.elements[0].ops.push_back(w); }
    auto ra = sa.run(a); auto rb = sa.run(b);
    auto d = scorer.compute(ra, rb);
    // Only validate that compute/gain execute without throwing
    (void)d; int g = scorer.gain(d); (void)g;
    CHECK(true, "diff computed and gain evaluated");
}

static void test_ElementPolicy(){
    cout << "[Class] ElementPolicy\n";
    SynthConfig cfg; ElementPolicy p(cfg);
    CHECK(!p.should_close({}), "empty deltas -> keep open");
    CHECK(p.should_close({Delta{0,0,0,0.0}}), "all zero -> close");
    CHECK(p.should_close({Delta{0,0,1,0.0}}), "detect-only and defer=true -> close");
}

static void test_GreedySynthDriver(){
    cout << "[Class] GreedySynthDriver\n";
    auto faults = load_faults("input/S_C_faults.json"); auto tps = gen_tps(faults);
    SynthConfig cfg; cfg.max_ops = 64; // keep small for unit test
    GreedySynthDriver drv(cfg, faults, tps);
    MarchTest init; init.name = "UT-Greedy";
    auto mt = drv.run(init, 0.01); // small target to finish quickly
    CHECK(!mt.elements.empty(), "greedy returns non-empty test");
}

int main(){
    try{
        test_FaultsJsonParser();
        test_FaultNormalizer();
        test_TPGenerator();
        test_MarchTestJson_and_Normalizer();
        test_RawMarchEditor();
        test_OpTableBuilder();
        test_CoverLUT();
        test_StateCoverEngine();
        test_SensEngine();
        test_DetectEngine();
        test_Reporter();
        test_FaultSimulator();
        test_SimulatorAdaptor();
        test_DiffScorer();
        test_ElementPolicy();
        test_GreedySynthDriver();

        cout << "\n[AllUnitTests] checks="<<g_checks<<" fails="<<g_fails<<(g_fails?" (NG)":" (OK)")<<"\n";
    } catch (const std::exception& e) {
        cout << "Exception: " << e.what() << endl; return 2;
    }
    return g_fails?1:0;
}
