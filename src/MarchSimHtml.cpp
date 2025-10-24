// # 編譯（需 C++17 以上）
// g++ -std=c++17 -O2 -Wall -Wextra -o src/MarchSimHtml src/MarchSimHtml.cpp

// # 執行
// ./src/MarchSimHtml input/S_C_faults.json input/MarchTest.json output/March_Sim_Report.html

// # 合併
// g++ -std=c++17 -O2 -Wall -Wextra -o src/MarchSimHtml src/MarchSimHtml.cpp && ./src/MarchSimHtml input/S_C_faults.json input/MarchTest.json output/March_Sim_Report.html

// MarchSimHtml.cpp — 產生 March 模擬後的 cover lists HTML 報告（以 argv 指定輸入輸出）
// -------------------------------------------------------------------------------------------------
// 功能總覽：
// 1) 讀取 faults.json → 以 FaultNormalizer 正規化為 Fault → 以 TPGenerator 產生所有 TestPrimitive
// 2) 讀取 MarchTest.json → 正規化 → 逐一呼叫 FaultSimulator::simulate
// 3) 每個 MarchTest 輸出四段內容：State / Sens / Detect cover 三張表 + Fault coverage summary
//
// 風格與維護性：
// - 單一職責：主流程只做 IO 與組裝，HTML 由下方 writer 系列函式集中處理
// - 完整註解（每個物件/函式：輸入、輸出資料結構與處理）
// - 以 argc/argv 指定三個參數：<faults.json> <MarchTest.json> <output.html>
// - C++17：使用 std::filesystem 建立輸出路徑
//
// 主要外部型別（由包含檔提供）：
//   Val { Zero, One, X }
//   AddrOrder { Up, Down, Any }
//   PositionMark { Adjacent, SameElementHead, NextElementHead }
//   OrientationGroup { Single, A_LT_V, A_GT_V }
//   OpKind { Write, Read, ComputeAnd }
//   struct Op { OpKind kind; Val value; Val C_T, C_M, C_B; /* ... */ }
//   struct Detector { Op detectOp; PositionMark pos; /* ... */ }
//
//   struct RawFault { std::string fault_id; std::vector<std::string> fp_raw; /* ... */ }
//   struct Fault     { std::string fault_id; /* ... 正規化後可被 TP 生成器消化的 Fault */ }
//   struct TestPrimitive {
//       std::string parent_fault_id;
//       size_t parent_fp_index;
//       OrientationGroup group;
//       struct { struct { Val D,C; } A0,A1,A2_CAS,A3,A4; } state;
//       std::vector<Op> ops_before_detect;
//       Detector detector;
//   };
//   struct OpContext {
//       size_t elem_index;          // 第幾個 March element
//       size_t index_within_elem;   // element 內第幾個 op
//       AddrOrder order;            // 該 element 的位址遞移方向
//       Op op;                      // 當前操作（W0/W1/R0/R1/C(T)(M)(B)）
//       struct { struct { Val D,C; } A0,A1,A2_CAS,A3,A4; } pre_state; // Cross-state(5點) 於此 op 之前
//   };
//   struct CoverListPerOp {
//       std::vector<size_t> state_cover;  // 命中 TP gid（state）
//       std::vector<size_t> sens_cover;   // 命中 TP gid（sens）
//       std::vector<size_t> det_cover;    // 命中偵測（tp_gid）
//   };
//   struct FaultCoverageDetail {
//       double coverage;          // 與 detect_coverage 相同（相容性）
//       double state_coverage;    // state 階段 coverage
//       double sens_coverage;     // sens  階段 coverage
//       double detect_coverage;   // detect階段 coverage
//       std::vector<size_t> state_tp_gids;  // 命中 state 的 TP gids
//       std::vector<size_t> sens_tp_gids;   // 命中 sens  的 TP gids
//       std::vector<size_t> detect_tp_gids; // 命中 detect 的 TP gids
//   };
//   struct SimulationResult {
//       std::vector<OpContext> op_table;                        // 模擬展開後的 op 清單（含 pre-state）
//       std::vector<CoverListPerOp> cover_lists;                // 對應每個 op 的 cover 結果
//       std::unordered_map<std::string, FaultCoverageDetail> fault_detail_map; // 依 fault_id 彙總
//       double total_coverage;                                  // 全體 fault coverage
//   };
//
// 依賴的介面（由包含檔提供）：
//   class FaultsJsonParser { vector<RawFault> parse_file(const string& path); }
//   class FaultNormalizer   { Fault normalize(const RawFault&); }
//   class TPGenerator       { vector<TestPrimitive> generate(const Fault&); }
//   class MarchTestJsonParser { vector<RawMarchTest> parse_file(const string&); /*…*/ }
//   class MarchTestNormalizer { MarchTest normalize(const RawMarchTest&); }
//   class FaultSimulator    { SimulationResult simulate(const MarchTest&, const vector<Fault>&, const vector<TestPrimitive>&); }
//
// -------------------------------------------------------------------------------------------------

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

