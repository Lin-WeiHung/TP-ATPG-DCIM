// MarchSimHtml.cpp — 產生 March 模擬後的 cover lists HTML 報告
// 功能：
// 1) 讀取 faults.json，解析並正規化為 Fault；產生所有 TestPrimitive
// 2) 讀取 MarchTest.json，逐一模擬（使用 FaultSimulator::simulate）
// 3) 每個 MarchTest 產生三張表格：State / Sens / Detect cover
//     - 每列包含該 op 的 CrossState 與 Op
//     - 每列的最後一欄為 cover list，下拉可展開 TP 細節（Fault 連結、Raw primitive、State/ops/detector）
// 設計：HTML 輸出純由下方 writer 函式處理；主流程只負責資料準備與呼叫。

#include <iostream>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <iomanip>

#include "FpParserAndTpGen.hpp"
#include "FaultSimulator.hpp"

using std::string; using std::vector; using std::cout; using std::endl;

// =====================
// 小工具：格式/轉換
// =====================
// 將 Val 轉為 0/1/- 字樣
static const char* v2s(Val v){ switch(v){ case Val::Zero: return "0"; case Val::One: return "1"; case Val::X: return "-"; } return "?"; }
// 將位移順序轉字串
static const char* addr2s(AddrOrder o){ switch(o){ case AddrOrder::Up: return "Up"; case AddrOrder::Down: return "Down"; case AddrOrder::Any: return "Any"; } return "?"; }
// 偵測器位置符號
static const char* pos2s(PositionMark p){ switch(p){ case PositionMark::Adjacent: return "#"; case PositionMark::SameElementHead: return "^"; case PositionMark::NextElementHead: return ";"; } return "?"; }
// 方向群組短字樣
static const char* group2short(OrientationGroup g){ switch(g){ case OrientationGroup::Single: return "single"; case OrientationGroup::A_LT_V: return "a&lt;v"; case OrientationGroup::A_GT_V: return "a&gt;v"; } return "?"; }

