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

#include "../include/FaultSimulator.hpp"

using std::string; using std::vector; using std::cout; using std::endl; using std::unordered_map; using std::unordered_set;

// =============================
// TPEvent + Event Center
// =============================

struct TPEvent {
    using EventId = size_t;
    enum class Status { Stated, Sensitized, Detected, StateMasked, SensMasked, DetectMasked };

    TpGid tp_gid{};
    EventId id{};
    OpId state_op{-1};
    vector<OpId> sens_ops;     // 完成致敏的 op（舊式引擎：最多一個；DontNeedSens 用 state_op）
    int det_op{-1};
    int mask_op{-1};           // 記錄第一個遮蔽點（sens/detect）
    Status final_status{Status::Stated};

    bool is_sens_done() const { return !sens_ops.empty(); }

    void mark_state_mask(OpId op){ final_status = Status::StateMasked; mask_op = op; }
    void add_sens_complete(OpId op){
        if (sens_ops.empty()) final_status = Status::Sensitized;
        sens_ops.push_back(op);
    }
    void mark_sens_mask(OpId op){ if (!is_sens_done()) { final_status = Status::SensMasked; mask_op = op; } }
    void mark_detected(OpId op){ det_op = op; final_status = Status::Detected; }
    void mark_detect_mask(OpId op){ if (final_status != Status::Detected) { final_status = Status::DetectMasked; mask_op = op; } }
};

struct TPEventCenter {
    // data
    vector<TPEvent> events;
    vector<vector<TPEvent::EventId>> state_begins;   // per-op
    vector<vector<TPEvent::EventId>> sens_done;      // per-op
    vector<vector<TPEvent::EventId>> detect_done;    // per-op
    vector<vector<TPEvent::EventId>> sens_masked;    // per-op
    vector<vector<TPEvent::EventId>> detect_masked;  // per-op

    vector<vector<TPEvent::EventId>> tp2events;      // per-tp

    void init(size_t op_count, size_t tp_count){
        events.clear();
        state_begins.assign(op_count, {});
        sens_done.assign(op_count, {});
        detect_done.assign(op_count, {});
        sens_masked.assign(op_count, {});
        detect_masked.assign(op_count, {});
        tp2events.assign(tp_count, {});
    }

    TPEvent::EventId start_state(TpGid tp, OpId op){
        TPEvent::EventId id = events.size();
        TPEvent e; e.tp_gid = tp; e.id = id; e.state_op = op; e.final_status = TPEvent::Status::Stated;
        events.push_back(e);
        state_begins[op].push_back(id);
        tp2events[tp].push_back(id);
        return id;
    }
    void add_sens_complete(TPEvent::EventId id, OpId op){ events[id].add_sens_complete(op); sens_done[op].push_back(id); }
    void mask_sens(TPEvent::EventId id, OpId op){ events[id].mark_sens_mask(op); sens_masked[op].push_back(id); }
    void set_detect(TPEvent::EventId id, OpId op){ events[id].mark_detected(op); detect_done[op].push_back(id); }
    void mask_detect(TPEvent::EventId id, OpId op){ events[id].mark_detect_mask(op); detect_masked[op].push_back(id); }

    // accumulate tp gids up to op index for a stage
    enum class Stage { State, Sens, Detect };
    vector<TpGid> accumulate_tp_gids_upto(size_t op_idx, Stage s) const {
        unordered_set<TpGid> set;
        auto add = [&](const vector<vector<TPEvent::EventId>>& buckets){
            size_t n = std::min(op_idx+1, buckets.size());
            for(size_t i=0;i<n;++i){ for(auto id: buckets[i]) set.insert(events[id].tp_gid); }
        };
        switch(s){
            case Stage::State:  add(state_begins); break;
            case Stage::Sens:   add(sens_done);    break;
            case Stage::Detect: add(detect_done);  break;
        }
        return vector<TpGid>(set.begin(), set.end());
    }
};

// =============================
// FaultSimulatorEvent — 沿用舊引擎，改以 TPEvent 記錄
// =============================

struct SimulationEventResult {
    vector<OpContext> op_table;
    TPEventCenter events;
};

class FaultSimulatorEvent {
public:
    SimulationEventResult simulate(const MarchTest& mt, const vector<Fault>& faults, const vector<TestPrimitive>& tps);
private:
    OpTableBuilder op_table_builder;
    StateCoverEngine state_cover_engine;
    SensEngine sens_engine;
    DetectEngine detect_engine;
};