using std::string; using std::vector; using std::cout; using std::endl;

// =====================
// 小工具：格式/轉換（純字串/HTML 輸出，不做業務邏輯）
// =====================

/**
 * @brief 將 Val 轉為 "0" / "1" / "-" 字樣
 */
static const char* v2s(Val v){
	switch(v){
		case Val::Zero: return "0";
		case Val::One : return "1";
		case Val::X   : return "-";
	}
	return "?";
}

/**
 * @brief 將位址遞移順序轉成字串 "Up" / "Down" / "Any"
 */
static const char* addr2s(AddrOrder o){
	switch(o){
		case AddrOrder::Up  : return "Up";
		case AddrOrder::Down: return "Down";
		case AddrOrder::Any : return "Any";
	}
	return "?";
}

/**
 * @brief 偵測器位置符號（用於 HTML 顯示）："#" / "^" / ";"
 * - Adjacent: 需緊鄰
 * - SameElementHead: 偵測器在同一個 Element 的起始
 * - NextElementHead: 偵測器在下一個 Element 的起始
 */
static const char* pos2s(PositionMark p){
	switch(p){
		case PositionMark::Adjacent       : return "#";
		case PositionMark::SameElementHead: return "^";
		case PositionMark::NextElementHead: return ";";
	}
	return "?";
}

/**
 * @brief 方向群組的簡短字樣（TP 群組標籤）
 */
static const char* group2short(OrientationGroup g){
	switch(g){
		case OrientationGroup::Single:  return "single";
		case OrientationGroup::A_LT_V:  return "a&lt;v";
		case OrientationGroup::A_GT_V:  return "a&gt;v";
	}
	return "?";
}

/**
 * @brief 基本 HTML escape（& < > " '）
 * @param in 原始字串
 * @return 轉義後字串
 */
static string html_escape(const string& in){
	string out; out.reserve(in.size());
	for(char c: in){
		switch(c){
			case '&': out += "&amp;";  break;
			case '<': out += "&lt;";   break;
			case '>': out += "&gt;";   break;
			case '"': out += "&quot;"; break;
			case '\'': out += "&#39;";  break;
			default : out += c;        break;
		}
	}
	return out;
}

/**
 * @brief 組 anchor id（非英數轉 '-'）
 * @param raw 任意字串（通常是 fault_id）
 */
