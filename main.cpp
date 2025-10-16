#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "FpParserAndTpGen.hpp"
#include "MarchSynth.hpp"

using std::cerr;
using std::cout;
using std::endl;
using std::fixed;
using std::setprecision;
using std::size_t;
using std::string;
using std::vector;

namespace {

string order_to_string(AddrOrder order) {
    switch (order) {
        case AddrOrder::Up: return "Up";
        case AddrOrder::Down: return "Down";
        case AddrOrder::Any: return "Any";
    }
    return "?";
}

string op_to_string(const Op& op) {
    if (op.kind == OpKind::Write) {
        return string("W") + (op.value == Val::Zero ? "0" : "1");
    }
    if (op.kind == OpKind::Read) {
        return string("R") + (op.value == Val::Zero ? "0" : "1");
    }
    string out = "C(";
    out += (op.C_T == Val::One ? "1" : (op.C_T == Val::Zero ? "0" : "-"));
    out += ")(";
    out += (op.C_M == Val::One ? "1" : (op.C_M == Val::Zero ? "0" : "-"));
    out += ")(";
    out += (op.C_B == Val::One ? "1" : (op.C_B == Val::Zero ? "0" : "-"));
    out += ")";
    return out;
}

void print_march(const MarchTest& test) {
    cout << "March Test: " << test.name << "\n";
    for (size_t i = 0; i < test.elements.size(); ++i) {
        const auto& elem = test.elements[i];
        cout << "  Element " << i << " (" << order_to_string(elem.order) << "): ";
        if (elem.ops.empty()) {
            cout << "<empty>\n";
            continue;
        }
        for (size_t j = 0; j < elem.ops.size(); ++j) {
            if (j) cout << ", ";
            cout << op_to_string(elem.ops[j]);
        }
        cout << "\n";
    }
}

}

int main(int argc, char** argv) {
    if (argc != 2) {
        cerr << "Usage: " << (argc > 0 ? argv[0] : "synth_demo")
             << " <faults.json>\n";
        return 2;
    }

    try {
        const string faults_path = argv[1];

        FaultsJsonParser fparser;
        FaultNormalizer fnorm;
        TPGenerator generator;

        auto raw_faults = fparser.parse_file(faults_path);
        vector<Fault> faults;
        faults.reserve(raw_faults.size());
        vector<TestPrimitive> all_tps;

        for (const auto& raw : raw_faults) {
            faults.push_back(fnorm.normalize(raw));
        }
        for (const auto& fault : faults) {
            auto tps = generator.generate(fault);
            all_tps.insert(all_tps.end(), tps.begin(), tps.end());
        }

        if (faults.empty()) {
            throw std::runtime_error("Fault list is empty after normalization.");
        }
        if (all_tps.empty()) {
            throw std::runtime_error("No test primitives generated from provided faults.");
        }

        SynthConfig config;
        config.beam_width = 4;
        config.max_ops = 32;
        config.target_coverage = 1.0;
        config.alpha = 1.0;
        config.beta = 1.0;
        config.gamma = 2.0;
        config.lambda = 0.0;
        config.mu = 0.05;
        config.defer_detect_only = true;
    config.max_ops_per_element = 6;
        config.initial_order = AddrOrder::Up;

        FaultSimulator simulator;
        MarchSynthesizer synthesizer(simulator, faults, all_tps, config);

        MarchTest generated = synthesizer.synthesize("Synthesized March");
        SimulationResult summary = simulator.simulate(generated, faults, all_tps);

       print_march(generated);
       cout << fixed << setprecision(2);
       cout << "Total coverage: " << (summary.total_coverage * 100.0) << "%\n";
       cout << "Ops expanded: " << summary.op_table.size() << "\n";
       CoverageStats stats = make_stats(summary);
       cout << "State hits: " << stats.state_hits
           << ", Sens hits: " << stats.sens_hits
           << ", Detect hits: " << stats.detect_hits << "\n";

        return 0;
    } catch (const std::exception& ex) {
        cerr << "Synth demo error: " << ex.what() << endl;
        return 1;
    }
}
