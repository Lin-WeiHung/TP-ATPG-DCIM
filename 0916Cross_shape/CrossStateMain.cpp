// CrossStateMain.cpp
// 功能: 讀入 faults.json -> Parser -> CrossStateExpander 展開 -> 建立候選集合 -> 使用 CrossStateCoverSolver -> 輸出 Markdown

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <chrono>

#include "Parser.hpp"
#include "CrossStateExpander.hpp"
#include "CrossStateCoverSolver.hpp"

using std::string; using std::vector; using std::cout; using std::endl; using std::size_t; using std::map; using std::set; using std::ostringstream; using std::runtime_error; using std::ifstream; using std::ofstream; using std::chrono::steady_clock; using std::chrono::duration_cast; using std::chrono::milliseconds;

struct ExpandedPrimitive {
	size_t fault_index;     // 對應 Fault
	size_t primitive_index; // 對應 Fault.primitives
	vector<CrossState> states; // 展開出的 CrossState 列表
};

// 將 CrossState 轉為簡易字串 (用於去重) D?C? 五組
static string cross_state_key(const CrossState& st) {
	// 每格: D: -1->X,0->0,1->1  C 同
	auto enc = [](int v){ return v==-1? 'X' : (v==0?'0':'1'); };
	std::string s; s.reserve(5*4);
	for (int i=0;i<5;++i) {
		s.push_back(enc(st.cells[i].D));
		s.push_back('/');
		s.push_back(enc(st.cells[i].C));
		if (i!=4) s.push_back(' ');
	}
	return s;
}

// Markdown 輔助: 將單一 CrossState 轉為表格行或行內表示
static string cross_state_md(const CrossState& st) {
	auto enc = [](int v){ return v==-1? 'X' : (v==0?'0':'1'); };
	std::ostringstream oss;
	// D row
	oss << "D:|";
	for (int i=0;i<5;++i) oss << enc(st.cells[i].D) << '|';
	oss << "\nC:|";
	for (int i=0;i<5;++i) oss << enc(st.cells[i].C) << '|';
	oss << " (" << st.case_name << ")";
	return oss.str();
}

// 產生 Markdown 報告
static void write_markdown(const string& path,
						   const vector<Fault>& faults,
						   const vector<ExpandedPrimitive>& expanded,
						   const vector<CrossState>& universe,
						   const vector<vector<CrossState>>& candidateSets,
						   const CoverResult& result) {
	ofstream ofs(path);
	if (!ofs) throw runtime_error("Cannot open output markdown: " + path);
	ofs << "# CrossState Cover Solver Report\n\n";
	ofs << "Generated at: " << time(nullptr) << "\n\n";

	ofs << "## Fault Summary\n\n";
	ofs << "Total faults: " << faults.size() << "\n\n";

	ofs << "## Expanded Primitives\n\n";
	size_t total_states = 0;
	for (const auto& ep : expanded) total_states += ep.states.size();
	ofs << "Total primitives: " << expanded.size() << ", total expanded CrossStates: " << total_states << "\n\n";
	for (const auto& ep : expanded) {
		const auto& f = faults[ep.fault_index];
		const auto& prim = f.primitives[ep.primitive_index];
		ofs << "### Fault " << f.fault_id << " / primitive " << ep.primitive_index << " (" << prim.original << ")\n\n";
		ofs << "CellScope: ";
		switch (f.cell_scope) {
			case CellScope::Single: ofs << "single"; break;
			case CellScope::TwoRowAgnostic: ofs << "two row-agnostic"; break;
			case CellScope::TwoCrossRow: ofs << "two cross row"; break;
		}
		ofs << "  States: " << ep.states.size() << "\n\n";
		ofs << "|Type|D0|D1|D2|D3|D4|C0|C1|C2|C3|C4|Case|\n";
		ofs << "|---|---|---|---|---|---|---|---|---|---|---|---|\n";
		auto enc = [](int v){ return v==-1? 'X' : (v==0?'0':'1'); };
		for (size_t si=0; si<ep.states.size(); ++si) {
			const auto& st = ep.states[si];
			ofs << '|' << si;
			for (int i=0;i<5;++i) ofs << '|' << enc(st.cells[i].D);
			for (int i=0;i<5;++i) ofs << '|' << enc(st.cells[i].C);
			ofs << '|' << st.case_name << "|\n";
		}
		ofs << "\n";
	}

	ofs << "## Universe (" << universe.size() << ")\n\n";
	ofs << "|Idx|D0|D1|D2|D3|D4|C0|C1|C2|C3|C4|\n";
	ofs << "|---|---|---|---|---|---|---|---|---|---|---|\n";
	auto enc = [](int v){ return v==-1? 'X' : (v==0?'0':'1'); };
	for (size_t ui=0; ui<universe.size(); ++ui) {
		ofs << '|' << ui;
		for (int i=0;i<5;++i) ofs << '|' << enc(universe[ui].cells[i].D);
		for (int i=0;i<5;++i) ofs << '|' << enc(universe[ui].cells[i].C);
		ofs << "|\n";
	}
	ofs << "\n";

	ofs << "## Candidate Sets (" << candidateSets.size() << ")\n\n";
	for (size_t ci=0; ci<candidateSets.size(); ++ci) {
		ofs << "### Candidate Set " << ci << " (size=" << candidateSets[ci].size() << ")\n\n";
		ofs << "|Idx|D0|D1|D2|D3|D4|C0|C1|C2|C3|C4|Case|\n";
		ofs << "|---|---|---|---|---|---|---|---|---|---|---|---|\n";
		for (size_t si=0; si<candidateSets[ci].size(); ++si) {
			const auto& st = candidateSets[ci][si];
			ofs << '|' << si;
			for (int i=0;i<5;++i) ofs << '|' << enc(st.cells[i].D);
			for (int i=0;i<5;++i) ofs << '|' << enc(st.cells[i].C);
			ofs << '|' << st.case_name << "|\n";
		}
		ofs << "\n";
	}

	ofs << "## Solver Result\n\n";
	ofs << "Chosen set count: " << result.chosen_sets.size() << "\n\n";
	for (size_t i=0;i<result.chosen_sets.size(); ++i) {
		ofs << "- Set " << result.chosen_sets[i] << " covers universe indices: ";
		const auto& cov = result.cover_report[i];
		for (size_t j=0;j<cov.size();++j) { ofs << cov[j]; if (j+1<cov.size()) ofs << ','; }
		ofs << "\n";
	}
	if (!result.uncovered_indices.empty()) {
		ofs << "\nUncovered universe indices: ";
		for (size_t i=0;i<result.uncovered_indices.size();++i) { ofs << result.uncovered_indices[i]; if (i+1<result.uncovered_indices.size()) ofs << ','; }
		ofs << "\n";
	}
}

