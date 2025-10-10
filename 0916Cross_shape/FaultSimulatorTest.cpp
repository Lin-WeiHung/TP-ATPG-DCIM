// FaultSimulator unit tests + end-to-end coverlists dump
// Build & Run (optional):
//   g++ -std=c++20 -Wall -Wextra -Iinclude -I0916Cross_shape 0916Cross_shape/FaultSimulatorTest.cpp -o 0916Cross_shape/fault_sim_test && ./0916Cross_shape/fault_sim_test

#include <iostream>
#include <sstream>
#include <vector>
#include <string>
#include <algorithm>
#include <set>

#include "FpParserAndTpGen.hpp"
#include "FaultSimulator.hpp"

using std::cout; using std::endl; using std::string; using std::vector; using std::set;

// tiny assert helper
static int g_checks=0, g_fails=0;
static void CHECK(bool cond, const char* expr, const char* hint=nullptr){ ++g_checks; if(!cond){ ++g_fails; cout<<"[FAIL] "<<expr; if(hint) cout<<" | "<<hint; cout<<"\n"; } }

static const char* v2s(Val v){ switch(v){ case Val::Zero: return "0"; case Val::One: return "1"; case Val::X: return "-"; } return "?"; }
static string dc_cell(Val d, Val c){ std::ostringstream os; os<<v2s(d)<<","<<v2s(c); return os.str(); }
static string op_str(const Op& op){ auto b=[](Val v){return v==Val::Zero?'0':v==Val::One?'1':'-';}; if(op.kind==OpKind::Write){return op.value==Val::Zero?"W0":op.value==Val::One?"W1":"W-";} if(op.kind==OpKind::Read){return op.value==Val::Zero?"R0":op.value==Val::One?"R1":"R-";} std::string s="C("; s.push_back(b(op.C_T)); s+=")("; s.push_back(b(op.C_M)); s+=")("; s.push_back(b(op.C_B)); s+=")"; return s; }

// helper: a(W0,R0,C(1)(1)(1),R1)
static MarchTest make_mini_march(){ RawMarchTest raw{"Mini", "a(W0,R0,C(1)(1)(1),R1)"}; MarchTestNormalizer norm; return norm.normalize(raw); }

// ===== Test helpers (放在測試檔上方) =====
static inline Val v(int x){ return x==0? Val::Zero : x==1? Val::One : Val::X; }

// 依 encode_to_key 的位元順序建立 CrossState
// order: A1.D, A2_CAS.D, A3.D, A0.C, A2_CAS.C, A4.C
static CrossState mk_css(std::array<int,6> t){
    CrossState s{};
    s.A1.D      = v(t[0]);
    s.A2_CAS.D  = v(t[1]);
    s.A3.D      = v(t[2]);
    s.A0.C      = v(t[3]);
    s.A2_CAS.C  = v(t[4]);
    s.A4.C      = v(t[5]);
    s.enforceDCrule();
    return s;
}

// 將 key 反解為 CrossState（測 CoverLUT 用）
static CrossState decode_key_to_css(size_t key){
    std::array<int,6> t{};
    for(int i=5;i>=0;--i){ t[i] = key % 3; key /= 3; }
    return mk_css(t);
}

// naive 規則（與 CoverLUT 相同）：tp 位元是 2 → 一律相容；否則必等於 op 位元（且 op=2 不相容 0/1）
static bool naive_bit_compat(int tp_bit, int op_bit){
    if(tp_bit==2) return true;
    if(op_bit==2) return false;
    return tp_bit==op_bit;
}
static bool naive_css_compat(const CrossState& tp, const CrossState& op){
    auto g=[&](Val x){ return x==Val::Zero?0: x==Val::One?1:2; };
    int a[6] = {g(tp.A1.D),g(tp.A2_CAS.D),g(tp.A3.D),g(tp.A0.C),g(tp.A2_CAS.C),g(tp.A4.C)};
    int b[6] = {g(op.A1.D),g(op.A2_CAS.D),g(op.A3.D),g(op.A0.C),g(op.A2_CAS.C),g(op.A4.C)};
    for(int i=0;i<6;++i) if(!naive_bit_compat(a[i], b[i])) return false;
    return true;
}

