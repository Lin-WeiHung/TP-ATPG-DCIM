// g++ -std=c++20 -O2 -Wall -Wextra -Iinclude src/FaultSimulationEvent.cpp -o build/FaultSimulationEvent
// ./build/FaultSimulationEvent input/S_C_faults.json input/MarchTest.json output/March_Sim_Report_event.html

#include <iostream>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <iomanip>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>
#include <filesystem>
#include <chrono>

#include "../include/FaultSimulator.hpp" // now provides TPEvent, TPEventCenter, SimulationEventResult, FaultSimulatorEvent

using std::string; using std::vector; using std::cout; using std::endl; using std::unordered_map; using std::unordered_set;

// =============================
// HTML writers（對齊 MarchSimHtml.cpp 的外觀與結構）
// =============================

static const char* v2s(Val v){ switch(v){ case Val::Zero: return "0"; case Val::One: return "1"; case Val::X: return "-";} return "?"; }
static const char* addr2s(AddrOrder o){ switch(o){ case AddrOrder::Up: return "Up"; case AddrOrder::Down: return "Down"; case AddrOrder::Any: return "Any";} return "?"; }
static const char* pos2s(PositionMark p){ switch(p){ case PositionMark::Adjacent: return "#"; case PositionMark::SameElementHead: return "^"; case PositionMark::NextElementHead: return ";";} return "?"; }
static const char* group2short(OrientationGroup g){ switch(g){ case OrientationGroup::Single: return "single"; case OrientationGroup::A_LT_V: return "a&lt;v"; case OrientationGroup::A_GT_V: return "a&gt;v";} return "?"; }
static string html_escape(const string& in){ string out; out.reserve(in.size()); for(char c: in){ switch(c){ case '&': out+="&amp;"; break; case '<': out+="&lt;"; break; case '>': out+="&gt;"; break; case '"': out+="&quot;"; break; case '\'': out+="&#39;"; break; default: out.push_back(c); } } return out; }
static string make_anchor_id(const string& raw){ string id; id.reserve(raw.size()); for(char c: raw){ if ((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')) id.push_back(c); else id.push_back('-'); } return id; }
static string op_repr(const Op& op){ auto bit=[](Val v){ return v==Val::Zero?'0':v==Val::One?'1':'-'; }; if (op.kind==OpKind::Write){ if (op.value==Val::Zero) return string("W0"); if (op.value==Val::One) return string("W1"); return string("W-"); } else if (op.kind==OpKind::Read){ if (op.value==Val::Zero) return string("R0"); if (op.value==Val::One) return string("R1"); return string("R-"); } else { std::string s="C("; s.push_back(bit(op.C_T)); s+=")("; s.push_back(bit(op.C_M)); s+=")("; s.push_back(bit(op.C_B)); s+=")"; return s; } }
static string detect_repr(const Detector& d){ if (d.detectOp.kind==OpKind::Read){ if (d.detectOp.value==Val::Zero) return "R0"; if (d.detectOp.value==Val::One) return "R1"; return string("R-"); } if (d.detectOp.kind==OpKind::ComputeAnd){ auto b=[](Val v){ return v==Val::Zero?'0':v==Val::One?'1':'-'; }; std::string s="C("; s.push_back(b(d.detectOp.C_T)); s+=")("; s.push_back(b(d.detectOp.C_M)); s+=")("; s.push_back(b(d.detectOp.C_B)); s+=")"; return s; } return string("?"); }
static string state_cell(Val d, Val c){ std::ostringstream os; os<<v2s(d)<<","<<v2s(c); return os.str(); }

// 將 TPEvent::Status 轉字串（新增功能需要）
static const char* tpe_status_str(TPEvent::Status s){
    switch(s){
        case TPEvent::Status::Stated: return "Stated";
        case TPEvent::Status::Sensitized: return "Sensitized";
        case TPEvent::Status::Detected: return "Detected";
        case TPEvent::Status::StateMasked: return "StateMasked";
        case TPEvent::Status::SensMasked: return "SensMasked";
        case TPEvent::Status::DetectMasked: return "DetectMasked";
    }
    return "?";
}

static void write_fault_anchors(std::ostream& os, const vector<RawFault>& raws){ os << "<section id=\"fault-anchors\" style=\"display:none\">"; for (const auto& rf : raws){ os << "<div id=\"fault-"<< make_anchor_id(rf.fault_id) << "\"></div>"; } os << "</section>"; }
static void write_tp_details(std::ostream& os, const TestPrimitive& tp, const RawFault* raw_fault, bool show_state, bool show_sens, bool show_detect){
    os << "<div class=\"tpd\">";
    if (raw_fault){
        os << "<div><b>Fault:</b> <a href=\"#fault-"<< make_anchor_id(raw_fault->fault_id) << "\">"<< html_escape(raw_fault->fault_id) <<"</a></div>";
        if (tp.parent_fp_index < raw_fault->fp_raw.size()) os << "<div><b>Primitive:</b> "<< html_escape(raw_fault->fp_raw[tp.parent_fp_index]) <<"</div>";
    } else {
        os << "<div><b>Fault:</b> "<< html_escape(tp.parent_fault_id) <<"</div>";
    }
    os << "<div><b>Group:</b> "<< group2short(tp.group) <<"</div>";
    if (show_state){ os << "<div><b>TP State:</b> A0("<< v2s(tp.state.A0.D) <<","<< v2s(tp.state.A0.C) <<") "
        << "A1("<< v2s(tp.state.A1.D) <<","<< v2s(tp.state.A1.C) <<") "
        << "CAS("<< v2s(tp.state.A2_CAS.D) <<","<< v2s(tp.state.A2_CAS.C) <<") "
        << "A3("<< v2s(tp.state.A3.D) <<","<< v2s(tp.state.A3.C) <<") "
        << "A4("<< v2s(tp.state.A4.D) <<","<< v2s(tp.state.A4.C) <<")</div>"; }
    if (show_sens){ std::ostringstream opss; if (tp.ops_before_detect.empty()) opss<<"-"; else { for (size_t i=0;i<tp.ops_before_detect.size();++i){ if(i)opss<<", "; opss<<op_repr(tp.ops_before_detect[i]); } } os << "<div><b>Ops(before detect):</b> "<< html_escape(opss.str()) <<"</div>"; }
    if (show_detect){ os << "<div><b>Detector:</b> "<< detect_repr(tp.detector) << " ["<< pos2s(tp.detector.pos) << "]</div>"; }
    os << "</div>";
}
static void write_op_summary_cells(std::ostream& os, size_t i, const OpContext& oc){
    os << "<td>"<< i <<"</td><td>"<< (oc.elem_index + 1) <<"</td><td>"<< (oc.index_within_elem + 1)
       <<"</td><td>"<< addr2s(oc.order) <<"</td>";
    os << "<td class=\"state\">"<< html_escape(state_cell(oc.pre_state.A0.D,     oc.pre_state.A0.C))     <<"</td>";
    os << "<td class=\"state\">"<< html_escape(state_cell(oc.pre_state.A1.D,     oc.pre_state.A1.C))     <<"</td>";
    os << "<td class=\"state\">"<< html_escape(state_cell(oc.pre_state.A2_CAS.D, oc.pre_state.A2_CAS.C)) <<"</td>";
    os << "<td class=\"state\">"<< html_escape(state_cell(oc.pre_state.A3.D,     oc.pre_state.A3.C))     <<"</td>";
    os << "<td class=\"state\">"<< html_escape(state_cell(oc.pre_state.A4.D,     oc.pre_state.A4.C))     <<"</td>";
    os << "<td>"<< op_repr(oc.op) <<"</td>";
}
static inline void write_common_table_head(std::ostream& os){
    os << "<table class=\"striped\"><thead><tr>"
          "<th>#</th><th>Elem</th><th>Idx</th><th>Order</th>"
          "<th>Pre A0</th><th>Pre A1</th><th>Pre CAS</th><th>Pre A3</th><th>Pre A4</th><th>Op</th><th>Coverage</th><th>TPs</th>"
          "</tr></thead><tbody>";
}

// 覆蓋率累積工具（與 MarchSimHtml 規則一致）
static double coverage_percent_upto(const TPEventCenter& ec, size_t op_idx, TPEventCenter::Stage stage,
                                    const vector<Fault>& faults, const vector<TestPrimitive>& tps){
    auto gids = ec.accumulate_tp_gids_upto(op_idx, stage);
    struct Flags{ bool any=false, lt=false, gt=false; };
    unordered_map<string, Flags> agg;
    for (auto gid : gids){ const auto& tp = tps[gid]; auto& f = agg[tp.parent_fault_id];
        if (tp.group==OrientationGroup::Single) f.any=true; else if (tp.group==OrientationGroup::A_LT_V) f.lt=true; else if (tp.group==OrientationGroup::A_GT_V) f.gt=true; }
    double sum=0.0; size_t denom = faults.size();
    for (const auto& fault : faults){ auto it = agg.find(fault.fault_id); double cov=0.0; if (fault.cell_scope==CellScope::SingleCell){ cov = (it!=agg.end() && it->second.any)?1.0:0.0; } else { bool lt = (it!=agg.end() && it->second.lt); bool gt = (it!=agg.end() && it->second.gt); cov = (lt?0.5:0.0)+(gt?0.5:0.0); } sum += cov; }
    double avg = denom? (sum/denom) : 0.0; return avg * 100.0;
}

static void write_state_table(std::ostream& os, const SimulationEventResult& sim,
                              const vector<TestPrimitive>& tps,
                              const vector<RawFault>& raw_faults,
                              const unordered_map<string, size_t>& raw_index_by_id,
                              const vector<Fault>& faults){
    os << "<details><summary>State cover (rows: "<< sim.op_table.size() <<")</summary>";
    write_common_table_head(os);
    int last_elem=-1; bool useB=true;
    for (size_t i=0;i<sim.op_table.size();++i){ const auto& oc = sim.op_table[i]; if (oc.elem_index!=last_elem){ useB=!useB; last_elem=oc.elem_index; }
        os << "<tr class=\""<< (useB?"rowB":"rowA") <<"\">";
        write_op_summary_cells(os,i,oc);
        // coverage
        {
            double pct = coverage_percent_upto(sim.events, i, TPEventCenter::Stage::State, faults, tps);
            std::ostringstream ss; ss.setf(std::ios::fixed); ss<< std::setprecision(2) << pct << "%";
            os << "<td>"<< ss.str() <<"</td>";
        }
        // TPs
        // 以 tp_gid 去重，避免同一 op 重複列出，並按 gid 升冪排序
        unordered_set<size_t> seen; vector<size_t> unique_tp;
        const auto& begins = sim.events.stateBegins();
        if (i < begins.size()){
            unique_tp.reserve(begins[i].size());
            for (auto eid : begins[i]){
                size_t gid = sim.events.events()[eid].tp_gid(); if (seen.insert(gid).second) unique_tp.push_back(gid);
            }
        }
        std::sort(unique_tp.begin(), unique_tp.end());
        os << "<td><details><summary>TPs ("<< unique_tp.size() <<")</summary>";
        for (auto gid : unique_tp){
            const auto& tp = tps[gid];
            const RawFault* rf = nullptr; if (auto it = raw_index_by_id.find(tp.parent_fault_id); it!=raw_index_by_id.end()) rf = &raw_faults.at(it->second);
            os << "<details><summary>#"<< gid <<"</summary>"; write_tp_details(os, tp, rf, true,false,false); os << "</details>";
        }
        os << "</details></td></tr>";
    }
    os << "</tbody></table></details>";
}

static void write_sens_table(std::ostream& os, const SimulationEventResult& sim,
                             const vector<TestPrimitive>& tps,
                             const vector<RawFault>& raw_faults,
                             const unordered_map<string, size_t>& raw_index_by_id,
                             const vector<Fault>& faults){
    os << "<details><summary>Sens cover (rows: "<< sim.op_table.size() <<")</summary>";
    write_common_table_head(os);
    int last_elem=-1; bool useB=true;
    for (size_t i=0;i<sim.op_table.size();++i){ const auto& oc = sim.op_table[i]; if (oc.elem_index!=last_elem){ useB=!useB; last_elem=oc.elem_index; }
        os << "<tr class=\""<< (useB?"rowB":"rowA") <<"\">";
        write_op_summary_cells(os,i,oc);
        {
            double pct = coverage_percent_upto(sim.events, i, TPEventCenter::Stage::Sens, faults, tps);
            std::ostringstream ss; ss.setf(std::ios::fixed); ss<< std::setprecision(2) << pct << "%"; os << "<td>"<< ss.str() <<"</td>";
        }
        // 以 tp_gid 去重並排序
        unordered_set<size_t> seen; vector<size_t> unique_tp; 
        const auto& sdone = sim.events.sensDone();
        if (i < sdone.size()){
            unique_tp.reserve(sdone[i].size());
            for (auto eid : sdone[i]){ size_t gid = sim.events.events()[eid].tp_gid(); if (seen.insert(gid).second) unique_tp.push_back(gid); }
        }
        std::sort(unique_tp.begin(), unique_tp.end());
        os << "<td><details><summary>TPs ("<< unique_tp.size() <<")</summary>";
        for (auto gid : unique_tp){
            const auto& tp = tps[gid]; const RawFault* rf=nullptr;
            if (auto it = raw_index_by_id.find(tp.parent_fault_id); it!=raw_index_by_id.end()) rf=&raw_faults.at(it->second);
            os << "<details><summary>#"<< gid <<"</summary>"; write_tp_details(os, tp, rf, false,true,false); os << "</details>";
        }
        os << "</details></td></tr>";
    }
    os << "</tbody></table></details>";
}

static void write_detect_table(std::ostream& os, const SimulationEventResult& sim,
                               const vector<TestPrimitive>& tps,
                               const vector<RawFault>& raw_faults,
                               const unordered_map<string, size_t>& raw_index_by_id,
                               const vector<Fault>& faults){
    os << "<details><summary>Detect cover (rows: "<< sim.op_table.size() <<")</summary>";
    write_common_table_head(os);
    int last_elem=-1; bool useB=true;
    for (size_t i=0;i<sim.op_table.size();++i){ const auto& oc = sim.op_table[i]; if (oc.elem_index!=last_elem){ useB=!useB; last_elem=oc.elem_index; }
        os << "<tr class=\""<< (useB?"rowB":"rowA") <<"\">";
        write_op_summary_cells(os,i,oc);
        {
            double pct = coverage_percent_upto(sim.events, i, TPEventCenter::Stage::Detect, faults, tps);
            std::ostringstream ss; ss.setf(std::ios::fixed); ss<< std::setprecision(2) << pct << "%"; os << "<td>"<< ss.str() <<"</td>";
        }
        // 以 tp_gid 去重並排序
        unordered_set<size_t> seen; vector<size_t> unique_tp; 
        const auto& ddone = sim.events.detectDone();
        if (i < ddone.size()){
            unique_tp.reserve(ddone[i].size());
            for (auto eid : ddone[i]){ size_t gid = sim.events.events()[eid].tp_gid(); if (seen.insert(gid).second) unique_tp.push_back(gid); }
        }
        std::sort(unique_tp.begin(), unique_tp.end());
        os << "<td><details><summary>TPs ("<< unique_tp.size() <<")</summary>";
        for (auto gid : unique_tp){
            const auto& tp = tps[gid]; const RawFault* rf=nullptr;
            if (auto it = raw_index_by_id.find(tp.parent_fault_id); it!=raw_index_by_id.end()) rf=&raw_faults.at(it->second);
            os << "<details><summary>#"<< gid <<"</summary>"; write_tp_details(os, tp, rf, false,false,true); os << "</details>";
        }
        os << "</details></td></tr>";
    }
    os << "</tbody></table></details>";
}

static void write_faults_coverage_table(std::ostream& os,
                                        const SimulationEventResult& sim,
                                        const vector<Fault>& faults,
                                        const vector<TestPrimitive>& tps,
                                        const vector<RawFault>& raw_faults,
                                        const unordered_map<string, size_t>& raw_index_by_id){
    // 蒐集被偵測到的 tp 集合（以 tp_gid 為鍵）
    unordered_set<size_t> detected_tp_set;
    for (const auto& bucket : sim.events.detectDone()){ for (auto eid : bucket) detected_tp_set.insert(sim.events.events()[eid].tp_gid()); }

    os << "<details><summary>Fault coverage summary ("<< faults.size() <<")</summary>";
    os << "<table class=\"striped\"><thead><tr><th>#</th><th>Fault ID</th><th>Coverage</th><th>TPs</th></tr></thead><tbody>";

    for (size_t fi=0; fi<faults.size(); ++fi){
        const auto& f = faults[fi];
        // 計算 coverage（依該 fault 的 tp 分組）
        bool has_any=false, has_lt=false, has_gt=false;
        for (size_t tg=0; tg<tps.size(); ++tg){ if (tps[tg].parent_fault_id!=f.fault_id) continue; if (detected_tp_set.count(tg)==0) continue; auto g=tps[tg].group; if (g==OrientationGroup::Single) has_any=true; else if (g==OrientationGroup::A_LT_V) has_lt=true; else if (g==OrientationGroup::A_GT_V) has_gt=true; }
        double cov = 0.0; if (f.cell_scope==CellScope::SingleCell){ cov = has_any?1.0:0.0; } else { cov = (has_lt?0.5:0.0) + (has_gt?0.5:0.0); }
        std::ostringstream pss; pss.setf(std::ios::fixed); pss<< std::setprecision(2) << (cov*100.0) << "%";
        const char* covCls = ""; if (std::abs(cov - 0.0) < 1e-9) covCls = "cov0"; else if (std::abs(cov - 0.5) < 1e-9) covCls = "cov50";
        os << "<tr><td>"<< fi <<"</td><td>"<< html_escape(f.fault_id) <<"</td><td class=\""<< covCls <<"\">"<< pss.str() <<"</td><td>";

        // Detected（僅列出屬於該 fault 的 tp，並計數）
        vector<size_t> detected_for_fault; detected_for_fault.reserve(32);
        for (const auto& tp_gid : detected_tp_set){ if (tps[tp_gid].parent_fault_id != f.fault_id) continue; detected_for_fault.push_back(tp_gid); }
        std::sort(detected_for_fault.begin(), detected_for_fault.end());
        os << "<details><summary>Detected ("<< detected_for_fault.size() <<")</summary>";
        for (const auto& tp_gid : detected_for_fault){ const auto& tp = tps[tp_gid]; const RawFault* rf=nullptr; if (auto it=raw_index_by_id.find(tp.parent_fault_id); it!=raw_index_by_id.end()) rf=&raw_faults[it->second]; os << "<details><summary>#"<< tp_gid <<"</summary>"; write_tp_details(os, tp, rf, true,true,true); os << "</details>"; }
        os << "</details>";

        // Undetected
        vector<size_t> undetected; undetected.reserve(16);
        for (size_t tg=0; tg<tps.size(); ++tg){ if (tps[tg].parent_fault_id == f.fault_id && detected_tp_set.count(tg)==0) undetected.push_back(tg); }
        std::sort(undetected.begin(), undetected.end());
        os << "<details><summary>Undetected ("<< undetected.size() <<")</summary>";
        for (auto tp_gid : undetected){ const auto& tp = tps[tp_gid]; const RawFault* rf=nullptr; if (auto it=raw_index_by_id.find(tp.parent_fault_id); it!=raw_index_by_id.end()) rf=&raw_faults[it->second]; os << "<details><summary>#"<< tp_gid <<"</summary>"; write_tp_details(os, tp, rf, true,true,true); os << "</details>"; }
        os << "</details>";

        os << "</td></tr>";
    }
    os << "</tbody></table></details>";
}

// =============================
// CLI & page head
// =============================

struct CmdOptions { string faults_json; string march_json; string output_html; };
static bool parse_args(int argc, char** argv, CmdOptions& opt){ if (argc!=4){ std::cerr << "Usage: " << (argc>0? argv[0]:"FaultSimulationEvent") << " <faults.json> <MarchTest.json> <output.html>\n"; return false; } opt.faults_json=argv[1]; opt.march_json=argv[2]; opt.output_html=argv[3]; return true; }
static void write_html_head(std::ostream& ofs, size_t faults_n, size_t tps_n, size_t mts_n){
    ofs << "<!DOCTYPE html><html><head><meta charset=\"utf-8\">\n";
    ofs << "<title>March Simulation Report</title>\n";
    ofs << "<style>"
           "body{font-family:sans-serif}"
           "details{margin:8px 0}"
           "summary{cursor:pointer;font-weight:600}"
           "table{border-collapse:collapse;margin:6px 0;width:100%}"
           "th,td{border:1px solid #ccc;padding:4px 6px;text-align:center;vertical-align:top}"
           ".muted{color:#666}"
           ".badge{display:inline-block;background:#eef;border:1px solid #99c;border-radius:10px;padding:2px 8px;margin-left:6px;font-size:12px}"
           ".ops{text-align:left;white-space:nowrap}"
           ".state{font-family:monospace}"
           ".striped tbody tr.rowA{background:#ffffff}"
           ".striped tbody tr.rowB{background:#dce0eb}"
           ".faultHdr{margin-top:8px}"
           ".tpd{margin:6px 0 8px 12px;text-align:left}"
           ".cov0{color:#d33;font-weight:700}"
           ".cov50{color:#06c;font-weight:700}"
         "</style>\n";
    ofs << "</head><body>\n";
    ofs << "<h1>March Simulation Report</h1>\n";
    ofs << "<p class=\"muted\">Faults: "<<faults_n<<", TPs: "<<tps_n<<", MarchTests: "<<mts_n<<"</p>\n";
}

int main(int argc, char** argv){
    CmdOptions opt; if (!parse_args(argc, argv, opt)) return 2;
    try{
        using clock = std::chrono::steady_clock; auto to_us=[](auto d){ return std::chrono::duration_cast<std::chrono::microseconds>(d).count(); };
        // ensure output dir
        try{ std::filesystem::path outp(opt.output_html); if (outp.has_parent_path()) std::filesystem::create_directories(outp.parent_path()); }catch(...){ }

        // 1) Faults → Fault → TPs
        auto t1s=clock::now();
        FaultsJsonParser fparser; FaultNormalizer fnorm; TPGenerator tpg;
        auto raw_faults = fparser.parse_file(opt.faults_json);
        vector<Fault> faults; faults.reserve(raw_faults.size()); vector<string> warnings; warnings.reserve(16);
        for (const auto& rf : raw_faults){ try{ faults.push_back(fnorm.normalize(rf)); } catch(const std::exception& e){ warnings.push_back(string("Skip fault '") + rf.fault_id + "': " + e.what()); } }
        vector<TestPrimitive> all_tps; all_tps.reserve(2048);
        for (const auto& f : faults){ auto tps = tpg.generate(f); all_tps.insert(all_tps.end(), tps.begin(), tps.end()); }
        auto t1e=clock::now(); cout << "[時間] 1) Faults→Fault→TPs: "<< to_us(t1e-t1s) <<" us (raw_faults="<<raw_faults.size()<<", faults="<<faults.size()<<", TPs="<<all_tps.size()<<")\n";

        // fault_id -> raw idx
        unordered_map<string, size_t> raw_index_by_id; raw_index_by_id.reserve(raw_faults.size()*2+1);
        for (size_t i=0;i<raw_faults.size();++i) raw_index_by_id[raw_faults[i].fault_id]=i;

        // 2) March tests
        auto t2s=clock::now();
        MarchTestJsonParser mparser; auto raw_mts = mparser.parse_file(opt.march_json);
        MarchTestNormalizer mnorm; vector<MarchTest> marchTests; marchTests.reserve(raw_mts.size());
        for (const auto& r: raw_mts) marchTests.push_back(mnorm.normalize(r));
        auto t2e=clock::now(); cout << "[時間] 2) 解析 March tests 並正規化: "<< to_us(t2e-t2s) <<" us (tests="<<marchTests.size()<<")\n";

        // 3) output
        std::ofstream ofs(opt.output_html); if (!ofs) throw std::runtime_error("cannot open output html: "+opt.output_html);
        write_html_head(ofs, faults.size(), all_tps.size(), marchTests.size());
        if (!warnings.empty()){ ofs << "<details open><summary>Warnings ("<< warnings.size() <<")</summary><ul>"; for (const auto& w: warnings) ofs << "<li>"<< html_escape(w) <<"</li>"; ofs << "</ul></details>"; }
        write_fault_anchors(ofs, raw_faults);

        FaultSimulatorEvent simulator; // new encapsulated event simulator from header
        auto t3s=clock::now(); long long per_tests_sum_us=0;
        for (size_t mi=0; mi<marchTests.size(); ++mi){ const auto& mt = marchTests[mi]; auto tms=clock::now();
            auto sim = simulator.simulate(mt, faults, all_tps); auto tme=clock::now(); auto us = to_us(tme-tms); per_tests_sum_us += us;
            // total coverage（沿用舊版：等於 detect_coverage across faults 的平均）
            // 蒐集被偵測到的 tp 集合
            unordered_set<size_t> detected_tp_set;
            for (const auto& bucket : sim.events.detectDone()){ for (auto eid : bucket) detected_tp_set.insert(sim.events.events()[eid].tp_gid()); }
            // 計算每個 fault 的 coverage
            double sum_fault_cov = 0.0;
            for (const auto& f : faults){
                bool has_any=false, has_lt=false, has_gt=false;
                for (size_t tg=0; tg<all_tps.size(); ++tg){
                    if (all_tps[tg].parent_fault_id != f.fault_id) continue;
                    if (detected_tp_set.count(tg)==0) continue;
                    auto g = all_tps[tg].group;
                    if (g==OrientationGroup::Single) has_any=true;
                    else if (g==OrientationGroup::A_LT_V) has_lt=true;
                    else if (g==OrientationGroup::A_GT_V) has_gt=true;
                }
                double cov = 0.0;
                if (f.cell_scope==CellScope::SingleCell) cov = has_any?1.0:0.0;
                else cov = (has_lt?0.5:0.0) + (has_gt?0.5:0.0);
                sum_fault_cov += cov;
            }
            double avg_detect = faults.empty()? 0.0 : (sum_fault_cov / (double)faults.size());
            std::ostringstream covss; covss.setf(std::ios::fixed); covss<< std::setprecision(2) << (avg_detect*100.0) << "%";
            ofs << "<details open><summary>March Test: "<< html_escape(mt.name)
                <<" <span class=\"badge\">ops: "<< sim.op_table.size() <<"</span>"
                <<" <span class=\"badge\">total coverage: "<< covss.str() <<"</span>"
                <<"</summary>\n";

            write_state_table(ofs, sim, all_tps, raw_faults, raw_index_by_id, faults);
            write_sens_table (ofs, sim, all_tps, raw_faults, raw_index_by_id, faults);
            write_detect_table(ofs, sim, all_tps, raw_faults, raw_index_by_id, faults);
            write_faults_coverage_table(ofs, sim, faults, all_tps, raw_faults, raw_index_by_id);
            // ============= 新增：輸出尚未覆蓋的 TP 群組與其成員的 TPEvent 詳細資訊 =============
            {
                // 蒐集已偵測 TP 並標記群組
                std::vector<bool> group_detected(sim.tp_group.total_groups(), false);
                const auto& ddone = sim.events.detectDone();
                for (size_t op=0; op<ddone.size(); ++op){
                    for (auto eid : ddone[op]){
                        const auto& ev = sim.events.events()[eid];
                        int gid = sim.tp_group.group_of_tp(ev.tp_gid());
                        if (gid >= 0 && gid < (int)group_detected.size()) group_detected[gid] = true;
                    }
                }
                // 桶：群組 -> TP 列表
                std::vector<std::vector<size_t>> group_members(sim.tp_group.total_groups());
                for (size_t tp_gid=0; tp_gid<all_tps.size(); ++tp_gid){
                    int g = sim.tp_group.group_of_tp(tp_gid);
                    if (g>=0 && g<(int)group_members.size()) group_members[g].push_back(tp_gid);
                }
                // 未覆蓋群組列表
                std::vector<int> uncovered_groups;
                for (int g=0; g<(int)group_detected.size(); ++g){ if (!group_detected[g]) uncovered_groups.push_back(g); }
                ofs << "<details><summary>Uncovered TP Groups ("<< uncovered_groups.size() <<")</summary>";
                if (uncovered_groups.empty()){
                    ofs << "<p>All groups covered.</p>";
                } else {
                    ofs << "<table class=striped><thead><tr><th>GroupId</th><th>Members (details)</th><th>Events</th></tr></thead><tbody>";
                    for (int gid : uncovered_groups){
                        ofs << "<tr><td>"<< gid <<"</td><td style='text-align:left'>";
                        const auto& members = group_members[gid];
                        for (auto tp_gid : members){
                            if (tp_gid >= all_tps.size()) continue;
                            const auto& tp = all_tps[tp_gid];
                            const RawFault* rf = nullptr; if (auto it = raw_index_by_id.find(tp.parent_fault_id); it!=raw_index_by_id.end()) rf = &raw_faults[it->second];
                            ofs << "<details><summary>tp "<< tp_gid <<"</summary>";
                            // 重新使用 write_tp_details 顯示 Fault / Primitive / Group / State / Ops / Detector
                            write_tp_details(ofs, tp, rf, true, true, true);
                            ofs << "</details>";
                        }
                        ofs << "</td><td>";
                        // 對每個成員列出其所有事件
                        for (auto tp_gid : members){
                            ofs << "<details><summary>tp "<< tp_gid <<"</summary>";
                            // 事件 IDs
                            if (tp_gid < sim.events.tp2events().size()){
                                const auto& eids = sim.events.tp2events()[tp_gid];
                                if (eids.empty()){
                                    ofs << "<div class=tpd><em>No events</em></div>";
                                } else {
                                    ofs << "<table style='margin:4px 0;border-collapse:collapse' class='inner'><thead><tr><th>EvtId</th><th>Status</th><th>StateOp</th><th>SensOps</th><th>DetectOp</th><th>MaskOp</th></tr></thead><tbody>";
                                    for (auto eid : eids){
                                        if (eid >= sim.events.events().size()) continue;
                                        const auto& tev = sim.events.events()[eid];
                                        ofs << "<tr><td>"<< eid <<"</td><td>"<< tpe_status_str(tev.final_status()) <<"</td><td>"<< tev.state_op() <<"</td><td>";
                                        if (tev.sens_ops().empty()) ofs << "-"; else { for (size_t si=0; si<tev.sens_ops().size(); ++si){ if(si) ofs<<","; ofs<< tev.sens_ops()[si]; } }
                                        ofs << "</td><td>"<< (tev.det_op()>=0?std::to_string(tev.det_op()):string("-")) <<"</td><td>"<< (tev.mask_op()>=0?std::to_string(tev.mask_op()):string("-")) <<"</td></tr>";
                                    }
                                    ofs << "</tbody></table>";
                                }
                            } else {
                                ofs << "<div class=tpd><em>tp2events missing</em></div>";
                            }
                            ofs << "</details>";
                        }
                        ofs << "</td></tr>";
                    }
                    ofs << "</tbody></table>";
                }
                ofs << "</details>";
            }
            // ================================================================
            ofs << "</details>\n";
            cout << "[時間] 3) 模擬+輸出 March Test '"<< mt.name <<"': "<< us <<" us (ops="<< sim.op_table.size() <<")\n";
        }
        auto t3e=clock::now(); cout << "[時間] 3) 執行時間(包含撰寫報告)總耗時: "<< to_us(t3e-t3s) <<" us (單測累計="<< per_tests_sum_us <<" us)\n";
        ofs << "</body></html>\n"; ofs.close();
        cout << "HTML report written to: "<< opt.output_html <<"\n";
    } catch(const std::exception& e){ std::cerr << "FaultSimulationEvent error: "<< e.what() <<"\n"; return 1; }
    return 0;
}
