// TemplateSearchers.cpp
// Run Greedy and Beam template searches and export the top results to HTML.
// Single-responsibility helpers are provided for clarity and reuse.

#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iomanip>
#include <chrono>
#include <queue>
#include <limits>
#include <memory> // v2: for std::make_shared constraints

#include "FpParserAndTpGen.hpp"
#include "FaultSimulator.hpp"
#include "TemplateSearchers.hpp"
#include "TemplateSearchReport.hpp" // new HTML report class

using std::string; using std::vector; using std::ofstream; using std::ostringstream; using std::size_t;
using css::template_search::TemplateLibrary; using css::template_search::TemplateEnumerator; using css::template_search::TemplateOpKind;
using css::template_search::GreedyTemplateSearcher; using css::template_search::BeamTemplateSearcher; using css::template_search::CandidateResult;
using css::template_search::ValueExpandingGenerator;
using css::template_search::SequenceConstraintSet; // v2: sequence constraints container
using css::template_search::FirstElementWriteOnlyConstraint; // v2: first element must be W-only
using css::template_search::DataReadPolarityConstraint; // v2: read polarity constraint tied to written data
using css::template_search::FirstElementHasWriteNoReadConstraint; // v3: first element must have write and no read

// ------------------------------
// Single-responsibility helpers
// ------------------------------

// Load faults from JSON
static vector<Fault> load_faults(const string& path){
    FaultsJsonParser p; FaultNormalizer n;
    auto raws = p.parse_file(path);
    vector<Fault> out; out.reserve(raws.size());
    for (const auto& rf: raws) out.push_back(n.normalize(rf));
    return out;
}

// Generate test primitives from faults
static vector<TestPrimitive> gen_tps(const vector<Fault>& faults){
    TPGenerator g; vector<TestPrimitive> tps; tps.reserve(faults.size()*2);
    for (const auto& f: faults){ auto v=g.generate(f); tps.insert(tps.end(), v.begin(), v.end()); }
    return tps;
}

// Build a brute-force template library (valid elements only)
static TemplateLibrary make_bruce_lib(std::size_t slot_count){
    TemplateLibrary lib = TemplateLibrary::make_bruce(slot_count);
    return lib;
}

// Render an Op to compact string (R0/W1/C(...))
static string op_to_str(const Op& op){
    switch(op.kind){
        case OpKind::Read:  return string("R") + (op.value==Val::One?"1":"0");
        case OpKind::Write: return string("W") + (op.value==Val::One?"1":"0");
        case OpKind::ComputeAnd: {
            auto b = [](Val v){ return v==Val::One?"1":"0"; };
            ostringstream ss; ss << "C("<<b(op.C_T)<<","<<b(op.C_M)<<","<<b(op.C_B)<<")"; return ss.str();
        }
    }
    return "?";
}
// Pattern JSON export helpers
static string op_to_pattern_token(const Op& op){
    switch(op.kind){
        case OpKind::Read:  return string("R") + (op.value==Val::One?"1":"0");
        case OpKind::Write: return string("W") + (op.value==Val::One?"1":"0");
        case OpKind::ComputeAnd: {
            auto b=[](Val v){ return v==Val::One?"1":"0"; };
            return string("C(") + b(op.C_T) + ")(" + b(op.C_M) + ")(" + b(op.C_B) + ")";
        }
    }
    return "?";
}
static string build_pattern_element(const MarchElement& e){
    char addr='b'; if(e.order==AddrOrder::Up) addr='a'; else if(e.order==AddrOrder::Down) addr='d';
    ostringstream oss; oss<<addr<<'(';
    for(size_t i=0;i<e.ops.size();++i){ if(i) oss<<", "; oss<<op_to_pattern_token(e.ops[i]); }
    oss<<')'; return oss.str();
}
static void write_all_results_json(const vector<CandidateResult>& greedy_list,
                                   const vector<CandidateResult>& beam_list,
                                   const string& base_path){
    vector<const CandidateResult*> all; all.reserve(greedy_list.size()+beam_list.size());
    for(const auto& g: greedy_list) all.push_back(&g);
    for(const auto& b: beam_list) all.push_back(&b);
    std::sort(all.begin(), all.end(), [](const CandidateResult* a, const CandidateResult* b){ return a->sim_result.total_coverage > b->sim_result.total_coverage; });
    string json_path;
    if(!base_path.empty()){
        if(base_path.size() >= 5 && base_path.substr(base_path.size()-5) == ".json") {
            json_path = base_path; // explicit filename
        } else {
            auto pos = base_path.rfind('.');
            if(pos!=string::npos) json_path = base_path.substr(0,pos) + "_all.json"; else json_path = base_path + "_all.json";
        }
    } else {
        json_path = "template_search_all.json"; // fallback
    }
    nlohmann::json arr = nlohmann::json::array();
    for(size_t rank=0; rank<all.size(); ++rank){
        const CandidateResult* cr = all[rank];
        ostringstream pattern;
        for(size_t ei=0; ei<cr->march_test.elements.size(); ++ei){
            pattern << build_pattern_element(cr->march_test.elements[ei]) << ';';
            if(ei+1 < cr->march_test.elements.size()) pattern << ' ';
        }
        nlohmann::json obj; obj["March_test"] = string("Rank") + std::to_string(rank+1); obj["Pattern"] = pattern.str();
        arr.push_back(std::move(obj));
    }
    std::ofstream ofs(json_path);
    if(ofs.is_open()){ ofs<<arr.dump(2); std::cout<<"[Export] All results JSON written: "<<json_path<<" ("<<all.size()<<" items)"<<std::endl; }
    else { std::cerr<<"[Export] Failed to open all-results JSON path: "<<json_path<<std::endl; }
}