// 建 TP（只用到 state 與 gid，其他欄位不影響本層）
static TestPrimitive mk_tp(const CrossState& st){
    TestPrimitive tp{};
    tp.state = st;
    return tp;
}

// 取 vector<size_t> 當成集合做比較（忽略順序與重覆）
template<typename T>
static std::set<T> as_set(const std::vector<T>& v){ return std::set<T>(v.begin(), v.end()); }


// ============ Unit Tests ============
static void test_OpTableBuilder(){
	cout << "\n[Test] OpTableBuilder" << endl;
	auto mt = make_mini_march();
	OpTableBuilder b; auto opt = b.build(mt);
	CHECK(opt.size()==4, "opt.size()==4");
	int w0=0, r0=1, c111=2, r1=3;
	CHECK(opt[w0].op.kind==OpKind::Write && opt[w0].op.value==Val::Zero, "first is W0");
	CHECK(opt[r0].pre_state.A2_CAS.D==Val::Zero, "pre after W0 => D2=0");
	CHECK(opt[c111].op.kind==OpKind::ComputeAnd && opt[c111].op.C_M==Val::One, "third is C(1)(1)(1)");
	CHECK(opt[r1].pre_state.A0.C==Val::One && opt[r1].pre_state.A2_CAS.C==Val::One && opt[r1].pre_state.A4.C==Val::One, "pre after C => C*=1");
}

static void test_CoverLUT() {
    cout << "\n[Test] CoverLUT" << endl;
    CoverLUT lut; // 建構時即完成 729x729 相容表

    // 1) 完全相等匹配
    {
        auto css = mk_css({0,1,0,1,0,1});
        const auto& tpkeys = lut.get_compatible_tp_keys(css);
        // 找到 tp==css 的那個 key
        auto key = encode_to_key(css);
        bool ok = std::find(tpkeys.begin(), tpkeys.end(), key) != tpkeys.end();
        CHECK(ok, "Exact match key must be present.");
    }

    // 2) 單一 don’t-care（tp 有 1 個位元=2），對應 input 的 0/1/2 都應相容
    {
        // tp: 第 3 位為 2（其他固定）
        auto tp = mk_css({0,1,2,1,0,1});
        size_t tp_key = encode_to_key(tp);

        for(int bit=0; bit<3; ++bit){
            auto op = mk_css({0,1,bit,1,0,1}); // 讓該位跑 0/1/2
            const auto& keys = lut.get_compatible_tp_keys(op);
            bool ok = std::find(keys.begin(), keys.end(), tp_key) != keys.end();
            CHECK(ok, "Single don’t-care should accept all 0/1/2 at that bit.");
        }
    }

    // 3) 多個 don’t-care 的計數：出現次數應為 3^k
    {
        auto tp = mk_css({2,2,0,1,2,1}); // k=3
        size_t tp_key = encode_to_key(tp);
        int cnt = 0;
        for(size_t key=0; key<729; ++key){
            auto op = decode_key_to_css(key);
            const auto& keys = lut.get_compatible_tp_keys(op);
            if(std::find(keys.begin(), keys.end(), tp_key) != keys.end()) ++cnt;
        }
        CHECK(cnt==27, "TP with k=3 don’t-cares should appear for 3^3=27 op_css.");
    }

    // 4) input 含 X：只有 tp=2 能相容
    {
        auto op = mk_css({0,2,1,2,0,1}); // 有兩個 X
        auto tp0 = mk_css({0,0,1,0,0,1});
        auto tp1 = mk_css({0,1,1,1,0,1});
        auto tpX = mk_css({0,2,1,2,0,1});
        size_t k0 = encode_to_key(tp0), k1 = encode_to_key(tp1), kX = encode_to_key(tpX);

        const auto& keys = lut.get_compatible_tp_keys(op);
        bool has0 = std::find(keys.begin(), keys.end(), k0)!=keys.end();
        bool has1 = std::find(keys.begin(), keys.end(), k1)!=keys.end();
        bool hasX = std::find(keys.begin(), keys.end(), kX)!=keys.end();

        CHECK(!has0 && !has1 && hasX, "Only tp=2 matches op=2 at those bits.");
    }

    // 5) 極端 key：全 2 的 tp 應該匹配所有 op_css（所以對任一 op_css 都必出現）
    {
        auto tp_any = mk_css({2,2,2,2,2,2});
        size_t any_key = encode_to_key(tp_any);

        for(int t=0;t<5;++t){ // 抽幾個點測
            auto op = mk_css({t%3, (t+1)%3, t%3, (t+2)%3, (t+1)%3, 0});
            const auto& keys = lut.get_compatible_tp_keys(op);
            bool ok = std::find(keys.begin(), keys.end(), any_key)!=keys.end();
            CHECK(ok, "All-2 tp must appear in every op_css bucket.");
        }
    }

    // 6) 非對稱：op=2, tp=0 不相容
    {
        auto op = mk_css({2,1,1,1,1,1});
        auto tp = mk_css({0,1,1,1,1,1});
        size_t k = encode_to_key(tp);
        const auto& keys = lut.get_compatible_tp_keys(op);
        bool has = std::find(keys.begin(), keys.end(), k)!=keys.end();
        CHECK(!has, "op=2, tp=0 should be incompatible.");
    }

    // 7) 單調性：若 B 命中則 A（用 2 放寬）也必命中
    {
        auto tpB = mk_css({0,1,0,1,0,1});
        auto tpA = mk_css({2,1,0,2,0,1}); // 把第1與第4位放寬成 2
        size_t kA = encode_to_key(tpA), kB = encode_to_key(tpB);

        for(size_t key=0; key<729; ++key){
            auto op = decode_key_to_css(key);
            const auto& keys = lut.get_compatible_tp_keys(op);
            bool hitB = std::find(keys.begin(), keys.end(), kB)!=keys.end();
            bool hitA = std::find(keys.begin(), keys.end(), kA)!=keys.end();
            if(hitB) CHECK(hitA, "Superset TP must hit whenever subset TP hits.");
        }
    }

    // 8) 決定性
    {
        auto op = mk_css({1,0,1,0,1,0});
        auto s1 = as_set(lut.get_compatible_tp_keys(op));
        auto s2 = as_set(lut.get_compatible_tp_keys(op));
        CHECK(s1==s2, "LUT must be deterministic.");
    }
}