static string make_anchor_id(const string& raw){
	string id; id.reserve(raw.size());
	for(char c: raw){
		if ((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')) id.push_back(c);
		else id.push_back('-');
	}
	return id;
}

/**
 * @brief 將 Op 轉為短字串（W0/R1 或 C(T)(M)(B)）
 * @param op 操作
 */
static string op_repr(const Op& op){
	auto bit=[](Val v){ return v==Val::Zero?'0':v==Val::One?'1':'-'; };
	if (op.kind==OpKind::Write){
		if (op.value==Val::Zero) return string("W0");
		if (op.value==Val::One ) return string("W1");
		return string("W-");
	} else if (op.kind==OpKind::Read){
		if (op.value==Val::Zero) return string("R0");
		if (op.value==Val::One ) return string("R1");
		return string("R-");
	} else {
		std::string s="C(";
		s.push_back(bit(op.C_T)); s+=")(";
		s.push_back(bit(op.C_M)); s+=")(";
		s.push_back(bit(op.C_B)); s+=")";
		return s;
	}
}

/**
 * @brief 偵測器文字表示（讀或運算）
 * @param d 偵測器
 */
static string detect_repr(const Detector& d){
	if (d.detectOp.kind==OpKind::Read){
		if (d.detectOp.value==Val::Zero) return "R0";
		if (d.detectOp.value==Val::One ) return "R1";
		return string("R-");
	}
	if (d.detectOp.kind==OpKind::ComputeAnd){
		auto b=[](Val v){ return v==Val::Zero?'0':v==Val::One?'1':'-'; };
		std::string s="C(";
		s.push_back(b(d.detectOp.C_T)); s+=")(";
		s.push_back(b(d.detectOp.C_M)); s+=")(";
		s.push_back(b(d.detectOp.C_B)); s+=")";
		return s;
	}
	return string("?");
}

/**
 * @brief CrossState 的 (D,C) 一格
 */
static string state_cell(Val d, Val c){
	std::ostringstream os; os<<v2s(d)<<","<<v2s(c);
	return os.str();
}

// =====================
// HTML writers（單一職責：只輸出 HTML；輸入均為已準備好的資料）
// =====================

/**
 * @brief 寫出 Fault anchors（供 TP 明細中的連結指向正確位置）
 * @param os      目標輸出串流（HTML file stream）
 * @param raws    faults.json 解析結果（RawFault 陣列；用到 fault_id 與 fp_raw）
 * @return 無
 */
static void write_fault_anchors(std::ostream& os, const vector<RawFault>& raws){
	os << "<section id=\"fault-anchors\" style=\"display:none\">";
	for (const auto& rf : raws){
		os << "<div id=\"fault-"<< make_anchor_id(rf.fault_id) << "\"></div>";
	}
	os << "</section>";
}

/**
 * @brief 寫出單一 TP 的詳情（依表格性質控制是否顯示 State、Sens、Detect）
 * @param os            目標輸出串流
 * @param tp_gid        TP 的全域索引
 * @param tp            TestPrimitive 內容（含 parent_fault_id、state、ops_before_detect、detector）
 * @param raw_fault     指向原始 RawFault（若可對應，便可顯示 FP 字串）；否則可為 nullptr
 * @param show_state    是否顯示 TP 的五點位 State（A0/A1/CAS/A3/A4）
 * @param show_sens     是否顯示偵測前的操作序列（ops_before_detect）
 * @param show_detect   是否顯示偵測器（detector）
 */
static void write_tp_details(std::ostream& os, const TestPrimitive& tp,
							 const RawFault* raw_fault, bool show_state, bool show_sens, bool show_detect){
	os << "<div class=\"tpd\">";
	// Fault 連結 + 原始 FP 字串
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
		std::ostringstream opss;
		if (tp.ops_before_detect.empty()) opss<<"-";
		else {
			for (size_t i=0;i<tp.ops_before_detect.size();++i){
				if(i)opss<<", ";
				opss<<op_repr(tp.ops_before_detect[i]);
			}
		}
		os << "<div><b>Ops(before detect):</b> "<< html_escape(opss.str()) <<"</div>";
	}
	if (show_detect){
		os << "<div><b>Detector:</b> "<< detect_repr(tp.detector) << " ["<< pos2s(tp.detector.pos) << "]</div>";
	}
	os << "</div>";
}

/**
 * @brief 寫出單一 op 的摘要欄位（index/elem/idx/order/op 與 pre-state 五個點位）
 * @param os 目標輸出串流
 * @param i  目前 op 在 sim.op_table 的索引
 * @param oc 對應的 OpContext（含 elem/index/order/op 與 pre_state 5 個點位）
 */
static void write_op_summary_cells(std::ostream& os, size_t i, const OpContext& oc){
	os << "<td>"<< i <<"</td><td>"<< (oc.elem_index + 1) <<"</td><td>"<< (oc.index_within_elem + 1)
	   <<"</td><td>"<< addr2s(oc.order) <<"</td>";
	// Place pre-state cells before Op
	os << "<td class=\"state\">"<< html_escape(state_cell(oc.pre_state.A0.D,     oc.pre_state.A0.C))     <<"</td>";
	os << "<td class=\"state\">"<< html_escape(state_cell(oc.pre_state.A1.D,     oc.pre_state.A1.C))     <<"</td>";
	os << "<td class=\"state\">"<< html_escape(state_cell(oc.pre_state.A2_CAS.D, oc.pre_state.A2_CAS.C)) <<"</td>";
	os << "<td class=\"state\">"<< html_escape(state_cell(oc.pre_state.A3.D,     oc.pre_state.A3.C))     <<"</td>";
	os << "<td class=\"state\">"<< html_escape(state_cell(oc.pre_state.A4.D,     oc.pre_state.A4.C))     <<"</td>";
	os << "<td>"<< op_repr(oc.op) <<"</td>";
}

/**
 * @brief 輔助：三種 cover 表格共同的表頭
 */
static inline void write_common_table_head(std::ostream& os){
	os << "<table class=\"striped\"><thead><tr>"
	<< "<th>#</th><th>Elem</th><th>Idx</th><th>Order</th>"
	<< "<th>Pre A0</th><th>Pre A1</th><th>Pre CAS</th><th>Pre A3</th><th>Pre A4</th><th>Op</th><th>Coverage</th><th>TPs</th>"
	   << "</tr></thead><tbody>";
}

/**
 * @brief 寫 State cover 表格
 * @param os             輸出
 * @param sim            模擬結果（含 op_table 與 cover_lists）
 * @param tps            全部 TestPrimitive（以 tp_gid 取用）
 * @param raw_faults     原始 RawFault 陣列（供回顯 FP 字串）
 * @param raw_index_by_id fault_id → raw_faults 索引
 */
static void write_state_cover_table(std::ostream& os, const SimulationResult& sim,
									const vector<TestPrimitive>& tps,
									const vector<RawFault>& raw_faults,
									const std::unordered_map<string, size_t>& raw_index_by_id,
									const vector<Fault>& faults){
	os << "<details><summary>State cover (rows: "<< sim.op_table.size() <<")</summary>";
	write_common_table_head(os);
	int last_elem = -1; bool useB = true; // first element => rowA
	for (size_t i=0;i<sim.op_table.size();++i){
		const auto& oc = sim.op_table[i];
		const auto& cl = sim.cover_lists[i];
		if (oc.elem_index != last_elem){ useB = !useB; last_elem = oc.elem_index; }
		os << "<tr class=\"" << (useB? "rowB" : "rowA") << "\">";
		write_op_summary_cells(os, i, oc);
		// Coverage cell (state) — cumulative from op#0 to current i
		{
			struct Flags{ bool any=false, lt=false, gt=false; };
			std::unordered_map<string, Flags> agg;
			// accumulate tp groups up to current op
			for (size_t j=0; j<=i && j<sim.cover_lists.size(); ++j){
				for (size_t gid : sim.cover_lists[j].state_cover){
					const auto& tp = tps[gid];
					auto& f = agg[tp.parent_fault_id];
					if (tp.group==OrientationGroup::Single) f.any=true;
					else if (tp.group==OrientationGroup::A_LT_V) f.lt=true;
					else if (tp.group==OrientationGroup::A_GT_V) f.gt=true;
				}
			}
			double sum=0.0; size_t denom = faults.size();
			for (const auto& fault : faults){
				const auto itf = agg.find(fault.fault_id);
				double cov = 0.0;
				if (fault.cell_scope == CellScope::SingleCell){
					cov = (itf!=agg.end() && itf->second.any) ? 1.0 : 0.0;
				} else {
					bool lt = (itf!=agg.end() && itf->second.lt);
					bool gt = (itf!=agg.end() && itf->second.gt);
					cov = (lt?0.5:0.0) + (gt?0.5:0.0);
				}
				sum += cov;
			}
			double avg = denom? (sum/denom) : 0.0;
			std::ostringstream covss; covss.setf(std::ios::fixed); covss<< std::setprecision(2) << (avg*100.0) << "%";
			os << "<td>"<< covss.str() <<"</td>";
		}
		os << "<td><details><summary>TPs ("<< cl.state_cover.size() <<")</summary>";
		for (size_t tp_gid : cl.state_cover){
			const auto& tp = tps[tp_gid];
			const RawFault* rf = nullptr;
			if (auto it = raw_index_by_id.find(tp.parent_fault_id); it!=raw_index_by_id.end())
				rf = &raw_faults.at(it->second);
			os << "<details><summary>#"<< tp_gid <<"</summary>";
			write_tp_details(os, tp, rf, /*state*/true, /*sens*/false, /*detect*/false);
			os << "</details>";
		}
		os << "</details></td></tr>";
	}
	os << "</tbody></table></details>";
}

/**
 * @brief 寫 Sens cover 表格
 */
static void write_sens_cover_table(std::ostream& os, const SimulationResult& sim,
								   const vector<TestPrimitive>& tps,
								   const vector<RawFault>& raw_faults,
								   const std::unordered_map<string, size_t>& raw_index_by_id,
								   const vector<Fault>& faults){
	os << "<details><summary>Sens cover (rows: "<< sim.op_table.size() <<")</summary>";
	write_common_table_head(os);
	int last_elem = -1; bool useB = true;
	for (size_t i=0;i<sim.op_table.size();++i){
		const auto& oc = sim.op_table[i];
		const auto& cl = sim.cover_lists[i];
		if (oc.elem_index != last_elem){ useB = !useB; last_elem = oc.elem_index; }
		os << "<tr class=\"" << (useB? "rowB" : "rowA") << "\">";
		write_op_summary_cells(os, i, oc);
		// Coverage cell (sens) — cumulative
		{
			struct Flags{ bool any=false, lt=false, gt=false; };
			std::unordered_map<string, Flags> agg;
			for (size_t j=0; j<=i && j<sim.cover_lists.size(); ++j){
				for (size_t gid : sim.cover_lists[j].sens_cover){
					const auto& tp = tps[gid];
					auto& f = agg[tp.parent_fault_id];
					if (tp.group==OrientationGroup::Single) f.any=true;
					else if (tp.group==OrientationGroup::A_LT_V) f.lt=true;
					else if (tp.group==OrientationGroup::A_GT_V) f.gt=true;
				}
			}
			double sum=0.0; size_t denom = faults.size();
			for (const auto& fault : faults){
				const auto itf = agg.find(fault.fault_id);
				double cov = 0.0;
				if (fault.cell_scope == CellScope::SingleCell){
					cov = (itf!=agg.end() && itf->second.any) ? 1.0 : 0.0;
				} else {
					bool lt = (itf!=agg.end() && itf->second.lt);
					bool gt = (itf!=agg.end() && itf->second.gt);
					cov = (lt?0.5:0.0) + (gt?0.5:0.0);
				}
				sum += cov;
			}
			double avg = denom? (sum/denom) : 0.0;
			std::ostringstream covss; covss.setf(std::ios::fixed); covss<< std::setprecision(2) << (avg*100.0) << "%";
			os << "<td>"<< covss.str() <<"</td>";
		}
		os << "<td><details><summary>TPs ("<< cl.sens_cover.size() <<")</summary>";
		for (size_t tp_gid : cl.sens_cover){
			const auto& tp = tps[tp_gid];
			const RawFault* rf = nullptr;
			if (auto it = raw_index_by_id.find(tp.parent_fault_id); it!=raw_index_by_id.end())
				rf = &raw_faults.at(it->second);
			os << "<details><summary>#"<< tp_gid <<"</summary>";
			write_tp_details(os, tp, rf, /*state*/false, /*sens*/true, /*detect*/false);
			os << "</details>";
		}
		os << "</details></td></tr>";
	}
	os << "</tbody></table></details>";
}

/**
 * @brief 寫 Detect cover 表格
 */
static void write_detect_cover_table(std::ostream& os, const SimulationResult& sim,
									 const vector<TestPrimitive>& tps,
									 const vector<RawFault>& raw_faults,
									 const std::unordered_map<string, size_t>& raw_index_by_id,
									 const vector<Fault>& faults){
	os << "<details><summary>Detect cover (rows: "<< sim.op_table.size() <<")</summary>";
	write_common_table_head(os);
	int last_elem = -1; bool useB = true;
	for (size_t i=0;i<sim.op_table.size();++i){
		const auto& oc = sim.op_table[i];
		const auto& cl = sim.cover_lists[i];
		if (oc.elem_index != last_elem){ useB = !useB; last_elem = oc.elem_index; }
		os << "<tr class=\"" << (useB? "rowB" : "rowA") << "\">";
		write_op_summary_cells(os, i, oc);
		// Coverage cell (detect) — cumulative
		{
			struct Flags{ bool any=false, lt=false, gt=false; };
			std::unordered_map<string, Flags> agg;
			for (size_t j=0; j<=i && j<sim.cover_lists.size(); ++j){
				for (size_t gid : sim.cover_lists[j].det_cover){
					const auto& tp = tps[gid];
					auto& f = agg[tp.parent_fault_id];
					if (tp.group==OrientationGroup::Single) f.any=true;
					else if (tp.group==OrientationGroup::A_LT_V) f.lt=true;
					else if (tp.group==OrientationGroup::A_GT_V) f.gt=true;
				}
			}
			double sum=0.0; size_t denom = faults.size();
			for (const auto& fault : faults){
				const auto itf = agg.find(fault.fault_id);
				double cov = 0.0;
				if (fault.cell_scope == CellScope::SingleCell){
					cov = (itf!=agg.end() && itf->second.any) ? 1.0 : 0.0;
				} else {
					bool lt = (itf!=agg.end() && itf->second.lt);
					bool gt = (itf!=agg.end() && itf->second.gt);
					cov = (lt?0.5:0.0) + (gt?0.5:0.0);
				}
				sum += cov;
			}
			double avg = denom? (sum/denom) : 0.0;
			std::ostringstream covss; covss.setf(std::ios::fixed); covss<< std::setprecision(2) << (avg*100.0) << "%";
			os << "<td>"<< covss.str() <<"</td>";
		}
		os << "<td><details><summary>TPs ("<< cl.det_cover.size() <<")</summary>";
		for (size_t tp_gid : cl.det_cover){
			const auto& tp = tps[tp_gid];
			const RawFault* rf = nullptr;
			if (auto it = raw_index_by_id.find(tp.parent_fault_id); it!=raw_index_by_id.end())
				rf = &raw_faults.at(it->second);
			os << "<details><summary>#"<< tp_gid <<"</summary>";
			write_tp_details(os, tp, rf, /*state*/false, /*sens*/false, /*detect*/true);
			os << "</details>";
		}
		os << "</details></td></tr>";
	}
	os << "</tbody></table></details>";
}

/**
 * @brief 輸出單一 op 的迷你表格（巢狀明細使用）
 */
// removed unused helper write_single_op_table (was causing -Wunused-function warning)

/**
 * @brief 輸出 Fault 覆蓋率總表（每個 fault 的 coverage 與被完整偵測的 TP 清單）
 * @param os, sim, faults, tps, raw_faults, raw_index_by_id 同上
 */
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
	const char* covCls = "";
	if (std::abs(cov - 0.0) < 1e-9) covCls = "cov0"; else if (std::abs(cov - 0.5) < 1e-9) covCls = "cov50";
	os << "<tr><td>"<< fi <<"</td><td>"<< html_escape(f.fault_id) <<"</td><td class=\""<< covCls <<"\">"<< pss.str() <<"</td><td>";

		// 已偵測與未偵測的 TP 分組顯示（以 tp_gid 作為唯一鍵）
		std::unordered_set<size_t> detected_tp_set;
		if (fd){ for (const auto& gid : fd->detect_tp_gids) detected_tp_set.insert(gid); }
		vector<size_t> undetected_tp_gids;
		for (size_t tg=0; tg<tps.size(); ++tg){
			if (tps[tg].parent_fault_id == f.fault_id && detected_tp_set.find(tg)==detected_tp_set.end())
				undetected_tp_gids.push_back(tg);
		}

		// Detected（不再顯示 occurrences 細節）
		os << "<details><summary>Detected ("<< detected_tp_set.size() <<")</summary>";
		for (const auto& tp_gid : detected_tp_set){
			const auto& tp = tps[tp_gid];
			const RawFault* rf = nullptr;
			if (auto rit = raw_index_by_id.find(tp.parent_fault_id); rit!=raw_index_by_id.end())
				rf = &raw_faults[rit->second];

			os << "<details><summary>#"<< tp_gid <<"</summary>";
			write_tp_details(os, tp, rf, /*state*/true, /*sens*/true, /*detect*/true);
			os << "</details>"; // TP
		}
		os << "</details>"; // Detected

		// Undetected
		os << "<details><summary>Undetected ("<< undetected_tp_gids.size() <<")</summary>";
		for (size_t tp_gid : undetected_tp_gids){
			const auto& tp = tps[tp_gid];
			const RawFault* rf = nullptr;
			if (auto rit = raw_index_by_id.find(tp.parent_fault_id); rit!=raw_index_by_id.end())
				rf = &raw_faults[rit->second];
			os << "<details><summary>#"<< tp_gid <<"</summary>";
			write_tp_details(os, tp, rf, /*state*/true, /*sens*/true, /*detect*/true);
			os << "</details>";
		}
		os << "</details>"; // Undetected

		os << "</td></tr>";
	}
	os << "</tbody></table></details>";
}

