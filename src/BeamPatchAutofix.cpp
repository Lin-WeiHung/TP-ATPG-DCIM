// g++ -std=c++20 -O2 -Wall -Wextra -Iinclude src/BeamPatchAutofix.cpp -o build/beam_patch
// 用法：
//   ./build/beam_patch <faults.json> [--L 6] [--beam 16] [--top 10] [--out output/patchTest.json]
// 說明：
//   - 以 TemplateSearchers 的 BeamTemplateSearcher 產生前 top 個 March 測試
//   - 針對每個候選，找出「未被偵測但其 TP 在 detect 階段被 mask」的群組
//   - 只嘗試插入 Read 偵測器於 mask 寫入之前，並確保 state_coverage 不下降（若原本是 100% 就維持 100%）
//   - 輸出符合 input/MarchTest.json 規格的 JSON（陣列，每個項目 {"March_test","Pattern"}）

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <algorithm>
#include <filesystem>

#include "nlohmann/json.hpp"
#include "../include/FpParserAndTpGen.hpp"
#include "../include/FaultSimulator.hpp"
#include "../include/TemplateSearchers.hpp"

using std::string; using std::vector; using std::cout; using std::cerr; using std::endl;
using nlohmann::json;

namespace {

struct CmdOpts {
    string faults_json;
    int L = 6;
    int beam = 16;
    int top = 10;
    string out = "output/patchTest.json";
};

static bool parse_args(int argc, char** argv, CmdOpts& o){
    if (argc < 2){
        cerr << "Usage: " << (argc>0? argv[0]:"beam_patch")
             << " <faults.json> [--L 6] [--beam 16] [--top 10] [--out output/patchTest.json]\n";
        return false;
    }
    o.faults_json = argv[1];
    for (int i=2;i<argc;i++){
        string a = argv[i];
        auto need = [&](int more){ if (i+more>=argc){ cerr<<"Missing value for "<<a<<"\n"; exit(2);} return string(argv[++i]); };
        if (a=="--L") o.L = std::stoi(need(1));
        else if (a=="--beam") o.beam = std::stoi(need(1));
        else if (a=="--top") o.top = std::stoi(need(1));
        else if (a=="--out") o.out = need(1);
        else { cerr << "Unknown arg: "<< a <<"\n"; return false; }
    }
    if (o.L <= 0) o.L = 6;
    if (o.beam <= 0) o.beam = 16;
    if (o.top <= 0) o.top = 10;
    return true;
}

// ---- 小工具：把 MarchTest 序列化為 input/MarchTest.json 的 Pattern 字串 ----
static char order2c(AddrOrder o){
    switch(o){ case AddrOrder::Up: return 'a'; case AddrOrder::Down: return 'd'; case AddrOrder::Any: default: return 'b'; }
}
static char v2c(Val v){ return v==Val::Zero?'0': v==Val::One?'1':'-'; }
static string op2s(const Op& op){
    if (op.kind == OpKind::Read){ return string("R") + (op.value==Val::One?"1":"0"); }
    if (op.kind == OpKind::Write){ return string("W") + (op.value==Val::One?"1":"0"); }
    // Compute：輸出 C(T)(M)(B) —— 必須都是 0/1
    char t=v2c(op.C_T), m=v2c(op.C_M), b=v2c(op.C_B);
    if (t=='-' || m=='-' || b=='-'){
        // 輸出端不接受 '-'，強制降級為 0（保守）。不過我們的自動補洞只插入 Read，不會走到這裡。
        if (t=='-') t='0'; if (m=='-') m='0'; if (b=='-') b='0';
    }
    std::ostringstream ss; ss << "C("<<t<<")("<<m<<")("<<b<<")"; return ss.str();
}
static string mt2pattern(const MarchTest& mt){
    std::ostringstream ss; bool first=true;
    for (const auto& e : mt.elements){
        if (!first) ss << ' ';
        first=false;
        ss << order2c(e.order) << '(';
        for (size_t i=0;i<e.ops.size();++i){ if (i) ss << ", "; ss << op2s(e.ops[i]); }
        ss << ");";
    }
    return ss.str();
}

// ---- 補洞提案（只考慮 Read 偵測器） ----
struct PatchProposal {
    int elem{-1};
    int insert_pos{-1};
    Val read_val{Val::X};
    size_t tp_gid{};
    int mask_op{-1};
};

static vector<bool> group_detected_from_events(const SimulationEventResult& er){
    vector<bool> g(er.tp_group.total_groups(), false);
    const auto& dd = er.events.detectDone();
    for (size_t i=0;i<dd.size();++i) for (auto eid : dd[i]) g[ er.tp_group.group_of_tp(er.events.events()[eid].tp_gid()) ] = true;
    return g;
}

static vector<PatchProposal> propose_read_patches(const SimulationEventResult& ev_res,
                                                  const vector<TestPrimitive>& tps){
    vector<PatchProposal> out;
    if (ev_res.op_table.empty()) return out;

    // 先找哪些群組已偵測
    auto gdet = group_detected_from_events(ev_res);

    struct Key{ int elem; int ipos; Val v; bool operator==(const Key& o) const { return elem==o.elem && ipos==o.ipos && v==o.v; } };
    struct KeyHash{ size_t operator()(const Key& k) const noexcept { return ((size_t)k.elem*1315423911u) ^ ((size_t)k.ipos*2654435761u) ^ (size_t)k.v; } };
    std::unordered_set<Key, KeyHash> dedup;

    const auto& dm = ev_res.events.detectMasked();
    for (size_t op=0; op<dm.size(); ++op){
        for (auto eid : dm[op]){
            const auto& ev = ev_res.events.events()[eid];
            size_t tp_gid = ev.tp_gid();
            int gid = ev_res.tp_group.group_of_tp(tp_gid);
            if (gid < 0) continue;
            if (gdet[gid]) continue; // 該群組已偵測，不需補

            // 只考慮 Read 偵測器
            const auto& need = tps[tp_gid].detector.detectOp;
            if (need.kind != OpKind::Read) continue;
            if (need.value == Val::X) continue;

            int mask_op = ev.mask_op();
            if (mask_op < 0 || mask_op >= (int)ev_res.op_table.size()) continue;
            const auto& oc = ev_res.op_table[mask_op];
            int elem = oc.elem_index;
            int insert_pos = oc.index_within_elem; // 插在遮蔽寫入之前

            Key key{elem, insert_pos, need.value};
            if (dedup.insert(key).second){
                out.push_back(PatchProposal{elem, insert_pos, need.value, tp_gid, mask_op});
            }
        }
    }
    std::sort(out.begin(), out.end(), [&](const PatchProposal& a, const PatchProposal& b){ return a.mask_op < b.mask_op; });
    return out;
}

static bool apply_patch(MarchTest& mt, const PatchProposal& p){
    if (p.elem < 0 || p.elem >= (int)mt.elements.size()) return false;
    auto& el = mt.elements[p.elem];
    if (p.insert_pos < 0 || p.insert_pos > (int)el.ops.size()) return false;
    Op rop; rop.kind = OpKind::Read; rop.value = p.read_val;
    el.ops.insert(el.ops.begin() + p.insert_pos, rop);
    return true;
}

struct PatchReport {
    MarchTest patched;
    vector<PatchProposal> accepted;
};

static PatchReport autopatch_read_detect_keep_state(FaultSimulator& sim,
                                                    const MarchTest& base,
                                                    const vector<Fault>& faults,
                                                    const vector<TestPrimitive>& tps){
    PatchReport rep; rep.patched = base;
    FaultSimulatorEvent evsim;

    auto base_sim = sim.simulate(rep.patched, faults, tps);
    double base_state = base_sim.state_coverage;
    auto base_ev  = evsim.simulate(rep.patched, faults, tps);
    auto base_gdet = group_detected_from_events(base_ev);

    auto proposals = propose_read_patches(base_ev, tps);
    for (const auto& prop : proposals){
        MarchTest trial = rep.patched;
        if (!apply_patch(trial, prop)) continue;

        auto t_sim = sim.simulate(trial, faults, tps);
        if (t_sim.state_coverage + 1e-12 < base_state) continue; // 不允許 state 下降

        auto t_ev  = evsim.simulate(trial, faults, tps);
        auto t_gdet = group_detected_from_events(t_ev);
        int gid = base_ev.tp_group.group_of_tp(prop.tp_gid);
        bool improved = (gid>=0) && !base_gdet[gid] && t_gdet[gid];
        if (!improved) continue;

        // 接受補丁，更新基線
        rep.patched = std::move(trial);
        base_sim = std::move(t_sim);
        base_ev  = std::move(t_ev);
        base_gdet = std::move(t_gdet);
        rep.accepted.push_back(prop);
    }
    return rep;
}

} // namespace