static void test_StateCoverEngine(){
    cout << "\n[Test] StateCoverEngine" << endl;

    StateCoverEngine eng;

    // Helper: 只以 state 為主，建立多個 TP
    auto build_with = [&](const vector<CrossState>& states){
        vector<TestPrimitive> tps; tps.reserve(states.size());
        for(const auto& s: states){ auto tp = mk_tp(s); tps.push_back(tp); }
        eng.build_tp_buckets(tps);
        return tps; // 回傳以便查 gid
    };

    // 1) 單一 TP 等值覆蓋
    {
        auto s = mk_css({0,1,0,1,0,1});
        auto tps = build_with({s});
        auto gids = eng.cover(s);
        CHECK(gids.size()==1 && gids[0]==0, "Exact equality should hit the only TP.");
    }

    // 2) 多 TP 同 key（兩個 gid 都應回傳）
    {
        auto s = mk_css({1,1,1,0,0,0});
        auto tps = build_with({s,s}); // gid=0,1 同 state
        auto gids = eng.cover(s);
        auto S = as_set(gids);
        CHECK(S.size()==2 && S.count(0) && S.count(1), "Both gids sharing same key must be returned.");
    }

    // 3) input 含 X：只允許取回該位 tp=2 的 TP
    {
        auto tp0 = mk_css({0,1,1,1,1,1});
        auto tp1 = mk_css({1,1,1,1,1,1});
        auto tpX = mk_css({2,1,1,1,1,1});
        auto tps = build_with({tp0,tp1,tpX}); // gid = 0,1,2

        auto op = mk_css({2,1,1,1,1,1});
        auto gids = eng.cover(op);
        auto S = as_set(gids);
        CHECK(S.size()==1 && S.count(2), "Only tp with 2 at that bit should be returned for op=2.");
    }

    // 4) 空結果：完全不相容
    {
        auto tps = build_with({ mk_css({0,0,0,0,0,0}) });
        auto op = mk_css({1,1,1,1,1,1});
        auto gids = eng.cover(op);
        CHECK(gids.empty(), "No compatible TP should yield empty result.");
    }

    // 5) 清空：重建 buckets 不應保留舊資料
    {
        auto A = build_with({ mk_css({0,0,0,0,0,0}) }); // gid=0
        auto B = build_with({ mk_css({1,1,1,1,1,1}) }); // 重新 build：gid=0 (新集)
        auto opA = mk_css({0,0,0,0,0,0});
        auto gidsA = eng.cover(opA);
        CHECK(gidsA.empty(), "After rebuild, old TP must disappear.");
        auto opB = mk_css({1,1,1,1,1,1});
        auto gidsB = eng.cover(opB);
        CHECK(gidsB.size()==1 && gidsB[0]==0, "New TP present after rebuild.");
    }

    // 6) 決定性
    {
        auto tps = build_with({ mk_css({0,1,0,1,2,2}), mk_css({2,1,0,1,2,2}) });
        auto op = mk_css({0,1,0,1,2,2});
        auto S1 = as_set(eng.cover(op));
        auto S2 = as_set(eng.cover(op));
        CHECK(S1==S2, "StateCoverEngine must be deterministic on repeated queries.");
    }

    // 7) 與 naive 交叉驗證（隨機抽樣幾組）
    {
        vector<CrossState> states;
        // 建 6 個 tp：不同 don’t-care 形態
        states.push_back(mk_css({0,1,0,1,0,1}));
        states.push_back(mk_css({2,1,0,1,0,1}));
        states.push_back(mk_css({0,2,2,1,0,1}));
        states.push_back(mk_css({2,2,2,2,2,2}));
        states.push_back(mk_css({1,1,1,1,1,1}));
        states.push_back(mk_css({0,0,2,2,1,1}));
        auto tps = build_with(states);

        for(int trial=0; trial<20; ++trial){
            // 造一個 op_css
            std::array<int,6> a{ trial%3, (trial/3)%3, (trial/9)%3, (trial/27)%3, 1, 2 };
            auto op = mk_css(a);
            auto gids = as_set(eng.cover(op));

            // naive 比對
            std::set<size_t> expect;
            for(size_t gid=0; gid<tps.size(); ++gid){
                if(naive_css_compat(tps[gid].state, op)) expect.insert(gid);
            }
            CHECK(gids==expect, "Cover result must equal naive compatibility filtering.");
        }
    }
}


