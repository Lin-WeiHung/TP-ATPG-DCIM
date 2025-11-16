#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>

#include "../include/FaultSimulator.hpp"
#include "../include/FpParserAndTpGen.hpp"
#include "../include/MarchTestRefiner.hpp"
#include "../include/TemplateSearchers.hpp"

using std::cerr; using std::cout; using std::endl; using std::string; using std::vector;

static string op_to_str(const Op& op){
    switch(op.kind){
        case OpKind::Read:  return string("R") + (op.value==Val::Zero?"0":op.value==Val::One?"1":"X");
        case OpKind::Write: return string("W") + (op.value==Val::Zero?"0":op.value==Val::One?"1":"X");
        case OpKind::ComputeAnd: {
            auto v = [](Val x){ return x==Val::Zero? '0' : x==Val::One? '1' : 'X'; };
            std::ostringstream oss; oss << "C(" << v(op.C_T) << v(op.C_M) << v(op.C_B) << ")"; return oss.str();
        }
    }
    return "?";
}

static string patch_to_str(const vector<Op>& ops){
    std::ostringstream oss; bool first=true; for(const auto& o:ops){ if(!first) oss << ", "; oss << op_to_str(o); first=false; } return oss.str();
}

static char order_char(AddrOrder o){
    switch(o){
        case AddrOrder::Any: return 'b';
        case AddrOrder::Up: return 'a';
        case AddrOrder::Down: return 'd';
    }
    return 'b';
}

static string op_token(const Op& op){
    if(op.kind==OpKind::Write) return string("W") + (op.value==Val::Zero?"0":op.value==Val::One?"1":"X");
    if(op.kind==OpKind::Read)  return string("R") + (op.value==Val::Zero?"0":op.value==Val::One?"1":"X");
    if(op.kind==OpKind::ComputeAnd){
        auto v=[](Val x){return x==Val::Zero?"0":x==Val::One?"1":"X";};
        return string("C(")+v(op.C_T)+")("+v(op.C_M)+")("+v(op.C_B)+")";
    }
    return "?";
}

static string pattern_string(const MarchTest& mt){
    std::ostringstream oss;
    for(const auto& elem : mt.elements){
        oss << order_char(elem.order) << '(';
        for(size_t i=0;i<elem.ops.size();++i){
            oss << op_token(elem.ops[i]);
            if(i+1<elem.ops.size()) oss << ", ";
        }
        oss << ")";
        oss << ';';
    }
    return oss.str();
}

static void write_html_report(const string& path, const RefineLog& log, const MarchTest& final_mt){
    std::ofstream ofs(path);
    if(!ofs){ cerr << "Cannot open output HTML: " << path << endl; return; }
    ofs << "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><title>March Refine Log</title>";
    ofs << "<style>body{font-family:Arial,Helvetica,sans-serif;margin:20px;} table{border-collapse:collapse;width:100%;margin-bottom:24px;} th,td{border:1px solid #ccc;padding:6px 8px;text-align:left;} .ok{color:#060;} .bad{color:#a00;} .muted{color:#777;} .sel{background:#e7f7ff;} code{background:#f6f6f6;padding:1px 4px;border-radius:4px;}</style>";
    ofs << "</head><body>";
    ofs << "<h1>March Test Refinement Log</h1>";

    ofs << "<h2>Iterations</h2>";
    ofs << "<table><thead><tr><th>#</th><th>Site(elem,i)</th><th>Δsens</th><th>Δdetect</th><th>Op+</th><th>Last-Resort Groups</th></tr></thead><tbody>";
    for(const auto& it : log.iters){
        ofs << "<tr>";
        ofs << "<td>" << it.iter << "</td>";
        ofs << "<td>(" << it.elem_index << "," << it.after_op_index << ")</td>";
        ofs << "<td>" << it.delta_sens << "</td>";
        ofs << "<td>" << it.delta_detect << "</td>";
        ofs << "<td>" << it.op_increment << "</td>";
        ofs << "<td>" << it.last_resort_groups << "</td>";
        ofs << "</tr>";
    }
    ofs << "</tbody></table>";

    for(const auto& it : log.iters){
        ofs << "<h3>Iteration " << it.iter << " — Site (" << it.elem_index << "," << it.after_op_index << ")</h3>";
        ofs << "<table><thead><tr><th>Patch</th><th>Score</th><th>Δsens</th><th>Δdetect</th><th>StateOK</th><th>NoWorse</th><th>Status</th></tr></thead><tbody>";
        for(const auto& p : it.patches){
            ofs << "<tr" << (p.selected?" class=\"sel\"":"") << ">";
            ofs << "<td><code>" << patch_to_str(p.ops) << "</code></td>";
            ofs << "<td>" << p.score << "</td>";
            ofs << "<td>" << p.sens_gain << "</td>";
            ofs << "<td>" << p.detect_gain << "</td>";
            ofs << "<td>" << (p.state_ok?"<span class=ok>Y</span>":"<span class=bad>N</span>") << "</td>";
            ofs << "<td>" << (p.coverage_progress?"<span class=ok>Y</span>":"<span class=bad>N</span>") << "</td>";
            string status;
            if(p.selected) status = "selected"; else if(!p.reject_reason.empty()) status = p.reject_reason; else status = "rejected";
            ofs << "<td>" << status << "</td>";
            ofs << "</tr>";
        }
        ofs << "</tbody></table>";
    }

    // Append final refined March Test pattern
    ofs << "<h2>Final Refined March Test</h2>";
    ofs << "<p><code>";
    string pat = pattern_string(final_mt);
    for(char c : pat){ if(c=='<'||c=='>'||c=='&'){ ofs << '&#' << (int)c << ';'; } else ofs << c; }
    ofs << "</code></p>";
    ofs << "</body></html>";
}