int main(int argc, char** argv){
    CmdOpts opt; if (!parse_args(argc, argv, opt)) return 2;
    try{
        // 1) Faults → Fault → TPs
        FaultsJsonParser fparser; FaultNormalizer fnorm; TPGenerator tpg;
        auto raw_faults = fparser.parse_file(opt.faults_json);
        vector<Fault> faults; faults.reserve(raw_faults.size());
        for (const auto& rf : raw_faults){
            try { faults.push_back(fnorm.normalize(rf)); }
            catch(const std::exception& e){ cerr << "Skip fault '"<< rf.fault_id <<"': "<< e.what() <<"\n"; }
        }
        vector<TestPrimitive> tps; tps.reserve(2048);
        for (const auto& f : faults){ auto v = tpg.generate(f); tps.insert(tps.end(), v.begin(), v.end()); }
        if (faults.empty() || tps.empty()){ cerr << "No faults/TPs generated.\n"; return 1; }

        // 2) Beam search 產生 top K
        FaultSimulator sim;
        css::template_search::TemplateLibrary lib = css::template_search::TemplateLibrary::make_bruce();
        css::template_search::SequenceConstraintSet constraints;
        constraints.add(std::make_shared<css::template_search::FirstElementWriteOnlyConstraint>());
        constraints.add(std::make_shared<css::template_search::DataReadPolarityConstraint>());
        css::template_search::BeamTemplateSearcher searcher(
            sim, lib, faults, tps, /*beam_width=*/(size_t)opt.beam,
            std::make_unique<css::template_search::ValueExpandingGenerator>(),
            css::template_search::score_state_total_ops,
            &constraints
        );
        auto results = searcher.run((size_t)opt.L, (size_t)opt.top);
        if (results.empty()){ cerr << "Beam search produced no candidates.\n"; return 1; }

        // 3) 對每個候選做 Read 偵測補洞，並輸出到 JSON
        json arr = json::array();
        int idx=0;
        for (auto& cr : results){
            // 命名
            if (cr.march_test.name.empty()) cr.march_test.name = "beam_" + std::to_string(idx);

            auto rep = autopatch_read_detect_keep_state(sim, cr.march_test, faults, tps);
            MarchTest outmt = std::move(rep.patched);
            // MarchTest outmt = cr.march_test;

            json item;
            item["March_test"] = string("BeamPatched ") + std::to_string(idx);
            item["Pattern"] = mt2pattern(outmt);
            arr.push_back(item);
            ++idx;
        }

        // 確保輸出資料夾存在
        try{ std::filesystem::path p(opt.out); if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path()); }catch(...){ }
        std::ofstream ofs(opt.out);
        if (!ofs){ cerr << "Cannot open output file: "<< opt.out <<"\n"; return 2; }
        ofs << arr.dump(2) << "\n";
        ofs.close();
        cout << "Wrote "<< arr.size() <<" patched tests to "<< opt.out <<"\n";
    } catch(const std::exception& e){ cerr << "beam_patch error: "<< e.what() <<"\n"; return 1; }
    return 0;
}
