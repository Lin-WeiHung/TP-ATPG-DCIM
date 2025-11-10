// TemplateSearchers.cpp
// Run Greedy and Beam template searches and export the top results to HTML.
// Single-responsibility helpers are provided for clarity and reuse.

#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iomanip>
#include <memory> // v2: for std::make_shared constraints

#include "FpParserAndTpGen.hpp"
#include "FaultSimulator.hpp"
#include "TemplateSearchers.hpp"

using std::string; using std::vector; using std::ofstream; using std::ostringstream; using std::size_t;
using css::template_search::TemplateLibrary; using css::template_search::TemplateEnumerator; using css::template_search::TemplateOpKind;
using css::template_search::GreedyTemplateSearcher; using css::template_search::BeamTemplateSearcher; using css::template_search::CandidateResult;
using css::template_search::ValueExpandingGenerator;
using css::template_search::SequenceConstraintSet; // v2: sequence constraints container
using css::template_search::FirstElementWriteOnlyConstraint; // v2: first element must be W-only
using css::template_search::DataReadPolarityConstraint; // v2: read polarity constraint tied to written data

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
static TemplateLibrary make_bruce_lib(){
    TemplateLibrary lib = TemplateLibrary().make_bruce();
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

// Escape minimal HTML
static string esc(const string& s){
    string out; out.reserve(s.size());
    for(char c: s){
        switch(c){
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default: out += c; break;
        }
    }
    return out;
}

// Render a MarchTest to small HTML table
static string render_march_test_html(const MarchTest& mt){
    ostringstream ss;
    ss << "<table class=mt>\n";
    for(size_t i=0;i<mt.elements.size();++i){
        const auto& e = mt.elements[i];
        ss << "  <tr><td class=idx>"<<i<<"</td><td class=ord>"<< (e.order==AddrOrder::Up?"Up":(e.order==AddrOrder::Down?"Down":"Any")) <<"</td><td class=ops>";
        for(size_t j=0;j<e.ops.size();++j){ if(j) ss<<" "; ss << esc(op_to_str(e.ops[j])); }
        ss << "</td></tr>\n";
    }
    ss << "</table>\n";
    return ss.str();
}

// Convert CandidateResult to an HTML card
static string render_candidate_card(const CandidateResult& cr){
    ostringstream ss;
    ss << "<div class=card>\n";
    ss << "  <div class=header>score="<< std::fixed << std::setprecision(4) << cr.score << "</div>\n";
    ss << "  <div class=seq>seq: ";
    for(size_t i=0;i<cr.sequence.size();++i){ if(i) ss<<", "; ss<<cr.sequence[i]; }
    ss << "</div>\n";
    ss << render_march_test_html(cr.march_test);
    ss << "</div>\n";
    return ss.str();
}

// Write full HTML report
static void write_html_report(const vector<CandidateResult>& greedy_list,
                              const vector<CandidateResult>& beam_list,
                              const string& out_path){
    ofstream ofs(out_path);
    ofs << "<!doctype html><html><head><meta charset=utf-8>";
    ofs << "<title>Template Search Report</title>";
    ofs << "<style>body{font-family:sans-serif;margin:20px} .grid{display:grid;grid-template-columns:1fr 1fr;gap:24px;} .card{border:1px solid #ccc;border-radius:8px;padding:12px;background:#fafafa} .header{font-weight:700;margin-bottom:6px} .seq{color:#555;margin-bottom:8px} table.mt{border-collapse:collapse;width:100%} table.mt td{border-bottom:1px solid #eee;padding:4px 6px} td.idx{width:40px;color:#666} td.ord{width:60px;color:#333;font-weight:600} td.ops{font-family:monospace} h2{margin-top:0} .col h2{margin-bottom:8px}</style>";
    ofs << "</head><body>";
    ofs << "<h1>Template Search Report</h1>";
    ofs << "<div class=grid>";

    ofs << "<div class=col><h2>Greedy (prefixes top 10)</h2>";
    for(const auto& cr: greedy_list) ofs << render_candidate_card(cr);
    ofs << "</div>";

    ofs << "<div class=col><h2>Beam (final top 10)</h2>";
    for(const auto& cr: beam_list) ofs << render_candidate_card(cr);
    ofs << "</div>";

    ofs << "</div></body></html>";
}

// Run greedy and return up to top_k prefix results (one per level)
static vector<CandidateResult> run_greedy_prefixes(FaultSimulator& sim,
                                                   const TemplateLibrary& lib,
                                                   const vector<Fault>& faults,
                                                   const vector<TestPrimitive>& tps,
                                                   size_t L,
                                                   size_t top_k){
    vector<CandidateResult> out;

    css::template_search::ValueExpandingGenerator defGen; // v2: value-expanding generator (enumerates R/W/C permutations)

    // v2: install sequence constraints (first element W-only + data read polarity)
    SequenceConstraintSet seq_constraints; // v2
    seq_constraints.add(std::make_shared<FirstElementWriteOnlyConstraint>()); // v2
    seq_constraints.add(std::make_shared<DataReadPolarityConstraint>()); // v2

    css::template_search::PrefixState prefix_state; // v2: track data state & length for constraints

    MarchTest prefix_mt; prefix_mt.name = "greedy_prefix";
    vector<TemplateLibrary::TemplateId> chosen_ids; chosen_ids.reserve(L);

    for(size_t pos=0; pos<L && out.size()<top_k; ++pos){
        double best_score = -1e100; TemplateLibrary::TemplateId best_tid=0; MarchElement best_elem; SimulationResult best_sim;
        for(size_t tid=0; tid<lib.size(); ++tid){
            auto elems = defGen.generate(lib, tid);
            for(auto &elem_variant : elems){
                // v2: apply sequence constraints before simulate
                if(!seq_constraints.allow(prefix_state, elem_variant, pos)) continue; // v2

                MarchTest trial = prefix_mt; if(!elem_variant.ops.empty()) trial.elements.push_back(elem_variant);
                SimulationResult simres = sim.simulate(trial, faults, tps);
                double score = simres.total_coverage;
                if(score > best_score){ best_score = score; best_tid = tid; best_elem = elem_variant; best_sim = std::move(simres); }
            }
        }
        // commit best for this position
        prefix_mt.elements.push_back(best_elem);
        chosen_ids.push_back(best_tid);

        // v2: update prefix_state through constraints set
        seq_constraints.update(prefix_state, best_elem, pos); // v2

        CandidateResult cr; cr.sequence = chosen_ids; cr.march_test = prefix_mt; cr.sim_result = best_sim; cr.score = best_sim.total_coverage; // v2: score currently coverage only
        out.push_back(std::move(cr));
    }
    return out;
}

// Run beam and return final top_k results
static vector<CandidateResult> run_beam_topk(FaultSimulator& sim,
                                             const TemplateLibrary& lib,
                                             const vector<Fault>& faults,
                                             const vector<TestPrimitive>& tps,
                                             size_t L,
                                             size_t beam_width,
                                             size_t top_k){
    // v2: install same sequence constraints for beam search
    auto gen = std::make_unique<ValueExpandingGenerator>(); // v2
    SequenceConstraintSet seq_constraints; // v2
    seq_constraints.add(std::make_shared<FirstElementWriteOnlyConstraint>()); // v2
    seq_constraints.add(std::make_shared<DataReadPolarityConstraint>()); // v2
    BeamTemplateSearcher searcher(sim, lib, faults, tps, beam_width, std::move(gen), css::template_search::default_score_func, &seq_constraints); // v2: pass constraints
    return searcher.run(L, top_k);
}

// Public entry: run full pipeline and emit HTML
int run_template_search_html(const string& faults_json_path,
                             size_t skeleton_length,
                             size_t beam_width,
                             const string& output_html_path){
    auto faults = load_faults(faults_json_path);
    auto tps    = gen_tps(faults);

    FaultSimulator sim;
    auto lib = make_bruce_lib();

    const size_t TOPK = 10;
    auto greedy_list = run_greedy_prefixes(sim, lib, faults, tps, skeleton_length, TOPK); // v2: greedy uses sequence constraints
    auto beam_list   = run_beam_topk(sim, lib, faults, tps, skeleton_length, beam_width, TOPK); // v2: beam uses sequence constraints

    write_html_report(greedy_list, beam_list, output_html_path);
    return 0;
}

// Optional standalone main (guarded) to quickly run from CLI without touching project main
#ifdef TEMPLATE_SEARCHERS_STANDALONE
int main(){
    const string faults = "input/S_C_faults.json";
    const size_t L = 6; // small for demo
    const size_t BW = 8;
    const string out = "output/TemplateSearchReport.html";
    return run_template_search_html(faults, L, BW, out);
}
#endif
