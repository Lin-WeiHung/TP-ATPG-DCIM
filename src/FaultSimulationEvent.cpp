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
static const char* pos2s(PositionMark p){ switch(p){ case PositionMark::Adjacent: return "#"; case PositionMark::SameElementHead: return "^"; case PositionMark::NextElementHead: return ";";} return "?"; }
static const char* group2short(OrientationGroup g){ switch(g){ case OrientationGroup::Single: return "single"; case OrientationGroup::A_LT_V: return "a&lt;v"; case OrientationGroup::A_GT_V: return "a&gt;v";} return "?"; }
static string html_escape(const string& in){ string out; out.reserve(in.size()); for(char c: in){ switch(c){ case '&': out+="&amp;"; break; case '<': out+="&lt;"; break; case '>': out+="&gt;"; break; case '"': out+="&quot;"; break; case '\'': out+="&#39;"; break; default: out.push_back(c); } } return out; }
static string make_anchor_id(const string& raw){ string id; id.reserve(raw.size()); for(char c: raw){ if ((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')) id.push_back(c); else id.push_back('-'); } return id; }
static string op_repr(const Op& op){ auto bit=[](Val v){ return v==Val::Zero?'0':v==Val::One?'1':'-'; }; if (op.kind==OpKind::Write){ if (op.value==Val::Zero) return string("W0"); if (op.value==Val::One) return string("W1"); return string("W-"); } else if (op.kind==OpKind::Read){ if (op.value==Val::Zero) return string("R0"); if (op.value==Val::One) return string("R1"); return string("R-"); } else { std::string s="C("; s.push_back(bit(op.C_T)); s+=")("; s.push_back(bit(op.C_M)); s+=")("; s.push_back(bit(op.C_B)); s+=")"; return s; } }
static string detect_repr(const Detector& d){ if (d.detectOp.kind==OpKind::Read){ if (d.detectOp.value==Val::Zero) return "R0"; if (d.detectOp.value==Val::One) return "R1"; return string("R-"); } if (d.detectOp.kind==OpKind::ComputeAnd){ auto b=[](Val v){ return v==Val::Zero?'0':v==Val::One?'1':'-'; }; std::string s="C("; s.push_back(b(d.detectOp.C_T)); s+=")("; s.push_back(b(d.detectOp.C_M)); s+=")("; s.push_back(b(d.detectOp.C_B)); s+=")"; return s; } return string("?"); }

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

static void write_progress_bar(std::ostream& os, double percent){
    os << "<div class=\"progress-container\"><div class=\"progress-bar\" style=\"width:" << percent << "%\"></div></div>";
    os << "<span class=\"cov-text\">" << std::fixed << std::setprecision(2) << percent << "%</span>";
}

static void write_faults_tab(std::ostream& os,
                                        const SimulationEventResult& sim,
                                        const vector<Fault>& faults,
                                        const vector<TestPrimitive>& tps,
                                        const vector<RawFault>& raw_faults,
                                        const unordered_map<string, size_t>& raw_index_by_id,
                                        size_t march_idx){
    // 蒐集被偵測到的 tp 集合（以 tp_gid 為鍵）
    unordered_set<size_t> detected_tp_set;
    for (const auto& bucket : sim.events.detectDone()){ for (auto eid : bucket) detected_tp_set.insert(sim.events.events()[eid].tp_gid()); }

    string tableId = "faultsTable-" + std::to_string(march_idx);
    string inputId = "faultsInput-" + std::to_string(march_idx);

    os << "<h3>Fault Coverage Summary ("<< faults.size() <<")</h3>";
    os << "<input type=\"text\" id=\""<< inputId <<"\" onkeyup=\"filterTable('"<< inputId <<"', '"<< tableId <<"')\" placeholder=\"Search for faults...\" class=\"filter-input\">";
    os << "<table class=\"striped\" id=\""<< tableId <<"\"><thead><tr><th>#</th><th>Fault ID</th><th>Coverage</th><th>TPs</th></tr></thead><tbody>";

    for (size_t fi=0; fi<faults.size(); ++fi){
        const auto& f = faults[fi];
        // 計算 coverage（依該 fault 的 tp 分組）
        bool has_any=false, has_lt=false, has_gt=false;
        for (size_t tg=0; tg<tps.size(); ++tg){ if (tps[tg].parent_fault_id!=f.fault_id) continue; if (detected_tp_set.count(tg)==0) continue; auto g=tps[tg].group; if (g==OrientationGroup::Single) has_any=true; else if (g==OrientationGroup::A_LT_V) has_lt=true; else if (g==OrientationGroup::A_GT_V) has_gt=true; }
        double cov = 0.0; if (f.cell_scope==CellScope::SingleCell){ cov = has_any?1.0:0.0; } else { cov = (has_lt?0.5:0.0) + (has_gt?0.5:0.0); }
        
        const char* covCls = ""; if (std::abs(cov - 0.0) < 1e-9) covCls = "cov0"; else if (std::abs(cov - 0.5) < 1e-9) covCls = "cov50";
        os << "<tr><td>"<< fi <<"</td><td>"<< html_escape(f.fault_id) <<"</td><td class=\""<< covCls <<"\">";
        write_progress_bar(os, cov*100.0);
        os << "</td><td>";

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
    os << "</tbody></table>";
}

static void write_events_tab(std::ostream& ofs, const SimulationEventResult& sim,
                             const vector<TestPrimitive>& all_tps,
                             const vector<RawFault>& raw_faults,
                             const unordered_map<string, size_t>& raw_index_by_id){
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
    
    ofs << "<h3>Uncovered TP Groups ("<< uncovered_groups.size() <<")</h3>";
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
}

// =============================
// CLI & page head
// =============================

struct CmdOptions { string faults_json; string march_json; string output_html; };
static bool parse_args(int argc, char** argv, CmdOptions& opt){ if (argc!=4){ std::cerr << "Usage: " << (argc>0? argv[0]:"FaultSimulationEvent") << " <faults.json> <MarchTest.json> <output.html>\n"; return false; } opt.faults_json=argv[1]; opt.march_json=argv[2]; opt.output_html=argv[3]; return true; }
static void write_html_head(std::ostream& ofs, size_t faults_n, size_t tps_n, size_t mts_n){
    ofs << "<!DOCTYPE html><html><head><meta charset=\"utf-8\">\n";
    ofs << "<title>March Simulation Report</title>\n";
    ofs << "<style>\n"
           ":root {\n"
           "    --bg-color: #1e1e1e;\n"
           "    --text-color: #d4d4d4;\n"
           "    --link-color: #3794ff;\n"
           "    --border-color: #444;\n"
           "    --table-head-bg: #252526;\n"
           "    --table-row-even: #2d2d2d;\n"
           "    --table-row-odd: #1e1e1e;\n"
           "    --code-bg: #2d2d2d;\n"
           "    --accent-color: #007acc;\n"
           "    --success-color: #4ec9b0;\n"
           "    --warning-color: #ce9178;\n"
           "    --danger-color: #f44747;\n"
           "    --tab-active-bg: #3e3e42;\n"
           "    --tab-inactive-bg: #2d2d2d;\n"
           "}\n"
           "body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; background-color: var(--bg-color); color: var(--text-color); margin: 0; padding: 20px; }\n"
           "a { color: var(--link-color); text-decoration: none; }\n"
           "a:hover { text-decoration: underline; }\n"
           "h1, h2, h3 { color: var(--text-color); }\n"
           ".muted { color: #888; font-size: 0.9em; }\n"
           ".badge { display: inline-block; background: var(--accent-color); color: white; border-radius: 4px; padding: 2px 6px; font-size: 12px; margin-left: 8px; }\n"
           
           /* Tables */
           "table { border-collapse: collapse; width: 100%; margin: 10px 0; font-size: 14px; }\n"
           "th, td { border: 1px solid var(--border-color); padding: 8px; text-align: left; }\n"
           "th { background-color: var(--table-head-bg); position: sticky; top: 0; z-index: 10; }\n"
           "tr:nth-child(even) { background-color: var(--table-row-even); }\n"
           "tr:nth-child(odd) { background-color: var(--table-row-odd); }\n"
           
           /* Tabs */
           ".tab-container { margin-top: 15px; border: 1px solid var(--border-color); border-radius: 4px; overflow: hidden; }\n"
           ".tab-header { display: flex; background-color: var(--tab-inactive-bg); border-bottom: 1px solid var(--border-color); }\n"
           ".tab-btn { background: none; border: none; outline: none; cursor: pointer; padding: 10px 20px; color: var(--text-color); font-size: 14px; transition: 0.3s; }\n"
           ".tab-btn:hover { background-color: #3e3e42; }\n"
           ".tab-btn.active { background-color: var(--tab-active-bg); border-bottom: 2px solid var(--accent-color); font-weight: bold; }\n"
           ".tab-content { display: none; padding: 15px; background-color: var(--bg-color); animation: fadeEffect 0.5s; }\n"
           ".tab-content.active { display: block; }\n"
           "@keyframes fadeEffect { from {opacity: 0;} to {opacity: 1;} }\n"

           /* Progress Bar */
           ".progress-container { background-color: #444; border-radius: 4px; width: 100px; height: 10px; display: inline-block; vertical-align: middle; margin-right: 8px; overflow: hidden; }\n"
           ".progress-bar { height: 100%; background-color: var(--success-color); }\n"
           ".cov-text { font-weight: bold; }\n"

           /* Filtering */
           ".filter-input { background-color: var(--code-bg); border: 1px solid var(--border-color); color: var(--text-color); padding: 6px; border-radius: 4px; margin-bottom: 10px; width: 200px; }\n"

           /* Utils */
           ".state { font-family: 'Consolas', monospace; color: #9cdcfe; }\n"
           ".tpd { margin: 4px 0 4px 12px; border-left: 2px solid var(--border-color); padding-left: 8px; }\n"
           "details { margin: 5px 0; }\n"
           "summary { cursor: pointer; font-weight: 600; padding: 4px; }\n"
           "summary:hover { background-color: #333; }\n"
           
           /* Dashboard Layout */
           ".march-section { border: 1px solid var(--border-color); margin-bottom: 30px; background: var(--code-bg); border-radius: 6px; overflow: hidden; box-shadow: 0 4px 6px rgba(0,0,0,0.3); }\n"
           ".march-header { padding: 10px 15px; background: #333; color: #fff; font-weight: bold; display: flex; justify-content: space-between; align-items: center; cursor: pointer; }\n"
           ".dashboard-row { display: flex; transition: all 0.3s ease; height: 600px; overflow: hidden; }\n" /* Fixed height for scrolling lists */
           ".dashboard-row.collapsed { height: 0; }\n"
           
           /* Left Stats Column */
           ".stats-col { width: 250px; padding: 20px; background: #1e1e1e; border-right: 1px solid var(--border-color); display: flex; flex-direction: column; gap: 25px; flex-shrink: 0; overflow-y: auto; transition: all 0.3s ease; }\n"
           ".stat-item { text-align: left; }\n"
           ".stat-label { font-size: 13px; color: #aaa; margin-bottom: 8px; text-transform: uppercase; letter-spacing: 0.5px; }\n"
           ".stat-value { font-size: 28px; font-weight: bold; color: var(--success-color); margin-bottom: 5px; }\n"
           ".stat-sub { font-size: 12px; color: #666; }\n"
           
           /* Middle Ops Column */
           ".ops-col { flex: 1; padding: 0; background: var(--bg-color); overflow-y: auto; position: relative; display: flex; flex-direction: column; transition: all 0.3s ease; }\n"
           ".ops-list { flex: 1; overflow-y: auto; }\n"
           ".op-row { display: flex; align-items: center; padding: 10px 15px; border-bottom: 1px solid var(--border-color); transition: background 0.2s; }\n"
           ".op-row:hover { background: #2a2d2e; }\n"
           ".op-row.active { background: #37373d; border-left: 4px solid var(--accent-color); }\n"
           ".op-icon { width: 40px; text-align: center; font-size: 20px; color: #569cd6; margin-right: 10px; }\n"
           ".op-info { flex: 1; font-family: 'Consolas', monospace; font-size: 14px; }\n"
           ".op-btn { background: #0e639c; color: white; border: none; padding: 6px 14px; border-radius: 15px; cursor: pointer; font-size: 12px; font-weight: 600; transition: background 0.2s; }\n"
           ".op-btn:hover { background: #1177bb; transform: scale(1.05); }\n"
           
           /* Right Details Column (Initially Hidden) */
           ".details-col { width: 0; border-left: 1px solid var(--border-color); background: #1e1e1e; transition: all 0.3s ease; display: flex; flex-direction: column; overflow: hidden; }\n"
           ".details-header { padding: 10px; background: var(--table-head-bg); border-bottom: 1px solid var(--border-color); }\n"
           ".details-content { flex: 1; overflow-y: auto; padding: 15px; }\n"
           
           /* Split View Layout Changes */
           ".dashboard-row.split-active .stats-col { width: 180px; padding: 10px; }\n"
           ".dashboard-row.split-active .stats-col .stat-label { font-size: 10px; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }\n"
           ".dashboard-row.split-active .stats-col .stat-value { font-size: 18px; }\n"
           ".dashboard-row.split-active .stats-col .progress-container { width: 100%; margin-right: 0; }\n"
           ".dashboard-row.split-active .stats-col .stat-sub { display: none; }\n"
           ".dashboard-row.split-active .ops-col { flex: 0 0 500px; }\n"
           ".dashboard-row.split-active .details-col { flex: 1; width: auto; }\n"
           
           /* Fault Analysis Toggle */
           ".fa-toggle-container { padding: 15px; border-top: 1px solid var(--border-color); text-align: right; background: #252526; }\n"
           ".fa-btn { background: #333; color: #ddd; border: 1px solid #555; padding: 8px 16px; cursor: pointer; border-radius: 4px; font-size: 13px; }\n"
           ".fa-btn:hover { background: #444; }\n"

           /* Fault Analysis Panel */
           ".fa-panel { display: none; padding: 20px; background: var(--bg-color); border-top: 1px solid var(--border-color); }\n"
           ".fa-panel.active { display: block; }\n"
           ".back-btn { margin-bottom: 15px; background: #333; color: #fff; border: none; padding: 8px 16px; cursor: pointer; border-radius: 4px; }\n"
           
           /* Filter Tabs in Details */
           ".filter-tabs { display: flex; gap: 5px; margin-bottom: 10px; flex-wrap: wrap; }\n"
           ".filter-tab { background: #2d2d2d; border: 1px solid #444; color: #aaa; padding: 4px 10px; cursor: pointer; font-size: 12px; border-radius: 3px; }\n"
           ".filter-tab.active { background: var(--accent-color); color: white; border-color: var(--accent-color); }\n"
           
           /* Element Grouping */
           ".elem-group { display: flex; border-bottom: 1px solid var(--border-color); }\n"
           ".elem-marker { width: 30px; background: #252526; display: flex; align-items: center; justify-content: center; border-right: 1px solid var(--border-color); font-weight: bold; color: #888; writing-mode: vertical-rl; transform: rotate(180deg); }\n"
           ".elem-ops { flex: 1; display: flex; flex-direction: column; }\n"
           ".op-row { display: flex; align-items: center; padding: 8px 15px; border-bottom: 1px solid #333; transition: background 0.2s; height: 40px; }\n"
           ".op-row:last-child { border-bottom: none; }\n"
           ".op-row:hover { background: #2a2d2e; }\n"
           ".op-row.active { background: #37373d; }\n"
           
           /* State Visualization */
           ".state-viz { display: flex; gap: 2px; margin-right: 15px; }\n"
           ".state-box { width: 30px; height: 24px; background: #333; border: 1px solid #555; display: flex; align-items: center; justify-content: center; font-size: 10px; color: #ccc; font-family: monospace; }\n"
           ".state-box.active { border-color: #007acc; color: white; }\n"
           
           /* Details Header */
           ".details-header { padding: 10px; background: var(--table-head-bg); border-bottom: 1px solid var(--border-color); display: flex; justify-content: space-between; align-items: center; height: 40px; }\n"
           ".details-title { font-weight: bold; display: flex; align-items: center; gap: 10px; }\n"
           ".close-btn { background: none; border: none; color: #aaa; font-size: 20px; cursor: pointer; padding: 0 5px; }\n"
           ".close-btn:hover { color: white; }\n"
           "</style>\n";
    
    ofs << "<script>\n"
           "function showOpDetails(marchIdx, opIdx) {\n"
           "  // 1. Activate Split Mode\n"
           "  document.getElementById('dash-row-' + marchIdx).classList.add('split-active');\n"
           "  document.getElementById('det-col-' + marchIdx).classList.add('active');\n"
           "  // 2. Highlight Row\n"
           "  var rows = document.querySelectorAll('#ops-list-' + marchIdx + ' .op-row');\n"
           "  rows.forEach(r => r.classList.remove('active'));\n"
           "  var activeRow = document.getElementById('op-row-' + marchIdx + '-' + opIdx);\n"
           "  activeRow.classList.add('active');\n"
           "  // 3. Update Header Info (Copy from row)\n"
           "  var headerContent = document.getElementById('det-header-content-' + marchIdx);\n"
           "  var stateViz = activeRow.querySelector('.state-viz').outerHTML;\n"
           "  var opText = activeRow.querySelector('.op-text').innerText;\n"
           "  headerContent.innerHTML = stateViz + ' <span style=\"font-family:monospace;font-size:14px;margin-left:10px\">' + opText + '</span>';\n"
           "  // 4. Show Specific Content\n"
           "  var contents = document.querySelectorAll('#det-col-' + marchIdx + ' .op-detail-content');\n"
           "  contents.forEach(c => c.style.display = 'none');\n"
           "  var target = document.getElementById('op-detail-' + marchIdx + '-' + opIdx);\n"
           "  if(target) target.style.display = 'block';\n"
           "  // 5. Default to 'All' tab\n"
           "  filterOpEvents(marchIdx, opIdx, 'All');\n"
           "}\n"
           "function closeOpDetails(marchIdx) {\n"
           "  document.getElementById('dash-row-' + marchIdx).classList.remove('split-active');\n"
           "  document.getElementById('det-col-' + marchIdx).classList.remove('active');\n"
           "  var rows = document.querySelectorAll('#ops-list-' + marchIdx + ' .op-row');\n"
           "  rows.forEach(r => r.classList.remove('active'));\n"
           "}\n"
           "function toggleFaultAnalysis(marchIdx) {\n"
           "  var dashRow = document.getElementById('dash-row-' + marchIdx);\n"
           "  var faPanel = document.getElementById('fa-panel-' + marchIdx);\n"
           "  if (faPanel.classList.contains('active')) {\n"
           "    faPanel.classList.remove('active');\n"
           "    dashRow.classList.remove('collapsed');\n"
           "  } else {\n"
           "    faPanel.classList.add('active');\n"
           "    dashRow.classList.add('collapsed');\n"
           "  }\n"
           "}\n"
           "function filterOpEvents(marchIdx, opIdx, type) {\n"
           "  // Update tabs UI\n"
           "  var container = document.getElementById('op-detail-' + marchIdx + '-' + opIdx);\n"
           "  var tabs = container.querySelectorAll('.filter-tab');\n"
           "  tabs.forEach(t => {\n"
           "    if(t.innerText === type) t.classList.add('active');\n"
           "    else t.classList.remove('active');\n"
           "  });\n"
           "  // Filter rows\n"
           "  var rows = container.querySelectorAll('tbody tr');\n"
           "  rows.forEach(r => {\n"
           "    if (type === 'All' || r.getAttribute('data-type') === type) r.style.display = '';\n"
           "    else r.style.display = 'none';\n"
           "  });\n"
           "}\n"
           "</script>\n";
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
            
            // --- Coverage Calculation ---
            unordered_set<size_t> detected_tp_set;
            for (const auto& bucket : sim.events.detectDone()){ for (auto eid : bucket) detected_tp_set.insert(sim.events.events()[eid].tp_gid()); }
            
            double sum_fault_cov = 0.0;
            double sum_single_cov = 0.0; size_t count_single = 0;
            double sum_two_cov = 0.0; size_t count_two = 0;

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
                if (f.cell_scope==CellScope::SingleCell){ sum_single_cov += cov; count_single++; }
                else { sum_two_cov += cov; count_two++; }
            }
            double avg_detect = faults.empty()? 0.0 : (sum_fault_cov / (double)faults.size());
            double avg_single = count_single? (sum_single_cov / (double)count_single) : 0.0;
            double avg_two = count_two? (sum_two_cov / (double)count_two) : 0.0;

            // --- Event Bucketing by Op ---
            struct OpEvt { size_t eid; const char* type; };
            vector<vector<OpEvt>> events_by_op(sim.op_table.size());
            const auto& evs = sim.events.events();
            for (size_t eid=0; eid<evs.size(); ++eid){
                const auto& e = evs[eid];
                if (e.state_op() >= 0 && (size_t)e.state_op() < events_by_op.size()) events_by_op[e.state_op()].push_back({eid, "Stated"});
                for (auto sop : e.sens_ops()){ if (sop >= 0 && (size_t)sop < events_by_op.size()) events_by_op[sop].push_back({eid, "Sensitized"}); }
                if (e.det_op() >= 0 && (size_t)e.det_op() < events_by_op.size()) events_by_op[e.det_op()].push_back({eid, "Detected"});
                if (e.mask_op() >= 0 && (size_t)e.mask_op() < events_by_op.size()){
                    auto s = e.final_status();
                    const char* t = "Masked";
                    if (s == TPEvent::Status::StateMasked) t = "StateMasked";
                    else if (s == TPEvent::Status::SensMasked) t = "SensMasked";
                    else if (s == TPEvent::Status::DetectMasked) t = "DetectMasked";
                    events_by_op[e.mask_op()].push_back({eid, t});
                }
            }

            // --- HTML Output ---
            ofs << "<div class=\"march-section\">\n";
            ofs << "  <div class=\"march-header\" onclick=\"toggleFaultAnalysis("<<mi<<")\">"
                << "<span>" << html_escape(mt.name) << "</span>"
                << "<span>"
                << "<span class=\"badge\" style=\"background:#555\">" << sim.op_table.size() << " Ops</span>"
                << "<span class=\"badge\">" << std::fixed << std::setprecision(2) << (avg_detect*100.0) << "% Cov</span>"
                << "</span>"
                << "</div>\n";

            // Dashboard Row (Stats + Ops + Details)
            ofs << "  <div class=\"dashboard-row\" id=\"dash-row-"<<mi<<"\">\n";
            
            // 1. Stats Column
            ofs << "    <div class=\"stats-col\">\n";
            auto write_stat = [&](const char* label, double val, size_t count){
                ofs << "<div class=\"stat-item\"><div class=\"stat-label\">"<<label<<"</div>";
                ofs << "<div class=\"stat-value\">"<< std::fixed << std::setprecision(2) << (val*100.0) << "%</div>";
                ofs << "<div class=\"progress-container\"><div class=\"progress-bar\" style=\"width:" << (val*100.0) << "%\"></div></div>";
                ofs << "<span class=\"cov-text\">" << count << " faults</span>";
                ofs << "</div>";
            };
            write_stat("Single Cell Coverage", avg_single, count_single);
            write_stat("Two Cell Coverage", avg_two, count_two);
            write_stat("Total Coverage", avg_detect, faults.size());
            ofs << "<div style=\"margin-top:auto;font-size:16px;font-weight:bold;color:#eee;padding-top:15px;border-top:1px solid #444\">Run Time: "<< us <<" us</div>";
            ofs << "    </div>\n";

            // 2. Ops Column
            ofs << "    <div class=\"ops-col\">\n";
            ofs << "      <div class=\"ops-list\" id=\"ops-list-"<<mi<<"\">\n";
            
            // Group ops by element
            for (size_t i=0; i<sim.op_table.size(); ){
                int current_elem = sim.op_table[i].elem_index;
                // Find range of this element
                size_t j = i;
                while(j < sim.op_table.size() && sim.op_table[j].elem_index == current_elem) j++;
                
                // Start Elem Group
                ofs << "        <div class=\"elem-group\">\n";
                ofs << "          <div class=\"elem-marker\">Elem "<< (current_elem+1) <<"</div>\n";
                ofs << "          <div class=\"elem-ops\">\n";
                
                for (size_t k=i; k<j; ++k){
                    const auto& oc = sim.op_table[k];
                    string icon = (oc.order==AddrOrder::Up) ? "&uarr;" : (oc.order==AddrOrder::Down) ? "&darr;" : "&updownarrow;";
                    ofs << "            <div class=\"op-row\" id=\"op-row-"<<mi<<"-"<<k<<"\">\n";
                    ofs << "              <div class=\"op-icon\">"<< icon <<"</div>\n";
                    
                    // State Visualization (5 cells)
                    auto mkBox = [&](Val d, Val c, bool active=false){
                        string cls = active ? "state-box active" : "state-box";
                        return "<div class=\""+cls+"\">"+v2s(d)+","+v2s(c)+"</div>";
                    };
                    ofs << "              <div class=\"state-viz\">\n";
                    ofs << mkBox(oc.pre_state.A0.D, oc.pre_state.A0.C);
                    ofs << mkBox(oc.pre_state.A1.D, oc.pre_state.A1.C);
                    ofs << mkBox(oc.pre_state.A2_CAS.D, oc.pre_state.A2_CAS.C, true); // CAS highlighted
                    ofs << mkBox(oc.pre_state.A3.D, oc.pre_state.A3.C);
                    ofs << mkBox(oc.pre_state.A4.D, oc.pre_state.A4.C);
                    ofs << "              </div>\n";

                    ofs << "              <div class=\"op-info\">\n";
                    ofs << "                <span class=\"op-text\">" << op_repr(oc.op) << "</span>\n";
                    ofs << "              </div>\n";
                    
                    size_t evt_count = events_by_op[k].size();
                    ofs << "              <button class=\"op-btn\" onclick=\"showOpDetails("<<mi<<", "<<k<<")\">"
                        << evt_count << " Events</button>\n";
                    ofs << "            </div>\n"; // end op-row
                }
                
                ofs << "          </div>\n"; // end elem-ops
                ofs << "        </div>\n"; // end elem-group
                
                i = j; // advance
            }

            ofs << "      </div>\n";
            ofs << "      <div class=\"fa-toggle-container\">\n";
            ofs << "        <button class=\"fa-btn\" onclick=\"toggleFaultAnalysis("<<mi<<")\">Fault Analysis &darr;</button>\n";
            ofs << "      </div>\n";
            ofs << "    </div>\n"; // end ops-col

            // 3. Details Column (Hidden)
            ofs << "    <div class=\"details-col\" id=\"det-col-"<<mi<<"\">\n";
            ofs << "      <div class=\"details-header\">\n";
            ofs << "        <div class=\"details-title\" id=\"det-header-content-"<<mi<<"\">Operation Events</div>\n";
            ofs << "        <button class=\"close-btn\" onclick=\"closeOpDetails("<<mi<<")\">&times;</button>\n";
            ofs << "      </div>\n";
            ofs << "      <div class=\"details-content\">\n";
            for (size_t i=0; i<sim.op_table.size(); ++i){
                ofs << "        <div id=\"op-detail-"<<mi<<"-"<<i<<"\" class=\"op-detail-content\" style=\"display:none\">\n";
                if (events_by_op[i].empty()){
                    ofs << "<p class=\"muted\">No events recorded for this operation.</p>";
                } else {
                    // Filter Tabs
                    ofs << "<div class=\"filter-tabs\">";
                    auto mkTab = [&](const char* n){ ofs << "<span class=\"filter-tab\" onclick=\"filterOpEvents("<<mi<<","<<i<<",'"<<n<<"')\">"<<n<<"</span>"; };
                    mkTab("All"); mkTab("Stated"); mkTab("Sensitized"); mkTab("Detected"); 
                    mkTab("StateMasked"); mkTab("SensMasked"); mkTab("DetectMasked");
                    ofs << "</div>";
                    
                    // Table
                    ofs << "<table class=\"striped\"><thead><tr><th>EvtId</th><th>Type</th><th>TP</th><th>Fault</th></tr></thead><tbody>";
                    for (const auto& item : events_by_op[i]){
                        const auto& e = evs[item.eid];
                        const auto& tp = all_tps[e.tp_gid()];
                        ofs << "<tr data-type=\""<< item.type <<"\">";
                        ofs << "<td>"<< item.eid <<"</td>";
                        ofs << "<td><span class=\"badge\" style=\"background:#444\">"<< item.type <<"</span></td>";
                        ofs << "<td><details><summary>TP #"<< e.tp_gid() <<"</summary>";
                        const RawFault* rf = nullptr; if (auto it = raw_index_by_id.find(tp.parent_fault_id); it!=raw_index_by_id.end()) rf = &raw_faults.at(it->second);
                        write_tp_details(ofs, tp, rf, true, true, true);
                        ofs << "</details></td>";
                        ofs << "<td>"<< html_escape(tp.parent_fault_id) <<"</td>";
                        ofs << "</tr>";
                    }
                    ofs << "</tbody></table>";
                }
                ofs << "        </div>\n";
            }
            ofs << "      </div>\n"; // end details-content
            ofs << "    </div>\n"; // end details-col

            ofs << "  </div>\n"; // end dashboard-row

            // Fault Analysis Panel (Initially Hidden)
            ofs << "  <div class=\"fa-panel\" id=\"fa-panel-"<<mi<<"\">\n";
            ofs << "    <button class=\"back-btn\" onclick=\"toggleFaultAnalysis("<<mi<<")\">&uarr; Back to Dashboard</button>\n";
            ofs << "    <div class=\"tab-container\">\n";
            ofs << "      <div class=\"tab-header\">\n";
            ofs << "        <button class=\"tab-btn active\" onclick=\"openTab(event, 'Faults-MT"<<mi<<"')\">Faults</button>\n";
            ofs << "        <button class=\"tab-btn\" onclick=\"openTab(event, 'Events-MT"<<mi<<"')\">Uncovered Events</button>\n";
            ofs << "      </div>\n";
            ofs << "      <div id=\"Faults-MT"<<mi<<"\" class=\"tab-content active\">\n";
            write_faults_tab(ofs, sim, faults, all_tps, raw_faults, raw_index_by_id, mi);
            ofs << "      </div>\n";
            ofs << "      <div id=\"Events-MT"<<mi<<"\" class=\"tab-content\">\n";
            write_events_tab(ofs, sim, all_tps, raw_faults, raw_index_by_id);
            ofs << "      </div>\n";
            ofs << "    </div>\n";
            ofs << "  </div>\n"; // end fa-panel

            ofs << "</div>\n"; // end march-section
            cout << "[時間] 3) 模擬+輸出 March Test '"<< mt.name <<"': "<< us <<" us (ops="<< sim.op_table.size() <<")\n";
        }
        auto t3e=clock::now(); cout << "[時間] 3) 執行時間(包含撰寫報告)總耗時: "<< to_us(t3e-t3s) <<" us (單測累計="<< per_tests_sum_us <<" us)\n";
        ofs << "</body></html>\n"; ofs.close();
        cout << "HTML report written to: "<< opt.output_html <<"\n";
    } catch(const std::exception& e){ std::cerr << "FaultSimulationEvent error: "<< e.what() <<"\n"; return 1; }
    return 0;
}