int main(int argc, char** argv){
    string march_path = argc>1 ? argv[1] : "input/MarchTest.json";
    string fault_path = argc>2 ? argv[2] : "input/fault.json";
    string out_html   = argc>3 ? argv[3] : "output/RefineLog.html";
    size_t top_k      = 10; // 取前 10 進行調整

    try{
        // 1) 讀 March 測試（僅用於決定元素長度 L）
        MarchTestJsonParser mt_parser; auto raws = mt_parser.parse_file(march_path);
        if (raws.empty()) { cerr << "No March tests in: " << march_path << endl; return 2; }
        MarchTestNormalizer mt_norm; MarchTest mt_ref = mt_norm.normalize(raws.front());
        size_t L = mt_ref.elements.size(); if (L==0) L = 6; // fallback

        // 2) 讀 faults 並生成 TPs
        FaultsJsonParser fparser; auto raw_faults = fparser.parse_file(fault_path);
        FaultNormalizer fnorm; TPGenerator tpg;
        vector<Fault> faults; faults.reserve(raw_faults.size());
        for(const auto& rf : raw_faults) faults.push_back(fnorm.normalize(rf));
        vector<TestPrimitive> tps; for(const auto& f : faults){ auto v = tpg.generate(f); tps.insert(tps.end(), v.begin(), v.end()); }

        // 3) 建立 Template library + BeamTemplateSearcher 取前 10 個初始模板
        FaultSimulator sim;
        using namespace css::template_search;
        TemplateLibrary lib = TemplateLibrary::make_bruce();
        SequenceConstraintSet constraints;
        constraints.add(std::make_shared<FirstElementWriteOnlyConstraint>());
        constraints.add(std::make_shared<DataReadPolarityConstraint>());
        auto scorer = make_score_state_total_ops(1.0, 0.5, 0.01);
        BeamTemplateSearcher beam(sim, lib, faults, tps, /*beam_width*/16,
                      std::make_unique<ValueExpandingGenerator>(),
                      scorer, &constraints);
        auto beam_results = beam.run(L, top_k);
        if (beam_results.empty()) { cerr << "Beam search produced no candidates." << endl; return 3; }
        cout << "Beam produced " << beam_results.size() << " candidates (L=" << L << ")" << endl;

        // 4) 逐一 refine 前 top_k 候選
        MarchTestRefiner refiner;
        RefineConfig cfg; cfg.max_iterations = 20; cfg.max_no_progress_rounds = 3; cfg.max_patch_len = 3; cfg.enable_cross_element_site = false;
        double best_det = -1.0; size_t best_idx = 0;
        MarchRefineResult best_res;
        for (size_t i = 0; i < beam_results.size(); ++i) {
            const auto& cand = beam_results[i];
            cout << "Candidate " << i << ": pre-score=" << cand.score
                 << ", state=" << cand.sim_result.state_coverage
                 << ", detect=" << cand.sim_result.detect_coverage << endl;
            RefineLog rlog;
            auto res = refiner.refine(cand.march_test, faults, tps, sim, cfg, &rlog);
            // 寫出 HTML 與 JSON
            string ith_html = "output/RefineLog_" + std::to_string(i) + ".html";
            write_html_report(ith_html, rlog, res.refined);
            string refined_json_path = "input/RefineMT_" + std::to_string(i) + ".json";
            std::ofstream jofs(refined_json_path);
            if(jofs){
                string pat = pattern_string(res.refined);
                jofs << "[\n  {\"March_test\":\"Refined " << res.refined.name << "\",\"Pattern\":\"";
                for(char c : pat){ if(c=='\"' || c=='\\') jofs << '\\'; jofs << c; }
                jofs << "\"}\n]\n";
            }
            double det_final = res.history.empty()? cand.sim_result.detect_coverage : res.history.back().detect_coverage;
            if (det_final > best_det) { best_det = det_final; best_idx = i; best_res = res; }
        }

        // 5) 複製最佳結果至預設輸出（方便現有流程）
        write_html_report(out_html, RefineLog{}, best_res.refined); // 僅輸出最終 pattern 區塊
        cout << "Best refined index=" << best_idx << ", detect=" << best_det << " → " << out_html << endl;
        {
            string refined_json_path = "input/RefineMT.json";
            std::ofstream jofs(refined_json_path);
            if(jofs){
                string pat = pattern_string(best_res.refined);
                jofs << "[\n  {\"March_test\":\"Refined " << best_res.refined.name << "\",\"Pattern\":\"";
                for(char c : pat){ if(c=='\"' || c=='\\') jofs << '\\'; jofs << c; }
                jofs << "\"}\n]\n";
                cout << "Refined MarchTest JSON written: " << refined_json_path << endl;
            } else {
                cerr << "Cannot open for write: input/RefineMT.json" << endl;
            }
        }
    } catch(const std::exception& ex){
        cerr << "Error: " << ex.what() << endl; return 1;
    }
    return 0;
}