static void test_SensEngine(){
	cout << "\n[Test] SensEngine" << endl;
	auto mt = make_mini_march(); OpTableBuilder b; auto opt = b.build(mt);
	TestPrimitive tp; tp.ops_before_detect = { opt[0].op, opt[1].op };
	SensEngine s; int end = s.cover(opt, 0, tp);
	CHECK(end==1, "sens end at op#1");
	TestPrimitive tp2; tp2.ops_before_detect = { opt[1].op }; // mismatch from 0
	int bad = s.cover(opt, 0, tp2); CHECK(bad==-1, "sens mismatch -> -1");
}

static void test_DetectEngine(){
	cout << "\n[Test] DetectEngine" << endl;
	auto mt = make_mini_march(); OpTableBuilder b; auto opt = b.build(mt);
	// sens_end=1 => adjacent should detect op#2 if detector is C(1)(1)(1)
	Detector dec; dec.detectOp.kind=OpKind::ComputeAnd; dec.detectOp.C_T=Val::One; dec.detectOp.C_M=Val::One; dec.detectOp.C_B=Val::One; dec.pos=PositionMark::Adjacent;
	DetectEngine d; TestPrimitive dummy; dummy.detector=dec; dummy.detectionNecessary=true;
	int det = d.cover(opt, 1, dummy); CHECK(det==2, "detect compute at op#2");
	// sens_end=2 => adjacent should detect op#3 if detector is R1
	Detector rd; rd.detectOp.kind=OpKind::Read; rd.detectOp.value=Val::One; rd.pos=PositionMark::Adjacent; dummy.detector=rd;
	int det2 = d.cover(opt, 2, dummy); CHECK(det2==3, "detect R1 at op#3");
}

static void test_Reporter(){
	cout << "\n[Test] Reporter" << endl;
	Fault f; f.fault_id="F1"; f.cell_scope=CellScope::SingleCell; f.category=Category::MustRead;
	TestPrimitive tp; tp.parent_fault_id="F1"; tp.group=OrientationGroup::Single;
	vector<Fault> faults{f}; vector<TestPrimitive> tps{tp};
	CoverLists cl; cl.det_cover.push_back(SensDetHit{.tp_gid=0,.sens_id=0,.det_id=1});
	vector<CoverLists> cls{cl}; Reporter r; SimulationResult res; res.cover_lists = cls;
	r.build(tps, faults, res);
	CHECK(res.fault_detail_map.count("F1")==1, "fault detail exists");
	CHECK(res.fault_detail_map["F1"].coverage==1.0, "single-cell any hit => coverage=1.0");
}

