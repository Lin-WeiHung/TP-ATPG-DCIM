// GreedySweepRunner.cpp
// Sweep slot_count (ops per element) and element count using Greedy search,
// then render best result per configuration to HTML and export patterns to JSON.

#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <nlohmann/json.hpp>

#include "TemplateSearchers.hpp"
#include "TemplateSearchReport.hpp"
#include "FpParserAndTpGen.hpp"
#include "FaultSimulator.hpp"

using css::template_search::TemplateLibrary;
using css::template_search::GreedyTemplateSearcher;
using css::template_search::CandidateResult;
using css::template_search::SequenceConstraintSet;
using css::template_search::FirstElementWriteOnlyConstraint;
using css::template_search::DataReadPolarityConstraint;
using css::template_search::ValueExpandingGenerator;

static std::vector<Fault> load_faults(const std::string& path){
    FaultsJsonParser p; FaultNormalizer n; auto raws=p.parse_file(path); std::vector<Fault> out; out.reserve(raws.size()); for(const auto& rf: raws) out.push_back(n.normalize(rf)); return out;
}
static std::vector<TestPrimitive> gen_tps(const std::vector<Fault>& faults){
    TPGenerator g; std::vector<TestPrimitive> tps; tps.reserve(faults.size()*2); for(const auto& f: faults){ auto v=g.generate(f); tps.insert(tps.end(), v.begin(), v.end()); } return tps;
}

int main(int argc, char** argv){
    if (argc < 3) {
        std::cerr << "Usage: " << (argc>0?argv[0]:"greedy_sweep")
                  << " <max_ops_per_element> <max_elements> [faults.json] [output.json] [output.html]\n";
        return 2;
    }
    std::size_t max_ops = static_cast<std::size_t>(std::stoull(argv[1]));
    std::size_t max_elements = static_cast<std::size_t>(std::stoull(argv[2]));
    std::string faults_path = (argc > 3) ? argv[3] : std::string("input/S_C_faults.json");
    std::string out_json = (argc > 4) ? argv[4] : std::string("output/GreedySweep_Bests.json");
    std::string out_html = (argc > 5) ? argv[5] : std::string("output/GreedySweep_Bests.html");

    // Load faults & tps
    auto faults = load_faults(faults_path);
    auto tps    = gen_tps(faults);
    FaultSimulator sim;

    // Collect per-configuration bests
    std::vector<CandidateResult> per_cfg_bests;

    auto t_all0 = std::chrono::steady_clock::now();
    long long total_greedy_ms = 0;

    for (std::size_t slots = 1; slots <= max_ops; ++slots) {
        TemplateLibrary lib = TemplateLibrary::make_bruce(slots);
        std::cout << "[Sweep] slots=" << slots << " lib.size=" << lib.size() << std::endl;

        // constraints used during greedy
        SequenceConstraintSet constraints;
        constraints.add(std::make_shared<FirstElementWriteOnlyConstraint>());
        constraints.add(std::make_shared<DataReadPolarityConstraint>());

        for (std::size_t L = 1; L <= max_elements; ++L) {
            std::cout << "  [Greedy] L=" << L << std::endl;
            GreedyTemplateSearcher searcher(
                sim, lib, faults, tps,
                std::make_unique<ValueExpandingGenerator>(),
                css::template_search::make_score_state_total_ops(0.9, 0.5, 0.01),
                &constraints
            );
            auto t0 = std::chrono::steady_clock::now();
            CandidateResult best = searcher.run(L);
            auto t1 = std::chrono::steady_clock::now();
            total_greedy_ms += std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

            if (best.march_test.elements.size() > 0) {
                best.march_test.name = std::string("Best_ops") + std::to_string(slots) + "_elems" + std::to_string(L);
                per_cfg_bests.push_back(std::move(best));
            }
        }
    }
    auto t_all1 = std::chrono::steady_clock::now();
    auto sweep_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t_all1 - t_all0).count();

    if (per_cfg_bests.empty()) {
        std::cerr << "[Sweep] No candidate found. Check constraints or parameters." << std::endl;
        return 3;
    }

    // Sort by total coverage desc for better readability
    std::sort(per_cfg_bests.begin(), per_cfg_bests.end(), [](const CandidateResult& a, const CandidateResult& b){
        return a.sim_result.total_coverage > b.sim_result.total_coverage;
    });

    // 1) Export per-config bests to JSON (March_test/Pattern)
    auto op_to_pattern_token = [](const Op& op)->std::string{
        switch(op.kind){
            case OpKind::Read:  return std::string("R") + (op.value==Val::One?"1":"0");
            case OpKind::Write: return std::string("W") + (op.value==Val::One?"1":"0");
            case OpKind::ComputeAnd: {
                auto b=[](Val v){ return v==Val::One?"1":"0"; };
                return std::string("C(") + b(op.C_T) + ")(" + b(op.C_M) + ")(" + b(op.C_B) + ")";
            }
        }
        return "?";
    };
    auto build_pattern_element = [&](const MarchElement& e){
        char addr='b'; if(e.order==AddrOrder::Up) addr='a'; else if(e.order==AddrOrder::Down) addr='d';
        std::ostringstream oss; oss<<addr<<'(';
        for(size_t i=0;i<e.ops.size();++i){ if(i) oss<<", "; oss<<op_to_pattern_token(e.ops[i]); }
        oss<<')'; return oss.str();
    };

    nlohmann::json arr = nlohmann::json::array();
    for(const auto& cr : per_cfg_bests){
        std::ostringstream pattern;
        for(size_t i=0;i<cr.march_test.elements.size();++i){
            pattern << build_pattern_element(cr.march_test.elements[i]) << ';';
            if(i+1<cr.march_test.elements.size()) pattern << ' ';
        }
        nlohmann::json obj; obj["March_test"] = cr.march_test.name; obj["Pattern"] = pattern.str();
        arr.push_back(std::move(obj));
    }
    {
        std::filesystem::create_directories(std::filesystem::path(out_json).parent_path());
        std::ofstream ofs(out_json);
        if(ofs.is_open()){ ofs<<arr.dump(2); std::cout << "[Sweep] JSON written: "<< out_json << " ("<< per_cfg_bests.size() <<" items)\n"; }
        else { std::cerr << "[Sweep] Failed to write JSON: "<< out_json << "\n"; }
    }

    // 2) Render HTML via gen_html_from_march_json using already simulated results
    TemplateSearchReport report;
    report.gen_html_from_march_json(std::filesystem::path(out_json).filename().string(), per_cfg_bests, out_html);

    std::cout << "[Sweep] Done. Items=" << per_cfg_bests.size() << "\n";
    std::cout << "[Sweep] Total elapsed=" << sweep_ms << " ms, greedy time=" << total_greedy_ms << " ms\n";
    std::cout << "[Sweep] HTML written: " << out_html << std::endl;
    return 0;
}