// Escape minimal HTML

// Run greedy and return up to top_k prefix results (one per level)
// Helper: unify greedy best (single) with beam list, remove duplicate sequences, sort by score desc
static vector<CandidateResult> unify_results(const CandidateResult& greedy_best,
                                             const vector<CandidateResult>& beam_list){
    vector<CandidateResult> combined;
    combined.reserve(beam_list.size()+1);
    auto same_seq = [](const vector<TemplateLibrary::TemplateId>& a, const vector<TemplateLibrary::TemplateId>& b){
        return a.size()==b.size() && std::equal(a.begin(), a.end(), b.begin());
    };
    bool duplicate = false;
    for(const auto& b: beam_list){ if(same_seq(greedy_best.sequence, b.sequence)){ duplicate = true; break; } }
    if(!duplicate) combined.push_back(greedy_best);
    for(const auto& b: beam_list) combined.push_back(b);
    std::sort(combined.begin(), combined.end(), [](const CandidateResult& a, const CandidateResult& b){ return a.score > b.score; });
    return combined;
}

// Run beam and return final top_k results
static vector<CandidateResult> run_beam_topk(FaultSimulator& sim,
                                             const TemplateLibrary& lib,
                                             const vector<Fault>& faults,
                                             const vector<TestPrimitive>& tps,
                                             size_t L,
                                             size_t beam_width,
                                             size_t top_k,
                                             css::template_search::ScoreFunc scorer){
    // v2: install same sequence constraints for beam search
    auto gen = std::make_unique<ValueExpandingGenerator>(); // v2
    SequenceConstraintSet seq_constraints; // v2
    seq_constraints.add(std::make_shared<FirstElementWriteOnlyConstraint>()); // v2
    seq_constraints.add(std::make_shared<DataReadPolarityConstraint>()); // v2
    // v3: use custom score function and progress callback
    std::cout << "[Beam] Start: L="<< L << ", beam_width="<< beam_width << ", lib="<< lib.size() << std::endl;
    auto progress = [L](std::size_t level, std::size_t candidates, std::size_t kept){
        std::cout << "[Beam] Level "<< level << "/"<< L << ": candidates="<< candidates << ", kept="<< kept << std::endl;
    };
    BeamTemplateSearcher searcher(sim, lib, faults, tps, beam_width, std::move(gen), std::move(scorer), &seq_constraints, progress); // custom scorer + progress
    return searcher.run(L, top_k);
}

