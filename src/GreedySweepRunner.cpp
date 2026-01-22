// GreedySweepRunner.cpp
// Sweep (slots, L) combinations under a total op budget using Greedy search,
// then render best result per configuration to HTML and export patterns to JSON.
// Terminates early when 100% coverage is achieved.

#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <unordered_map>
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

static std::vector<Fault> load_faults(const std::string& path) {
    FaultsJsonParser p;
    FaultNormalizer n;
    auto raws = p.parse_file(path);
    std::vector<Fault> out;
    out.reserve(raws.size());
    for (const auto& rf : raws) {
        out.push_back(n.normalize(rf));
    }
    return out;
}

static std::vector<TestPrimitive> gen_tps(const std::vector<Fault>& faults) {
    TPGenerator g;
    std::vector<TestPrimitive> tps;
    tps.reserve(faults.size() * 2);
    for (const auto& f : faults) {
        auto v = g.generate(f);
        tps.insert(tps.end(), v.begin(), v.end());
    }
    return tps;
}

// 產生所有合法 (slots, L) 組合
// 排序：先按總 op 數升序，再按 slots 升序
struct Config {
    std::size_t slots;
    std::size_t L;
    std::size_t total_ops() const { return slots * L; }
};

static std::vector<Config> generate_valid_configs(
    std::size_t start_slots,
    std::size_t start_L,
    std::size_t max_slots,
    std::size_t max_L
) {
    std::vector<Config> configs;
    
    for (std::size_t s = start_slots; s <= max_slots; ++s) {
        for (std::size_t l = start_L; l <= max_L; ++l) {
            configs.push_back({s, l});
        }
    }
    
    // 排序：總 op 數升序，同數量時 slots 升序
    std::sort(configs.begin(), configs.end(), [](const Config& a, const Config& b) {
        if (a.total_ops() != b.total_ops())
            return a.total_ops() < b.total_ops();
        return a.slots < b.slots;
    });
    
    return configs;
}

