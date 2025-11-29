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
        std::cerr << "Usage: " << (argc>0?argv[0]:"greedy_sweep") << " <max_ops_per_element> <max_elements>"
#ifdef USE_OP_SCORER
                  << " [faults.json] [output.json] [output.html] [--opscore alpha_S beta_D gamma_MPart lambda_MAll op_penalty]\n";
#else
                  << " [faults.json] [output.json] [output.html]\n";
#endif
        return 2;
    }
    std::size_t max_ops = static_cast<std::size_t>(std::stoull(argv[1]));
    std::size_t max_elements = static_cast<std::size_t>(std::stoull(argv[2]));
    std::string faults_path = (argc > 3) ? argv[3] : std::string("input/S_C_faults.json");
    std::string out_json = (argc > 4) ? argv[4] : std::string("output/GreedySweep_Bests.json");
    std::string out_html = (argc > 5) ? argv[5] : std::string("output/GreedySweep_Bests.html");

    int argi = 6;
    bool use_opscore = false;
    ScoreWeights sw; // defaults
    double op_penalty = 0.0; // default no penalty
#ifdef USE_OP_SCORER
    if (argc > argi && std::string(argv[argi]) == "--opscore") {
        if (argc < argi + 7) {
            std::cerr << "[Error] --opscore requires 6 numeric params: alpha_state alpha_sens beta_D gamma_MPart lambda_MAll op_penalty\n";
            return 3;
        }
        use_opscore = true;
        sw.alpha_state = std::stod(argv[argi+1]);
        sw.alpha_sens  = std::stod(argv[argi+2]);
        sw.beta_D      = std::stod(argv[argi+3]);
        sw.gamma_MPart = std::stod(argv[argi+4]);
        sw.lambda_MAll = std::stod(argv[argi+5]);
        op_penalty     = std::stod(argv[argi+6]);
    }
#endif

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
            css::template_search::ScoreFunc scorer;
#ifdef USE_OP_SCORER
            if (use_opscore) {
                // OpScorer-based scoring: sum(op_score_outcomes.total_score) - op_penalty * total_ops
                // Build a shared OpScorer with TP grouping once
                static OpScorer op_scorer; // reused across candidates
                op_scorer.set_group_index(tps);
                op_scorer.set_weights(sw);
                scorer = [&, op_penalty](const SimulationResult& sim_res, const MarchTest& mt){
                    auto outcomes = op_scorer.score_ops(sim_res.cover_lists);
                    double sum = 0.0; for (const auto& o : outcomes) sum += o.total_score;
                    std::size_t ops_count = 0; for (const auto& e : mt.elements) ops_count += e.ops.size();
                    return sum - op_penalty * static_cast<double>(ops_count);
                };
            } else {
                scorer = css::template_search::make_score_state_total_ops(0.9, 0.5, 0.01);
            }
#else
            scorer = css::template_search::make_score_state_total_ops(0.9, 0.5, 0.01);
#endif

            GreedyTemplateSearcher searcher(
                sim, lib, faults, tps,
                std::make_unique<ValueExpandingGenerator>(),
                scorer,
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

    // 2) Render HTML with per-op scores dropdown
    TemplateSearchReport report;
#ifdef USE_OP_SCORER
    report.gen_html_with_op_scores(per_cfg_bests, out_html, sw, op_penalty, use_opscore, tps);
#else
    // use default weights matching earlier scorer
    ScoreWeights default_w; // defaults already set in struct
    report.gen_html_with_op_scores(per_cfg_bests, out_html, default_w, 0.0, false, tps);
#endif

    std::cout << "[Sweep] Done. Items=" << per_cfg_bests.size() << "\n";
#ifdef USE_OP_SCORER
    if (use_opscore) {
        std::cout << "[Sweep] OpScorer weights: alpha_state="<<sw.alpha_state<<" alpha_sens="<<sw.alpha_sens
                  <<" beta_D="<<sw.beta_D<<" gamma_MPart="<<sw.gamma_MPart<<" lambda_MAll="<<sw.lambda_MAll
                  <<" op_penalty="<<op_penalty<<"\n";
    }
#endif
    std::cout << "[Sweep] Total elapsed=" << sweep_ms << " ms, greedy time=" << total_greedy_ms << " ms\n";
    std::cout << "[Sweep] HTML written: " << out_html << std::endl;
    return 0;
}