// Public entry: run full pipeline and emit HTML
int run_template_search_html(const string& faults_json_path,
                             size_t skeleton_length,
                             size_t beam_width,
                             const string& output_html_path,
                             const string& json_out_path,
                             css::template_search::ScoreFunc scorer,
                             double w_state,
                             double w_total,
                             double op_penalty,
                             std::size_t slot_count){
    auto faults = load_faults(faults_json_path);
    auto tps    = gen_tps(faults);

    FaultSimulator sim;
    auto lib = make_bruce_lib(slot_count);

    // Greedy best (single)
    SequenceConstraintSet greedy_constraints;
    greedy_constraints.add(std::make_shared<FirstElementWriteOnlyConstraint>());
    greedy_constraints.add(std::make_shared<DataReadPolarityConstraint>());
    GreedyTemplateSearcher greedy_searcher(sim, lib, faults, tps, std::make_unique<ValueExpandingGenerator>(), scorer, &greedy_constraints);
    auto t0 = std::chrono::steady_clock::now();
    CandidateResult greedy_best = greedy_searcher.run(skeleton_length);
    auto t1 = std::chrono::steady_clock::now();
    auto greedy_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    std::cout << "[Greedy] Elapsed: "<< greedy_ms << " ms" << std::endl;

    // Beam finals
    const size_t TOPK_BEAM = beam_width;
    auto t2 = std::chrono::steady_clock::now();
    auto beam_list = run_beam_topk(sim, lib, faults, tps, skeleton_length, beam_width, TOPK_BEAM, scorer);
    auto t3 = std::chrono::steady_clock::now();
    auto beam_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t3 - t2).count();
    std::cout << "[Beam] Elapsed: "<< beam_ms << " ms" << std::endl;

    auto combined = unify_results(greedy_best, beam_list);
    TemplateSearchReport{}.gen_html(combined, output_html_path, w_state, w_total, op_penalty, slot_count, greedy_ms, beam_ms);
    // For JSON export keep legacy merger (vector with single greedy element)
    vector<CandidateResult> greedy_vec; greedy_vec.push_back(greedy_best);
    write_all_results_json(greedy_vec, beam_list, json_out_path.empty()?output_html_path:json_out_path);
    return 0;
}

// Optional standalone main (guarded) to quickly run from CLI without touching project main
#ifdef TEMPLATE_SEARCHERS_STANDALONE
// Usage:
//   template_searchers <faults.json> <L> <beam_width> <output.html> [--slots N] [w_state w_total op_penalty]
// Defaults: slots=3, w_state=1.0, w_total=0.5, op_penalty=0.01
int main(int argc, char** argv){
    if(argc < 5){
        std::cerr << "Usage: " << (argc>0?argv[0]:"template_searchers")
                  << " <faults.json> <L> <beam_width> <output.html> [--slots N] [--json results.json] [w_state w_total op_penalty]\n";
        return 2;
    }
    std::string faults = argv[1];
    size_t L = static_cast<size_t>(std::stoul(argv[2]));
    size_t BW = static_cast<size_t>(std::stoul(argv[3]));
    std::string out = argv[4];

    // Optional args parsing: [--slots N] [w_state w_total op_penalty]
    std::size_t slot_count = 3;
    double w_state = 1.0, w_total = 0.5, op_penalty = 0.01;
    std::string json_path_override;

    int idx = 5;
    if (idx <= argc-2 && std::string(argv[idx]) == "--slots") {
        slot_count = static_cast<std::size_t>(std::stoull(argv[idx+1]));
        idx += 2;
    }
    if (idx <= argc-2 && std::string(argv[idx]) == "--json") {
        json_path_override = argv[idx+1];
        idx += 2;
    }
    if (idx <= argc-1) w_state = std::stod(argv[idx++]);
    if (idx <= argc-1) w_total = std::stod(argv[idx++]);
    if (idx <= argc-1) op_penalty = std::stod(argv[idx++]);

    auto scorer = css::template_search::make_score_state_total_ops(w_state, w_total, op_penalty);
    return run_template_search_html(faults, L, BW, out, json_path_override, scorer, w_state, w_total, op_penalty, slot_count);
}
#endif