// Entry point for Generate mode (called from main.cpp)
int run_greedy_sweep(int argc, char** argv){
    if (argc > 1 && (std::string(argv[1]) == "-h" || std::string(argv[1]) == "--help")) {
        std::cerr << "Generate Mode - Greedy Template Search\n\n"
                  << "Usage: cim-atpg --mode generate [faults.json] [output.json] [output.html] [options]\n\n"
                  << "  faults.json     : 輸入錯誤模型檔案 (預設: input/S_C_faults.json)\n"
                  << "  output.json     : 輸出 JSON 檔案 (預設: output/GreedySweep_Bests.json)\n"
                  << "  output.html     : 輸出 HTML 報表 (預設: output/GreedySweep_Bests.html)\n\n"
                  << "Options:\n"
                  << "  --start-slots N : 起始 slots 數 (預設 1)\n"
                  << "  --start-L M     : 起始 L 數 (預設 1)\n"
                  << "  --max-slots X   : 單一 element 最大 op 數 (預設 4)\n"
                  << "  --max-L Y       : 最大 element 數量 (預設 6)\n\n"
                  << "範例:\n"
                  << "  cim-atpg --mode generate\n"
                  << "  cim-atpg --mode generate faults.json out.json out.html\n"
                  << "  cim-atpg --mode generate --max-slots 5 --max-L 8\n";
        return 0;
    }

    // 解析參數 - 設定預設值
    std::string faults_path = "input/S_C_faults.json";
    std::string out_json = "output/GreedySweep_Bests.json";
    std::string out_html = "output/GreedySweep_Bests.html";
    std::size_t start_slots = 1;
    std::size_t start_L = 1;
    std::size_t max_slots = 4;  // 預設 max_slots=4
    std::size_t max_L = 6;      // 預設 max_L=6

    // 解析位置參數與選項
    int argi = 1;
    // 位置參數 (非 -- 開頭)
    if (argi < argc && argv[argi][0] != '-') faults_path = argv[argi++];
    if (argi < argc && argv[argi][0] != '-') out_json = argv[argi++];
    if (argi < argc && argv[argi][0] != '-') out_html = argv[argi++];
    
    // 選項參數
    while (argi < argc) {
        std::string arg = argv[argi];
        if (arg == "--start-slots" && argi + 1 < argc) {
            start_slots = static_cast<std::size_t>(std::stoull(argv[++argi]));
        } else if (arg == "--start-L" && argi + 1 < argc) {
            start_L = static_cast<std::size_t>(std::stoull(argv[++argi]));
        } else if (arg == "--max-slots" && argi + 1 < argc) {
            max_slots = static_cast<std::size_t>(std::stoull(argv[++argi]));
        } else if (arg == "--max-L" && argi + 1 < argc) {
            max_L = static_cast<std::size_t>(std::stoull(argv[++argi]));
        }
        ++argi;
    }

    // 產生所有合法組合
    auto configs = generate_valid_configs(start_slots, start_L, max_slots, max_L);
    if (configs.empty()) {
        std::cerr << "[Sweep] No valid (slots, L) combinations\n";
        return 3;
    }
    std::cout << "[Sweep] max_slots=" << max_slots << ", max_L=" << max_L
              << ", valid configs=" << configs.size() << "\n";

    // Load faults & tps
    auto faults = load_faults(faults_path);
    auto tps    = gen_tps(faults);
    FaultSimulator sim;

    // Collect per-configuration bests
    std::vector<CandidateResult> per_cfg_bests;
    CandidateResult overall_best;
    double best_coverage = 0.0;
    bool reached_100 = false;

    auto t_all0 = std::chrono::steady_clock::now();
    long long total_greedy_ms = 0;

    // 快取 TemplateLibrary（避免重複建立）
    std::unordered_map<std::size_t, TemplateLibrary> lib_cache;

    for (std::size_t ci = 0; ci < configs.size(); ++ci) {
        const auto& cfg = configs[ci];
        std::size_t slots = cfg.slots;
        std::size_t L = cfg.L;

        // 取得或建立 TemplateLibrary
        if (lib_cache.find(slots) == lib_cache.end()) {
            lib_cache.emplace(slots, TemplateLibrary::make_bruce(slots));
        }
        const auto& lib = lib_cache.at(slots);

        // constraints
        SequenceConstraintSet constraints;
        constraints.add(std::make_shared<FirstElementWriteOnlyConstraint>());
        constraints.add(std::make_shared<DataReadPolarityConstraint>());

        // scorer: prioritize state coverage, use total coverage as secondary, penalize op count
        auto scorer = css::template_search::make_score_state_total_ops(0.9, 0.5, 0.01);

        GreedyTemplateSearcher searcher(
            sim, lib, faults, tps,
            std::make_unique<ValueExpandingGenerator>(),
            scorer,
            &constraints
        );

        auto t0 = std::chrono::steady_clock::now();
        CandidateResult best = searcher.run(L);
        auto t1 = std::chrono::steady_clock::now();
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
        total_greedy_ms += elapsed_ms;

        if (best.march_test.elements.size() > 0) {
            best.march_test.name = std::string("Best_slots") + std::to_string(slots) + "_L" + std::to_string(L);
            per_cfg_bests.push_back(best);

            double cov = best.sim_result.total_coverage;
            std::cout << "[Sweep] (" << (ci+1) << "/" << configs.size() << ") "
                      << "slots=" << slots << ", L=" << L << " (ops=" << cfg.total_ops() << ") "
                      << "-> cov=" << std::fixed << std::setprecision(4) << (cov * 100.0) << "% "
                      << "[" << elapsed_ms << " ms]\n";

            // 更新最佳結果
            if (cov > best_coverage) {
                best_coverage = cov;
                overall_best = best;
            }

            // 檢查是否達到 100% 覆蓋率
            if (cov >= 1.0 - 1e-9) {
                std::cout << "\n[Sweep] *** 100% coverage achieved! Early termination. ***\n";
                reached_100 = true;
                break;
            }
        } else {
            std::cout << "[Sweep] (" << (ci+1) << "/" << configs.size() << ") "
                      << "slots=" << slots << ", L=" << L << " -> no valid candidate\n";
        }
    }

    auto t_all1 = std::chrono::steady_clock::now();
    auto sweep_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t_all1 - t_all0).count();

    if (per_cfg_bests.empty()) {
        std::cerr << "[Sweep] No candidate found. Check constraints or parameters.\n";
        return 3;
    }

    // 依覆蓋率降序排序
    std::sort(per_cfg_bests.begin(), per_cfg_bests.end(), [](const CandidateResult& a, const CandidateResult& b){
        return a.sim_result.total_coverage > b.sim_result.total_coverage;
    });

    // 1) Export to JSON
    auto op_to_pattern_token = [](const Op& op) -> std::string {
        switch (op.kind) {
            case OpKind::Read:
                return std::string("R") + (op.value == Val::One ? "1" : "0");
            case OpKind::Write:
                return std::string("W") + (op.value == Val::One ? "1" : "0");
            case OpKind::ComputeAnd: {
                auto b = [](Val v) { return v == Val::One ? "1" : "0"; };
                return std::string("C(") + b(op.C_T) + ")(" + b(op.C_M) + ")(" + b(op.C_B) + ")";
            }
        }
        return "?";
    };
    
    auto build_pattern_element = [&](const MarchElement& e) {
        char addr = 'b';
        if (e.order == AddrOrder::Up)
            addr = 'a';
        else if (e.order == AddrOrder::Down)
            addr = 'd';
        
        std::ostringstream oss;
        oss << addr << '(';
        for (size_t i = 0; i < e.ops.size(); ++i) {
            if (i) oss << ", ";
            oss << op_to_pattern_token(e.ops[i]);
        }
        oss << ')';
        return oss.str();
    };

    nlohmann::json arr = nlohmann::json::array();
    for (const auto& cr : per_cfg_bests) {
        std::ostringstream pattern;
        for (size_t i = 0; i < cr.march_test.elements.size(); ++i) {
            pattern << build_pattern_element(cr.march_test.elements[i]) << ';';
            if (i + 1 < cr.march_test.elements.size())
                pattern << ' ';
        }
        
        nlohmann::json obj;
        obj["March_test"] = cr.march_test.name;
        obj["Pattern"] = pattern.str();
        obj["total_coverage"] = cr.sim_result.total_coverage;
        obj["state_coverage"] = cr.sim_result.state_coverage;
        arr.push_back(std::move(obj));
    }
    
    // Write JSON file
    {
        auto parent = std::filesystem::path(out_json).parent_path();
        if (!parent.empty()) {
            std::error_code ec;
            std::filesystem::create_directories(parent, ec);
            if (ec) {
                std::cerr << "[Sweep] Warning: cannot create JSON parent directory '" << parent.string()
                          << "' : " << ec.message() << "\n";
            }
        }
    }
    std::ofstream ofs(out_json);
    if (ofs.is_open()) {
        ofs << arr.dump(2);
        std::cout << "[Sweep] JSON written: " << out_json
                  << " (" << per_cfg_bests.size() << " items)\n";
    } else {
        std::cerr << "[Sweep] Failed to write JSON: " << out_json << "\n";
    }

    // 2) Render HTML
    {
        auto html_parent = std::filesystem::path(out_html).parent_path();
        if (!html_parent.empty()) {
            std::error_code ec;
            std::filesystem::create_directories(html_parent, ec);
            if (ec) {
                std::cerr << "[Sweep] Warning: cannot create HTML parent directory '" << html_parent.string()
                          << "' : " << ec.message() << "\n";
            }
        }
    }
    TemplateSearchReport report;
    ScoreWeights default_weights;
    report.gen_html_with_op_scores(per_cfg_bests, out_html, default_weights, 0.0, false, tps);

    // Summary
    std::cout << "\n[Sweep] === Summary ===\n";
    std::cout << "[Sweep] Configs tested: " << per_cfg_bests.size() << "/" << configs.size() << "\n";
    std::cout << "[Sweep] Best coverage: " << std::fixed << std::setprecision(4) << (best_coverage * 100.0) << "%\n";
    if (!overall_best.march_test.name.empty()) {
        std::cout << "[Sweep] Best config: " << overall_best.march_test.name << "\n";
    }
    std::cout << "[Sweep] Reached 100%: " << (reached_100 ? "Yes" : "No") << "\n";
    std::cout << "[Sweep] Total elapsed: " << sweep_ms << " ms, greedy time: " << total_greedy_ms << " ms\n";
    std::cout << "[Sweep] HTML written: " << out_html << "\n";
    
    return 0;
}