// =====================
// 參數解析與 HTML 檔頭
// =====================

/**
 * @brief 簡易參數物件：三個必填路徑
 */
struct CmdOptions {
	string faults_json;   // argv[1]
	string march_json;    // argv[2]
	string output_html;   // argv[3]
};

/**
 * @brief 解析命令列參數。需要三個必填參數。
 * @return 解析成功回傳 true；否則輸出用法並回傳 false
 */
static bool parse_args(int argc, char** argv, CmdOptions& opt){
	if (argc != 4){
		std::cerr
			<< "Usage: " << (argc>0? argv[0] : "MarchSimHtml")
			<< " <faults.json> <MarchTest.json> <output.html>\n";
		return false;
	}
	opt.faults_json = argv[1];
	opt.march_json  = argv[2];
	opt.output_html = argv[3];
	return true;
}

/**
 * @brief 寫 HTML <head> 與樣式
 */
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

// =====================
// 主流程
// =====================

int main(int argc, char** argv){
	CmdOptions opt;
	if (!parse_args(argc, argv, opt)) return 2;

	try {
		// 計時工具
	using clock = std::chrono::steady_clock;
	auto to_us = [](auto dur){ return std::chrono::duration_cast<std::chrono::microseconds>(dur).count(); };
		// 確保輸出目錄存在
		try {
			std::filesystem::path outp(opt.output_html);
			if (outp.has_parent_path()){
				std::filesystem::create_directories(outp.parent_path());
			}
		} catch(...) {
			// 目錄建立失敗不致命，讓後面 open 檢查報錯
		}

		// 1) Faults → Fault → TPs
		auto t1_start = clock::now();
		FaultsJsonParser fparser; FaultNormalizer fnorm; TPGenerator tpg;

		auto raw_faults = fparser.parse_file(opt.faults_json); // 讀 faults.json
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
		for (const auto& f : faults){
			auto tps = tpg.generate(f);
			all_tps.insert(all_tps.end(), tps.begin(), tps.end());
		}
	auto t1_end = clock::now();
	cout << "[時間] 1) Faults→Fault→TPs: " << to_us(t1_end - t1_start) << " us"
		     << " (raw_faults=" << raw_faults.size() << ", faults=" << faults.size()
		     << ", TPs=" << all_tps.size() << ")\n";

		// fault_id -> raw index map（供回查 raw primitive 字串）
		std::unordered_map<string, size_t> raw_index_by_id;
		raw_index_by_id.reserve(raw_faults.size()*2+1);
		for (size_t i=0;i<raw_faults.size();++i) raw_index_by_id[raw_faults[i].fault_id]=i;

		// 2) March tests（讀 MarchTest.json 並正規化）
		auto t2_start = clock::now();
		MarchTestJsonParser mparser;
		auto raw_mts = mparser.parse_file(opt.march_json);

		MarchTestNormalizer mnorm;
		vector<MarchTest> marchTests; marchTests.reserve(raw_mts.size());
		for (const auto& r : raw_mts) marchTests.push_back(mnorm.normalize(r));
	   auto t2_end = clock::now();
	   cout << "[時間] 2) 解析 March tests 並正規化: " << to_us(t2_end - t2_start)
		   << " us (tests=" << marchTests.size() << ")\n";

		// 3) 輸出 HTML
		std::ofstream ofs(opt.output_html);
		if (!ofs){
			throw std::runtime_error("cannot open output html: " + opt.output_html);
		}

		write_html_head(ofs, faults.size(), all_tps.size(), marchTests.size());

		// 警告區塊（若有）
		if (!warnings.empty()){
			ofs << "<details open><summary>Warnings ("<< warnings.size() <<")</summary><ul>";
			for (const auto& w : warnings) ofs << "<li>"<< html_escape(w) <<"</li>";
			ofs << "</ul></details>";
		}

		// Fault anchors（給 TP 明細超連結）
		write_fault_anchors(ofs, raw_faults);

		// 逐個 March Test 模擬並輸出
		FaultSimulator simulator;
		auto t3_start = clock::now();
	long long per_tests_sum_us = 0;
		for (size_t mi=0; mi<marchTests.size(); ++mi){
			const auto& mt = marchTests[mi];
			auto t_mt_start = clock::now();
			auto sim = simulator.simulate(mt, faults, all_tps);
			auto t_mt_end = clock::now();
			auto us = to_us(t_mt_end - t_mt_start);
			per_tests_sum_us += us;

			std::ostringstream covss; covss.setf(std::ios::fixed); covss<< std::setprecision(2) << (sim.total_coverage*100.0) << "%";
			ofs << "<details open><summary>March Test: "<<html_escape(mt.name)
				<<" <span class=\"badge\">ops: "<<sim.op_table.size()<<"</span>"
				<<" <span class=\"badge\">total coverage: "<< covss.str() <<"</span>"
				<<"</summary>\n";

			// 三種 CoverList 表格
			write_state_cover_table(ofs, sim, all_tps, raw_faults, raw_index_by_id, faults);
			write_sens_cover_table(ofs, sim, all_tps, raw_faults, raw_index_by_id, faults);
			write_detect_cover_table(ofs, sim, all_tps, raw_faults, raw_index_by_id, faults);

			// Fault coverage summary
			write_faults_coverage_table(ofs, sim, faults, all_tps, raw_faults, raw_index_by_id);

			ofs << "</details>\n";
			
		  cout << "[時間] 3) 模擬+輸出 March Test '" << mt.name << "': " << us
			  << " us (ops=" << sim.op_table.size() << ")\n";
		}
		auto t3_end = clock::now();
	   cout << "[時間] 3) 執行時間(包含撰寫報告)總耗時: " << to_us(t3_end - t3_start)
		   << " us (單測累計=" << per_tests_sum_us << " us)\n";

		ofs << "</body></html>\n";
		ofs.close();

		cout << "HTML report written to: " << opt.output_html << "\n";
	} catch (const std::exception& e){
		std::cerr << "MarchSimHtml error: "<< e.what() <<"\n";
		return 1;
	}
	return 0;
}