// (Removed prior anonymous namespace streaming implementation; replaced by BeamTemplateSearcher::run_stream method.)
static vector<CandidateResult> run_greedy_prefixes(FaultSimulator& sim,
                                                   const TemplateLibrary& lib,
                                                   const vector<Fault>& faults,
                                                   const vector<TestPrimitive>& tps,
                                                   size_t L,
                                                   size_t top_k,
                                                   css::template_search::ScoreFunc scorer){
    vector<CandidateResult> out;

    css::template_search::ValueExpandingGenerator defGen; // v2: value-expanding generator (enumerates R/W/C permutations)

    // v2: install sequence constraints (first element W-only + data read polarity)
    SequenceConstraintSet seq_constraints; // v2
    seq_constraints.add(std::make_shared<FirstElementWriteOnlyConstraint>()); // v2
    seq_constraints.add(std::make_shared<DataReadPolarityConstraint>()); // v2

    css::template_search::PrefixState prefix_state; // v2: track data state & length for constraints

    MarchTest prefix_mt; prefix_mt.name = "greedy_prefix";
    vector<TemplateLibrary::TemplateId> chosen_ids; chosen_ids.reserve(L);

    std::cout << "[Greedy] Start: L="<< L << ", lib="<< lib.size() << std::endl;

    for(size_t pos=0; pos<L && out.size()<top_k; ++pos){
        double best_score = -1e100; TemplateLibrary::TemplateId best_tid=0; MarchElement best_elem; SimulationResult best_sim;
        for(size_t tid=0; tid<lib.size(); ++tid){
            auto elems = defGen.generate(lib, tid);
            for(auto &elem_variant : elems){
                // v2: apply sequence constraints before simulate
                if(!seq_constraints.allow(prefix_state, elem_variant, pos)) continue; // v2

                MarchTest trial = prefix_mt; if(!elem_variant.ops.empty()) trial.elements.push_back(elem_variant);
                SimulationResult simres = sim.simulate(trial, faults, tps);
                // use injected scorer (factory lambda captures custom weights)
                double score = scorer(simres, trial);
                if(score > best_score){ best_score = score; best_tid = tid; best_elem = elem_variant; best_sim = std::move(simres); }
            }
        }
        // commit best for this position
        prefix_mt.elements.push_back(best_elem);
        chosen_ids.push_back(best_tid);

        // v2: update prefix_state through constraints set
        seq_constraints.update(prefix_state, best_elem, pos); // v2

        CandidateResult cr; cr.sequence = chosen_ids; cr.march_test = prefix_mt; cr.sim_result = best_sim; cr.score = scorer(best_sim, prefix_mt); // score via injected scorer
        out.push_back(std::move(cr));

        std::cout << "[Greedy] Step "<< (pos+1) << "/"<< L << ": best_tid="<< best_tid << ", ops="<< best_elem.ops.size() << ", score="<< out.back().score << std::endl;
    }
    std::cout << "[Greedy] Done. Prefixes generated="<< out.size() << std::endl;
    return out;
}
// =============================
// Streaming demo main (optional)
// =============================
#ifdef TEMPLATE_SEARCHERS_STREAMING_DEMO
int main(int argc, char** argv){
    if(argc < 5){
        std::cerr << "Usage: " << (argc>0?argv[0]:"template_searchers_stream")
                  << " <faults.json> <L> <beam_width> <output.html> [--slots N] [--json results.json] [--cap M] [w_state w_total op_penalty]\n";
        return 2;
    }
    std::string faults = argv[1];
    std::size_t L = static_cast<std::size_t>(std::stoull(argv[2]));
    std::size_t BW = static_cast<std::size_t>(std::stoull(argv[3]));
    std::string out = argv[4];

    std::size_t slot_count = 4; // default
    std::size_t expand_cap = std::numeric_limits<std::size_t>::max(); // default: unlimited cap per template element
    std::string json_path_override;
    double w_state = 0.9, w_total = 0.5, op_penalty = 0.01;

    int idx = 5;
    if (idx <= argc-2 && std::string(argv[idx]) == "--slots") { slot_count = static_cast<std::size_t>(std::stoull(argv[idx+1])); idx += 2; }
    if (idx <= argc-2 && std::string(argv[idx]) == "--json")  { json_path_override = argv[idx+1]; idx += 2; }
    if (idx <= argc-2 && std::string(argv[idx]) == "--cap")   { expand_cap = static_cast<std::size_t>(std::stoull(argv[idx+1])); idx += 2; }
    if (idx <= argc-1) w_state = std::stod(argv[idx++]);
    if (idx <= argc-1) w_total = std::stod(argv[idx++]);
    if (idx <= argc-1) op_penalty = std::stod(argv[idx++]);

    auto scorer = css::template_search::make_score_state_total_ops(w_state, w_total, op_penalty);

    // Load data and build lib
    auto faults_vec = load_faults(faults);
    auto tps_vec    = gen_tps(faults_vec);
    FaultSimulator sim;
    auto lib = make_bruce_lib(slot_count);

    // Constraints (separate for greedy and beam)
    SequenceConstraintSet greedy_constraints_demo;
    greedy_constraints_demo.add(std::make_shared<FirstElementWriteOnlyConstraint>());
    greedy_constraints_demo.add(std::make_shared<DataReadPolarityConstraint>());
    SequenceConstraintSet beam_constraints_demo;
    beam_constraints_demo.add(std::make_shared<FirstElementWriteOnlyConstraint>());
    beam_constraints_demo.add(std::make_shared<DataReadPolarityConstraint>());

    // Greedy best (single) using constraints (first element write no read + polarity)
    GreedyTemplateSearcher greedy_searcher(sim, lib, faults_vec, tps_vec, std::make_unique<ValueExpandingGenerator>(), scorer, &greedy_constraints_demo);
    auto t0 = std::chrono::steady_clock::now();
    CandidateResult greedy_best = greedy_searcher.run(L);
    // auto greedy_best = run_greedy_prefixes(sim, lib, faults_vec, tps_vec, L, L, scorer).back(); // take only final full-length result
    auto t1 = std::chrono::steady_clock::now();
    auto greedy_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    std::cout << "[Greedy] Elapsed: "<< greedy_ms << " ms" << std::endl;

    // Streaming beam via integrated BeamTemplateSearcher::run_stream
    auto progress = [L](std::size_t level, std::size_t candidates, std::size_t kept){
        std::cout << "[SBeam] Level "<< level << "/"<< L << ": candidates="<< candidates << ", kept="<< kept << std::endl;
    };
    BeamTemplateSearcher stream_searcher(
        sim, lib, faults_vec, tps_vec, BW,
        std::make_unique<ValueExpandingGenerator>(),
        scorer,
        &beam_constraints_demo,
        progress
    );
    std::cout << "[SBeam] Start: L="<< L << ", beam_width="<< BW << ", lib="<< lib.size() << ", cap="<< (expand_cap==std::numeric_limits<std::size_t>::max()?std::string("unlimited"):std::to_string(expand_cap)) << std::endl;
    auto t2 = std::chrono::steady_clock::now();
    auto beam_list = stream_searcher.run_stream(L, expand_cap);
    auto t3 = std::chrono::steady_clock::now();
    auto beam_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t3 - t2).count();
    std::cout << "[SBeam] Elapsed: "<< beam_ms << " ms" << std::endl;
    if(!beam_list.empty()){
        double best_cov = 0.0, best_state = 0.0; std::size_t best_ops=0;
        for(const auto& cr : beam_list){
            best_cov = std::max(best_cov, cr.sim_result.total_coverage);
            best_state = std::max(best_state, cr.sim_result.state_coverage);
            std::size_t ops=0; for(const auto& e: cr.march_test.elements) ops+=e.ops.size();
            best_ops = std::max(best_ops, ops);
        }
        std::cout << "[SBeam] Best total_coverage=" << best_cov << ", best state_coverage=" << best_state << ", max ops=" << best_ops << std::endl;
    }
    std::cout << "[Greedy] total_coverage=" << greedy_best.sim_result.total_coverage << ", state_coverage=" << greedy_best.sim_result.state_coverage << std::endl;

    auto combined = unify_results(greedy_best, beam_list);
    TemplateSearchReport{}.gen_html(combined, out, w_state, w_total, op_penalty, slot_count, greedy_ms, beam_ms);
    vector<CandidateResult> greedy_vec; greedy_vec.push_back(greedy_best);
    write_all_results_json(greedy_vec, beam_list, json_path_override.empty()?out:json_path_override);
    return 0;
}
#endif
