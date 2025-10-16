// ATPG.cpp - SRP demo for GreedySynthDriver.run
// - Parse faults JSON -> normalize -> generate TPs
// - Run GreedySynthDriver to synthesize a March test
// - Simulate final test and print results to terminal

#include <iostream>
#include <iomanip>
#include <string>
#include <vector>

#include "MarchSynth.hpp" // includes FaultSimulator.hpp and FpParserAndTpGen.hpp

using std::cout;
using std::cerr;
using std::endl;
using std::string;
using std::vector;

static const char* to_order_str(AddrOrder ord) {
	switch (ord) {
		case AddrOrder::Up:   return "Up";
		case AddrOrder::Down: return "Down";
		case AddrOrder::Any:  return "Any";
	}
	return "?";
}

static string op_to_string(const Op& op) {
	switch (op.kind) {
		case OpKind::Write:
			return string("W") + (op.value == Val::One ? "1" : op.value == Val::Zero ? "0" : "X");
		case OpKind::Read:
			return string("R") + (op.value == Val::One ? "1" : op.value == Val::Zero ? "0" : "X");
		case OpKind::ComputeAnd: {
			auto v = [](Val x){ return (x==Val::One? '1' : x==Val::Zero? '0' : 'X'); };
			string s = "C("; s.push_back(v(op.C_T)); s += ")("; s.push_back(v(op.C_M)); s += ")("; s.push_back(v(op.C_B)); s += ")";
			return s;
		}
	}
	return "?";
}

static void print_march(const MarchTest& mt) {
	cout << "March Test: " << mt.name << "\n";
	for (size_t i = 0; i < mt.elements.size(); ++i) {
		const auto& e = mt.elements[i];
		cout << "  Element " << (i+1) << " (" << to_order_str(e.order) << "): ";
		const size_t previewN = 32;
		for (size_t j = 0; j < e.ops.size() && j < previewN; ++j) {
			if (j) cout << ", ";
			cout << op_to_string(e.ops[j]);
		}
		if (e.ops.size() > previewN) {
			cout << " ... (" << (e.ops.size() - previewN) << " more)";
		}
		if (e.ops.empty()) cout << "<empty>";
		cout << "\n";
	}
}

static void print_simulation(const SimulationResult& sim) {
	cout << std::fixed << std::setprecision(2);
	cout << "\nSimulation Summary:\n";
	cout << "  Total coverage: " << (sim.total_coverage * 100.0) << "%\n";
	cout << "  Ops in table:   " << sim.op_table.size() << "\n";

	// Aggregates
	size_t total_state=0, total_sens=0, total_det=0;
	for (const auto& cl : sim.cover_lists) {
		total_state += cl.state_cover.size();
		total_sens  += cl.sens_cover.size();
		total_det   += cl.det_cover.size();
	}
	cout << "  Aggregate hits: state=" << total_state
		 << ", sens=" << total_sens
		 << ", det="  << total_det << "\n";

	// Fault buckets
	size_t f_full=0, f_half=0, f_zero=0;
	for (const auto& kv : sim.fault_detail_map) {
		double c = kv.second.coverage;
		if (c >= 0.999) ++f_full; else if (c >= 0.499) ++f_half; else ++f_zero;
	}
	cout << "  Fault coverage buckets: full=" << f_full
		 << ", half=" << f_half
		 << ", zero=" << f_zero << "\n";

	// Per-op preview (top-N or first M with any hits)
	size_t shown = 0, limit = 20;
	for (size_t i = 0; i < sim.cover_lists.size() && shown < limit; ++i) {
		const auto& cl = sim.cover_lists[i];
		if (cl.state_cover.empty() && cl.sens_cover.empty() && cl.det_cover.empty()) continue;
		cout << "    op#" << (i+1)
			 << "  state=" << cl.state_cover.size()
			 << "  sens=" << cl.sens_cover.size()
			 << "  det="  << cl.det_cover.size() << "\n";
		++shown;
	}
	if (shown == 0) cout << "  (no per-op hits)\n";
}

static vector<Fault> load_faults(const string& path) {
	FaultsJsonParser fparser;
	FaultNormalizer normalizer;
	vector<RawFault> raws = fparser.parse_file(path);
	vector<Fault> faults; faults.reserve(raws.size());
	for (const auto& rf : raws) faults.push_back(normalizer.normalize(rf));
	return faults;
}

static vector<TestPrimitive> build_tps(const vector<Fault>& faults) {
	TPGenerator gen;
	vector<TestPrimitive> tps;
	for (const auto& f : faults) {
		auto per_fault = gen.generate(f);
		tps.insert(tps.end(), per_fault.begin(), per_fault.end());
	}
	return tps;
}

int main(int argc, char** argv) {
	try {
		const string faults_path = (argc >= 2) ? string(argv[1]) : string("input/S_C_faults.json");

	// 1) Parse faults and generate TPs
	auto faults = load_faults(faults_path);
	auto tps    = build_tps(faults);
	cout << "Loaded faults: " << faults.size() << ", TPs: " << tps.size() << "\n";

		// 2) Configure and run GreedySynthDriver
		SynthConfig cfg; // default weights and limits
		GreedySynthDriver driver(cfg, faults, tps);

		MarchTest init; init.name = "Synthesized March (Greedy)";
		MarchTest final_mt = driver.run(init, /*target_cov=*/1.0);

		// 3) Simulate final_mt
		FaultSimulator sim;
		SimulationResult simr = sim.simulate(final_mt, faults, tps);

		// 4) Print outputs
		print_march(final_mt);
		print_simulation(simr);

		return 0;
	} catch (const std::exception& e) {
		cerr << "Error: " << e.what() << endl;
		return 1;
	}
}