// 將字串做 HTML escape
static string html_escape(const string& in){ string out; out.reserve(in.size()); for(char c: in){ switch(c){ case '&': out+="&amp;"; break; case '<': out+="&lt;"; break; case '>': out+="&gt;"; break; case '"': out+="&quot;"; break; case '\'': out+="&#39;"; break; default: out+=c; break; } } return out; }
// 組 anchor id（非英數變成 '-')
static string make_anchor_id(const string& raw){ string id; id.reserve(raw.size()); for(char c: raw){ if ((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')) id.push_back(c); else id.push_back('-'); } return id; }

// OP 轉字串：W0/R1/C(T)(M)(B)
static string op_repr(const Op& op){ auto bit=[](Val v){ return v==Val::Zero?'0':v==Val::One?'1':'-'; }; if (op.kind==OpKind::Write){ if (op.value==Val::Zero) return string("W0"); if (op.value==Val::One) return string("W1"); return string("W-"); } else if (op.kind==OpKind::Read){ if (op.value==Val::Zero) return string("R0"); if (op.value==Val::One) return string("R1"); return string("R-"); } else { std::string s="C("; s.push_back(bit(op.C_T)); s+=")("; s.push_back(bit(op.C_M)); s+=")("; s.push_back(bit(op.C_B)); s+=")"; return s; } }
// 偵測器字串
static string detect_repr(const Detector& d){ if (d.detectOp.kind==OpKind::Read){ if (d.detectOp.value==Val::Zero) return "R0"; if (d.detectOp.value==Val::One) return "R1"; return string("R-"); } if (d.detectOp.kind==OpKind::ComputeAnd){ auto b=[](Val v){ return v==Val::Zero?'0':v==Val::One?'1':'-'; }; std::string s="C("; s.push_back(b(d.detectOp.C_T)); s+=")("; s.push_back(b(d.detectOp.C_M)); s+=")("; s.push_back(b(d.detectOp.C_B)); s+=")"; return s; } return string("?"); }
// CrossState 的 (D,C)
static string state_cell(Val d, Val c){ std::ostringstream os; os<<v2s(d)<<","<<v2s(c); return os.str(); }

// =====================
// HTML writers（單一職責：只輸出 HTML）
// =====================
// 寫出 Fault anchors（供 TP 詳細資訊中的連結指到正確位置）
static void write_fault_anchors(std::ostream& os, const vector<RawFault>& raws){
	os << "<section id=\"fault-anchors\" style=\"display:none\">";
	for (const auto& rf : raws){ os << "<div id=\"fault-"<< make_anchor_id(rf.fault_id) << "\"></div>"; }
	os << "</section>";
}

// 寫出單一 TP 的詳情（依表格性質決定要列印哪些欄位）
// show_state/show_sens/show_detect 用來控制輸出 TP 的 State、ops_before_detect 與 detector
static void write_tp_details(std::ostream& os, size_t tp_gid, const TestPrimitive& tp,
							 const RawFault* raw_fault, bool show_state, bool show_sens, bool show_detect){
	os << "<div class=\"tpd\">";
	// Fault 連結
	if (raw_fault){
		os << "<div><b>Fault:</b> <a href=\"#fault-"<< make_anchor_id(raw_fault->fault_id) <<"\">"
		   << html_escape(raw_fault->fault_id) <<"</a></div>";
		if (tp.parent_fp_index < raw_fault->fp_raw.size()){
			os << "<div><b>Primitive:</b> "<< html_escape(raw_fault->fp_raw[tp.parent_fp_index]) <<"</div>";
		}
	} else {
		os << "<div><b>Fault:</b> "<< html_escape(tp.parent_fault_id) <<"</div>";
	}
	os << "<div><b>Group:</b> "<< group2short(tp.group) <<"</div>";
	if (show_state){
		os << "<div><b>TP State:</b> A0("<< v2s(tp.state.A0.D) <<","<< v2s(tp.state.A0.C) <<") "
		   << "A1("<< v2s(tp.state.A1.D) <<","<< v2s(tp.state.A1.C) <<") "
		   << "CAS("<< v2s(tp.state.A2_CAS.D) <<","<< v2s(tp.state.A2_CAS.C) <<") "
		   << "A3("<< v2s(tp.state.A3.D) <<","<< v2s(tp.state.A3.C) <<") "
		   << "A4("<< v2s(tp.state.A4.D) <<","<< v2s(tp.state.A4.C) <<")</div>";
	}
	if (show_sens){
		std::ostringstream opss; if (tp.ops_before_detect.empty()) opss<<"-"; else { for (size_t i=0;i<tp.ops_before_detect.size();++i){ if(i)opss<<", "; opss<<op_repr(tp.ops_before_detect[i]); } }
		os << "<div><b>Ops(before detect):</b> "<< html_escape(opss.str()) <<"</div>";
	}
	if (show_detect){
		os << "<div><b>Detector:</b> "<< detect_repr(tp.detector) << " ["<< pos2s(tp.detector.pos) << "]</div>";
	}
	os << "</div>";
}

// 寫出單一 op 的摘要欄位（index/elem/idx/order/op 與 pre-state 五個點位）
static void write_op_summary_cells(std::ostream& os, size_t i, const OpContext& oc){
	os << "<td>"<< i <<"</td><td>"<< oc.elem_index <<"</td><td>"<< oc.index_within_elem <<"</td><td>"<< addr2s(oc.order) <<"</td><td>"<< op_repr(oc.op) <<"</td>";
	os << "<td class=\"state\">"<< html_escape(state_cell(oc.pre_state.A0.D, oc.pre_state.A0.C)) <<"</td>";
	os << "<td class=\"state\">"<< html_escape(state_cell(oc.pre_state.A1.D, oc.pre_state.A1.C)) <<"</td>";
	os << "<td class=\"state\">"<< html_escape(state_cell(oc.pre_state.A2_CAS.D, oc.pre_state.A2_CAS.C)) <<"</td>";
	os << "<td class=\"state\">"<< html_escape(state_cell(oc.pre_state.A3.D, oc.pre_state.A3.C)) <<"</td>";
	os << "<td class=\"state\">"<< html_escape(state_cell(oc.pre_state.A4.D, oc.pre_state.A4.C)) <<"</td>";
}

// 寫 State cover 表格
static void write_state_cover_table(std::ostream& os, const SimulationResult& sim,
									const vector<TestPrimitive>& tps,
									const vector<RawFault>& raw_faults,
									const std::unordered_map<string, size_t>& raw_index_by_id){
	os << "<details><summary>State cover (rows: "<< sim.op_table.size() <<")</summary>";
	os << "<table class=\"striped\"><thead><tr><th>#</th><th>Elem</th><th>Idx</th><th>Order</th><th>Op</th><th>Pre A0</th><th>Pre A1</th><th>Pre CAS</th><th>Pre A3</th><th>Pre A4</th><th>TPs</th></tr></thead><tbody>";
	for (size_t i=0;i<sim.op_table.size();++i){
		const auto& oc = sim.op_table[i]; const auto& cl = sim.cover_lists[i];
		os << "<tr>"; write_op_summary_cells(os, i, oc); os << "<td>";
		os << "<details><summary>TPs ("<< cl.state_cover.size() <<")</summary>";
		for (size_t tp_gid : cl.state_cover){
			const auto& tp = tps[tp_gid];
			const RawFault* rf = nullptr; auto it = raw_index_by_id.find(tp.parent_fault_id); if (it!=raw_index_by_id.end()) rf = &raw_faults[it->second];
			os << "<details><summary>#"<< tp_gid <<"</summary>";
			write_tp_details(os, tp_gid, tp, rf, /*state*/true, /*sens*/false, /*detect*/false);
			os << "</details>";
		}
		os << "</details>";
		os << "</td></tr>";
	}
	os << "</tbody></table>";
	os << "</details>";
}

// 寫 Sens cover 表格
static void write_sens_cover_table(std::ostream& os, const SimulationResult& sim,
								   const vector<TestPrimitive>& tps,
								   const vector<RawFault>& raw_faults,
								   const std::unordered_map<string, size_t>& raw_index_by_id){
	os << "<details><summary>Sens cover (rows: "<< sim.op_table.size() <<")</summary>";
	os << "<table class=\"striped\"><thead><tr><th>#</th><th>Elem</th><th>Idx</th><th>Order</th><th>Op</th><th>Pre A0</th><th>Pre A1</th><th>Pre CAS</th><th>Pre A3</th><th>Pre A4</th><th>TPs</th></tr></thead><tbody>";
	for (size_t i=0;i<sim.op_table.size();++i){
		const auto& oc = sim.op_table[i]; const auto& cl = sim.cover_lists[i];
		os << "<tr>"; write_op_summary_cells(os, i, oc); os << "<td>";
		os << "<details><summary>TPs ("<< cl.sens_cover.size() <<")</summary>";
		for (size_t tp_gid : cl.sens_cover){
			const auto& tp = tps[tp_gid];
			const RawFault* rf = nullptr; auto it = raw_index_by_id.find(tp.parent_fault_id); if (it!=raw_index_by_id.end()) rf = &raw_faults[it->second];
			os << "<details><summary>#"<< tp_gid <<"</summary>";
			write_tp_details(os, tp_gid, tp, rf, /*state*/false, /*sens*/true, /*detect*/false);
			os << "</details>";
		}
		os << "</details>";
		os << "</td></tr>";
	}
	os << "</tbody></table>";
	os << "</details>";
}

// 寫 Detect cover 表格
static void write_detect_cover_table(std::ostream& os, const SimulationResult& sim,
									 const vector<TestPrimitive>& tps,
									 const vector<RawFault>& raw_faults,
									 const std::unordered_map<string, size_t>& raw_index_by_id){
	os << "<details><summary>Detect cover (rows: "<< sim.op_table.size() <<")</summary>";
	os << "<table class=\"striped\"><thead><tr><th>#</th><th>Elem</th><th>Idx</th><th>Order</th><th>Op</th><th>Pre A0</th><th>Pre A1</th><th>Pre CAS</th><th>Pre A3</th><th>Pre A4</th><th>TPs</th></tr></thead><tbody>";
	for (size_t i=0;i<sim.op_table.size();++i){
		const auto& oc = sim.op_table[i]; const auto& cl = sim.cover_lists[i];
		os << "<tr>"; write_op_summary_cells(os, i, oc); os << "<td>";
		os << "<details><summary>TPs ("<< cl.det_cover.size() <<")</summary>";
		for (const auto& h : cl.det_cover){
			const auto& tp = tps[h.tp_gid];
			const RawFault* rf = nullptr; auto it = raw_index_by_id.find(tp.parent_fault_id); if (it!=raw_index_by_id.end()) rf = &raw_faults[it->second];
			os << "<details><summary>#"<< h.tp_gid <<"</summary>";
			write_tp_details(os, h.tp_gid, tp, rf, /*state*/false, /*sens*/false, /*detect*/true);
			os << "<div class=\"muted\">sens_op_id="<< h.sens_id <<", det_op_id="<< h.det_id <<"</div>";
			os << "</details>";
		}
		os << "</details>";
		os << "</td></tr>";
	}
	os << "</tbody></table>";
	os << "</details>";
}

// 輸出單一 op 的小表（便於在巢狀明細中查看該 op 的 pre-state 與資訊）
static void write_single_op_table(std::ostream& os, size_t id, const OpContext& oc){
	os << "<table class=\"striped\"><thead><tr><th>#</th><th>Elem</th><th>Idx</th><th>Order</th><th>Op</th><th>Pre A0</th><th>Pre A1</th><th>Pre CAS</th><th>Pre A3</th><th>Pre A4</th></tr></thead><tbody><tr>";
	write_op_summary_cells(os, id, oc);
	os << "</tr></tbody></table>";
}

// 輸出 Fault 覆蓋率總表：每個 fault 的 coverage 與被完整偵測的 TP 清單（含各次命中的 state/sens/detect op 明細）
static void write_faults_coverage_table(std::ostream& os,
										const SimulationResult& sim,
										const vector<Fault>& faults,
										const vector<TestPrimitive>& tps,
										const vector<RawFault>& raw_faults,
										const std::unordered_map<string, size_t>& raw_index_by_id){
	os << "<details><summary>Fault coverage summary ("<< faults.size() <<")</summary>";
	os << "<table class=\"striped\"><thead><tr><th>#</th><th>Fault ID</th><th>Coverage</th><th>TPs</th></tr></thead><tbody>";
	for (size_t fi=0; fi<faults.size(); ++fi){
		const auto& f = faults[fi];
		auto it = sim.fault_detail_map.find(f.fault_id);
		double cov = 0.0; const FaultCoverageDetail* fd = nullptr;
		if (it != sim.fault_detail_map.end()){ cov = it->second.coverage; fd = &it->second; }
		std::ostringstream pss; pss.setf(std::ios::fixed); pss<< std::setprecision(2) << (cov*100.0) << "%";
		os << "<tr><td>"<< fi <<"</td><td>"<< html_escape(f.fault_id) <<"</td><td>"<< pss.str() <<"</td><td>";
		{
			// 已偵測 TP：由 hits 彙總
			std::unordered_map<size_t, vector<const SensDetHit*>> group;
			std::unordered_set<size_t> detected_tp_set;
			if (fd){
				for (const auto& h : fd->hits){ group[h.tp_gid].push_back(&h); detected_tp_set.insert(h.tp_gid); }
			}
			// 未偵測 TP：掃 all_tps 中屬於此 fault 的，且不在 detected_tp_set
			vector<size_t> undetected_tp_gids;
			for (size_t tg=0; tg<tps.size(); ++tg){ if (tps[tg].parent_fault_id == f.fault_id && detected_tp_set.find(tg)==detected_tp_set.end()) undetected_tp_gids.push_back(tg); }

			os << "<details><summary>Detected ("<< group.size() <<")</summary>";
			for (const auto& [tp_gid, vecHits] : group){
				const auto& tp = tps[tp_gid];
				const RawFault* rf = nullptr; auto rit = raw_index_by_id.find(tp.parent_fault_id); if (rit!=raw_index_by_id.end()) rf = &raw_faults[rit->second];
				os << "<details><summary>#"<< tp_gid <<"</summary>";
				write_tp_details(os, tp_gid, tp, rf, /*state*/true, /*sens*/true, /*detect*/true);
				os << "<details><summary>Occurrences ("<< vecHits.size() <<")</summary>";
				size_t occ_idx = 0;
				for (const auto* ph : vecHits){
					const auto& h = *ph;
					int sens_end = h.sens_id; int det_id = h.det_id;
					int start_id = sens_end; if (!tp.ops_before_detect.empty()) start_id = sens_end - ((int)tp.ops_before_detect.size() - 1);
					os << "<details><summary>#"<< (++occ_idx) <<": state@"<< (start_id>=0? std::to_string(start_id):string("-")) <<", sens@"<< sens_end <<", det@"<< det_id <<"</summary>";
					if (start_id>=0 && start_id < (int)sim.op_table.size()){ os << "<details><summary>state op (#"<< start_id <<")</summary>"; write_single_op_table(os, start_id, sim.op_table[start_id]); os << "</details>"; } else { os << "<div class=\"muted\">state op: -</div>"; }
					if (sens_end>=0 && sens_end < (int)sim.op_table.size()){ os << "<details><summary>sens op (#"<< sens_end <<")</summary>"; write_single_op_table(os, sens_end, sim.op_table[sens_end]); os << "</details>"; } else { os << "<div class=\"muted\">sens op: -</div>"; }
					if (det_id>=0 && det_id < (int)sim.op_table.size()){ os << "<details><summary>detect op (#"<< det_id <<")</summary>"; write_single_op_table(os, det_id, sim.op_table[det_id]); os << "</details>"; } else { os << "<div class=\"muted\">detect op: -</div>"; }
					os << "</details>";
				}
				os << "</details>"; // occurrences
				os << "</details>"; // TP
			}
			os << "</details>"; // Detected

			os << "<details><summary>Undetected ("<< undetected_tp_gids.size() <<")</summary>";
			for (size_t tp_gid : undetected_tp_gids){
				const auto& tp = tps[tp_gid];
				const RawFault* rf = nullptr; auto rit = raw_index_by_id.find(tp.parent_fault_id); if (rit!=raw_index_by_id.end()) rf = &raw_faults[rit->second];
				os << "<details><summary>#"<< tp_gid <<"</summary>";
				write_tp_details(os, tp_gid, tp, rf, /*state*/true, /*sens*/true, /*detect*/true);
				os << "</details>";
			}
			os << "</details>"; // Undetected
		}
		os << "</td></tr>";
	}
	os << "</tbody></table>";
	os << "</details>";
}

// =====================
// 主流程
// =====================
int main(){
	try {
		// 1) Faults → Fault → TPs
		FaultsJsonParser fparser; FaultNormalizer fnorm; TPGenerator tpg;
		auto raw_faults = fparser.parse_file("0916Cross_shape/faults.json");
		vector<Fault> faults; faults.reserve(raw_faults.size());
		vector<string> warnings; warnings.reserve(16);
		for (const auto& rf : raw_faults) {
			try {
				faults.push_back(fnorm.normalize(rf));
			} catch (const std::exception& e) {
				warnings.push_back(string("Skip fault '") + rf.fault_id + "': " + e.what());
			}
		}
		vector<TestPrimitive> all_tps; all_tps.reserve(2048);
		for (const auto& f : faults){ auto tps = tpg.generate(f); all_tps.insert(all_tps.end(), tps.begin(), tps.end()); }
		// fault_id -> raw index map（供取回 raw primitive 字串）
		std::unordered_map<string, size_t> raw_index_by_id; raw_index_by_id.reserve(raw_faults.size()*2+1);
		for (size_t i=0;i<raw_faults.size();++i) raw_index_by_id[raw_faults[i].fault_id]=i;

		// 2) March tests
		MarchTestJsonParser mparser; auto raw_mts = mparser.parse_file("0916Cross_shape/MarchTest.json");
		MarchTestNormalizer mnorm; vector<MarchTest> marchTests; marchTests.reserve(raw_mts.size());
		for (const auto& r : raw_mts) marchTests.push_back(mnorm.normalize(r));

		// 3) 輸出 HTML
		std::ofstream ofs("output/March_Sim_Report.html");
		ofs << "<!DOCTYPE html><html><head><meta charset=\"utf-8\">\n";
		ofs << "<title>March Simulation Report</title>\n";
		ofs << "<style>body{font-family:sans-serif} details{margin:8px 0} summary{cursor:pointer;font-weight:600} table{border-collapse:collapse;margin:6px 0;width:100%} th,td{border:1px solid #ccc;padding:4px 6px;text-align:center;vertical-align:top} .muted{color:#666} .badge{display:inline-block;background:#eef;border:1px solid #99c;border-radius:10px;padding:2px 8px;margin-left:6px;font-size:12px} .ops{text-align:left;white-space:nowrap} .state{font-family:monospace} .striped tbody tr:nth-child(even){background:#f7f9fc} .faultHdr{margin-top:8px} .tpd{margin:6px 0 8px 12px;text-align:left}</style>\n";
		ofs << "</head><body>\n";
		ofs << "<h1>March Simulation Report</h1>\n";
		ofs << "<p class=\"muted\">Faults: "<<faults.size()<<", TPs: "<<all_tps.size()<<", MarchTests: "<<marchTests.size()<<"</p>\n";
		if (!warnings.empty()){
			ofs << "<details open><summary>Warnings ("<< warnings.size() <<")</summary><ul>";
			for (const auto& w : warnings) ofs << "<li>"<< html_escape(w) <<"</li>";
			ofs << "</ul></details>";
		}
		write_fault_anchors(ofs, raw_faults);

		FaultSimulator simulator;
		for (size_t mi=0; mi<marchTests.size(); ++mi){
			const auto& mt = marchTests[mi];
			auto sim = simulator.simulate(mt, faults, all_tps);

			std::ostringstream covss; covss.setf(std::ios::fixed); covss<< std::setprecision(2) << (sim.total_coverage*100.0) << "%";
			ofs << "<details open><summary>March Test: "<<html_escape(mt.name)
				<<" <span class=\"badge\">ops: "<<sim.op_table.size()<<"</span>"
				<<" <span class=\"badge\">total coverage: "<< covss.str() <<"</span>"
				<<"</summary>\n";
			// 三種 CoverList 表格
			write_state_cover_table(ofs, sim, all_tps, raw_faults, raw_index_by_id);
			write_sens_cover_table(ofs, sim, all_tps, raw_faults, raw_index_by_id);
			write_detect_cover_table(ofs, sim, all_tps, raw_faults, raw_index_by_id);
			// Fault coverage summary
			write_faults_coverage_table(ofs, sim, faults, all_tps, raw_faults, raw_index_by_id);
			ofs << "</details>\n";
		}
		ofs << "</body></html>\n";
		ofs.close();
		cout << "HTML report written to: output/March_Sim_Report.html\n";
	} catch (const std::exception& e){
		std::cerr << "MarchSimHtml error: "<< e.what() <<"\n";
		return 1;
	}
	return 0;
}