SimulationEventResult FaultSimulatorEvent::simulate(const MarchTest& mt, const vector<Fault>& faults, const vector<TestPrimitive>& tps){
    SimulationEventResult out;
    if (mt.elements.empty() || faults.empty()) return out;
    // 1) op table
    out.op_table = op_table_builder.build(mt);
    // 2) buckets for state
    state_cover_engine.build_tp_buckets(tps);
    // 3) events
    out.events.init(out.op_table.size(), tps.size());

    for (size_t op_id = 0; op_id < out.op_table.size(); ++op_id){
        // state
        auto state_tps = state_cover_engine.cover(out.op_table[op_id].pre_state_key);
        for (auto tp_gid : state_tps){
            auto evt = out.events.start_state(tp_gid, (OpId)op_id);
            // sens
            auto sens_res = sens_engine.cover(out.op_table, (OpId)op_id, tps[tp_gid]);
            if (sens_res.status == SensOutcome::Status::SensNone) {
                continue; // 舊版：未致敏就不偵測
            }
            if (sens_res.status == SensOutcome::Status::SensPartial) {
                if (sens_res.sens_mask_at_op >= 0 && sens_res.sens_mask_at_op < (int)out.op_table.size())
                    out.events.mask_sens(evt, sens_res.sens_mask_at_op);
                continue;
            }
            // SensAll 或 DontNeedSens 都視為完成致敏，使用 sens_end_op
            out.events.add_sens_complete(evt, sens_res.sens_end_op);

            // detect
            auto det_res = detect_engine.cover(out.op_table, sens_res.sens_end_op, tps[tp_gid]);
            if (det_res.status == DetectOutcome::Status::Found && det_res.det_op >= 0)
                out.events.set_detect(evt, det_res.det_op);
            else if (det_res.status == DetectOutcome::Status::MaskedOnD && det_res.mask_at_op >= 0)
                out.events.mask_detect(evt, det_res.mask_at_op);
        }
    }
    return out;
}

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
        unique_tp.reserve(sim.events.state_begins[i].size());
        for (auto eid : sim.events.state_begins[i]){
            size_t gid = sim.events.events[eid].tp_gid; if (seen.insert(gid).second) unique_tp.push_back(gid);
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
        unordered_set<size_t> seen; vector<size_t> unique_tp; unique_tp.reserve(sim.events.sens_done[i].size());
        for (auto eid : sim.events.sens_done[i]){ size_t gid = sim.events.events[eid].tp_gid; if (seen.insert(gid).second) unique_tp.push_back(gid); }
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
        unordered_set<size_t> seen; vector<size_t> unique_tp; unique_tp.reserve(sim.events.detect_done[i].size());
        for (auto eid : sim.events.detect_done[i]){ size_t gid = sim.events.events[eid].tp_gid; if (seen.insert(gid).second) unique_tp.push_back(gid); }
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
    for (const auto& bucket : sim.events.detect_done){ for (auto eid : bucket) detected_tp_set.insert(sim.events.events[eid].tp_gid); }

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

        FaultSimulatorEvent simulator;
        auto t3s=clock::now(); long long per_tests_sum_us=0;
        for (size_t mi=0; mi<marchTests.size(); ++mi){ const auto& mt = marchTests[mi]; auto tms=clock::now();
            auto sim = simulator.simulate(mt, faults, all_tps); auto tme=clock::now(); auto us = to_us(tme-tms); per_tests_sum_us += us;
            // total coverage（沿用舊版：等於 detect_coverage across faults 的平均）
            // 蒐集被偵測到的 tp 集合
            unordered_set<size_t> detected_tp_set;
            for (const auto& bucket : sim.events.detect_done){ for (auto eid : bucket) detected_tp_set.insert(sim.events.events[eid].tp_gid); }
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
            ofs << "</details>\n";
            cout << "[時間] 3) 模擬+輸出 March Test '"<< mt.name <<"': "<< us <<" us (ops="<< sim.op_table.size() <<")\n";
        }
        auto t3e=clock::now(); cout << "[時間] 3) 執行時間(包含撰寫報告)總耗時: "<< to_us(t3e-t3s) <<" us (單測累計="<< per_tests_sum_us <<" us)\n";
        ofs << "</body></html>\n"; ofs.close();
        cout << "HTML report written to: "<< opt.output_html <<"\n";
    } catch(const std::exception& e){ std::cerr << "FaultSimulationEvent error: "<< e.what() <<"\n"; return 1; }
    return 0;
}