int main(int argc, char** argv) {
	if (argc < 3) {
		std::cerr << "Usage: " << argv[0] << " <faults.json> <output.md> [--dedup]" << std::endl;
		return 1;
	}
	const string json_path = argv[1];
	const string md_path = argv[2];
	bool dedup = false;
	if (argc >= 4 && string(argv[3]) == "--dedup") dedup = true;

	try {
		steady_clock::time_point t0 = steady_clock::now();
		FaultsParser parser;
		auto faults = parser.parse_file(json_path);
		steady_clock::time_point t1 = steady_clock::now();

		CrossStateExpander expander;
		vector<ExpandedPrimitive> expanded;
		for (size_t fi=0; fi<faults.size(); ++fi) {
			const auto& f = faults[fi];
			for (size_t pi=0; pi<f.primitives.size(); ++pi) {
				ExpandedPrimitive ep; ep.fault_index = fi; ep.primitive_index = pi;
				ep.states = expander.expand(f.primitives[pi], f.cell_scope);
				expanded.push_back(std::move(ep));
			}
		}
		steady_clock::time_point t2 = steady_clock::now();

		// Universe: 所有展開 states (可選擇去重)
		vector<CrossState> universe;
		universe.reserve(64);
		if (!dedup) {
			for (auto &ep : expanded) for (auto &st : ep.states) universe.push_back(st);
		} else {
			std::unordered_map<string, CrossState> uniq; uniq.reserve(256);
			for (auto &ep : expanded) {
				for (auto &st : ep.states) {
					auto key = cross_state_key(st);
					if (!uniq.count(key)) uniq.emplace(key, st);
				}
			}
			for (auto &kv : uniq) universe.push_back(kv.second);
		}

		// Candidate sets: 先選擇「每個 primitive 的 states 為一個 set」
		vector<vector<CrossState>> candidateSets;
		candidateSets.reserve(expanded.size());
		for (auto &ep : expanded) candidateSets.push_back(ep.states);

		steady_clock::time_point t3 = steady_clock::now();
		CrossStateCoverSolver solver;
		auto result = solver.solve(universe, candidateSets);
		steady_clock::time_point t4 = steady_clock::now();

		write_markdown(md_path, faults, expanded, universe, candidateSets, result);
		steady_clock::time_point t5 = steady_clock::now();

		auto dt_parse = duration_cast<milliseconds>(t1 - t0).count();
		auto dt_expand = duration_cast<milliseconds>(t2 - t1).count();
		auto dt_solver = duration_cast<milliseconds>(t4 - t3).count();
		auto dt_output = duration_cast<milliseconds>(t5 - t4).count();

		cout << "Parse ms: " << dt_parse << ", Expand ms: " << dt_expand
			 << ", Solve ms: " << dt_solver << ", Output ms: " << dt_output << "\n";
		cout << "Chosen sets: ";
		for (size_t i=0;i<result.chosen_sets.size();++i) { cout << result.chosen_sets[i]; if (i+1<result.chosen_sets.size()) cout << ','; }
		cout << "\nOutput markdown: " << md_path << "\n";
	} catch (const std::exception& ex) {
		std::cerr << "Error: " << ex.what() << std::endl;
		return 2;
	}
	return 0;
}