// ============ End-to-End ============
static void dump_coverlists_console(){
	cout << "\n[Test] End-to-End: dump coverlists for each MarchTest" << endl;
	// faults -> TPs
	FaultsJsonParser fparser; FaultNormalizer fnorm; TPGenerator tpg;
	auto raw_faults = fparser.parse_file("0916Cross_shape/faults.json");
	vector<Fault> faults; faults.reserve(raw_faults.size());
	for (const auto& rf : raw_faults) faults.push_back(fnorm.normalize(rf));
	vector<TestPrimitive> all_tps; all_tps.reserve(1024);
	for (const auto& f : faults){ auto tps = tpg.generate(f); all_tps.insert(all_tps.end(), tps.begin(), tps.end()); }

	// March tests
	MarchTestJsonParser mparser; auto mt_raws = mparser.parse_file("0916Cross_shape/MarchTest.json");
	MarchTestNormalizer mnorm; vector<MarchTest> mts; for (const auto& r : mt_raws) mts.push_back(mnorm.normalize(r));

	FaultSimulator sim;
	for (const auto& mt : mts){
		cout << "\n=== MarchTest: " << mt.name << " ===" << endl;
		SimulationResult res = sim.simulate(mt, faults, all_tps);
		// dump cover_lists
		const auto& opt = res.op_table;
		for (size_t i=0; i<opt.size(); ++i){
			const auto& oc = opt[i];
			cout << "op#"<<i<<" elem="<<oc.elem_index<<" idx="<<oc.index_within_elem
				 <<" order="<<(oc.order==AddrOrder::Up?"Up":oc.order==AddrOrder::Down?"Down":"Any")
				 <<" op="<<op_str(oc.op)
				 <<" pre:["<<dc_cell(oc.pre_state.A0.D, oc.pre_state.A0.C)
				 <<"," <<dc_cell(oc.pre_state.A1.D, oc.pre_state.A1.C)
				 <<"," <<dc_cell(oc.pre_state.A2_CAS.D, oc.pre_state.A2_CAS.C)
				 <<"," <<dc_cell(oc.pre_state.A3.D, oc.pre_state.A3.C)
				 <<"," <<dc_cell(oc.pre_state.A4.D, oc.pre_state.A4.C)
				 <<"]\n";

			const auto& cl = res.cover_lists[i];
			// 1) State cover
			cout << "  [state] tp count="<<cl.state_cover.size()<<" ->";
			for (size_t tp_gid : cl.state_cover) { cout << " "<<tp_gid; } cout << "\n";
			// 2) Sens cover (show ops)
			cout << "  [sens ] tp count="<<cl.sens_cover.size()<<"\n";
			for (size_t tp_gid : cl.sens_cover){
				const auto& tp = all_tps[tp_gid];
				cout << "    tp#"<<tp_gid<<" ops:";
				if (tp.ops_before_detect.empty()) cout << " -"; else { for (size_t k=0;k<tp.ops_before_detect.size();++k){ if(k) cout<<","; cout<<op_str(tp.ops_before_detect[k]); } }
				cout << "\n";
			}
			// 3) Detect cover (show ids)
			cout << "  [det  ] hits="<<cl.det_cover.size()<<"\n";
			for (const auto& h : cl.det_cover){ cout << "    tp#"<<h.tp_gid<<" sens_id="<<h.sens_id<<" det_id="<<h.det_id<<"\n"; }
		}
	}
}

int main(){
	try{
		test_OpTableBuilder();
		test_CoverLUT();
		test_StateCoverEngine();
		test_SensEngine();
		test_DetectEngine();
		test_Reporter();
		cout << "\n[UnitTest] checks="<<g_checks<<" fails="<<g_fails<< (g_fails?" (NG)":" (OK)") << endl;
		dump_coverlists_console();
	} catch(const std::exception& e){ cout << "Exception: "<< e.what() << endl; return 2; }
	return g_fails?1:0;
}

