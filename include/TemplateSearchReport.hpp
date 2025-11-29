// TemplateSearchReport.hpp
// Encapsulates HTML generation for template search results.
// Provides a single public method: gen_html(...)

#pragma once

#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <filesystem>
#include <nlohmann/json.hpp>

#include "TemplateSearchers.hpp" // for CandidateResult, MarchTest, etc.
#include "FaultSimulator.hpp" // for OpScorer & ScoreWeights

using css::template_search::CandidateResult; // other types (MarchTest, Op, etc.) assumed available globally from included headers

class TemplateSearchReport {
public:
    void gen_html(const std::vector<CandidateResult>& combined_list,
                  const std::string& out_path,
                  double w_state,
                  double w_total,
                  double op_penalty,
                  std::size_t slot_count,
                  long long greedy_ms,
                  long long beam_ms) const {
        std::ofstream ofs(out_path);
        if(!ofs.is_open()){
            std::cerr << "[HTML] Failed to open path: " << out_path << std::endl;
            return;
        }
        write_html_start(ofs, "Template Search Report");
        ofs << render_meta(slot_count, greedy_ms, beam_ms, w_state, w_total, op_penalty, combined_list);
        ofs << "<h2>Combined Results (Greedy + Beam)</h2><div class=cards>";
        ofs << render_candidate_cards(combined_list, w_state, w_total, op_penalty);
        ofs << "</div>";
        write_html_end(ofs);
    }

    // New entry: render results from March JSON after simulation
    // Shows same bars and march table as gen_html, with meta including source name and best result.
    bool gen_html_from_march_json(const std::string& source_name,
                                  const std::vector<CandidateResult>& results,
                                  const std::string& out_path) const {
        std::ofstream ofs(out_path);
        if(!ofs.is_open()){
            std::cerr << "[HTML] Failed to open output: " << out_path << std::endl;
            return false;
        }
        write_html_start(ofs, "March Test Report");

        ofs << render_meta_march(source_name, results);
        ofs << "<h2>March Tests</h2><div class=cards>";
        ofs << render_march_result_cards(results);
        ofs << "</div>";
        write_html_end(ofs);
        return true;
    }

    // Extended: generate HTML with per-op scores (OpScorer). Each candidate gets a dropdown listing ops.
    // If use_opscore=false, op-level scores still computed with provided weights (default or user) for inspection.
    void gen_html_with_op_scores(const std::vector<CandidateResult>& results,
                                 const std::string& out_path,
                                 const ScoreWeights& weights,
                                 double op_penalty,
                                 bool use_opscore,
                                 const std::vector<TestPrimitive>& tps) const {
        std::ofstream ofs(out_path);
        if(!ofs.is_open()){
            std::cerr << "[HTML] Failed to open output: " << out_path << std::endl;
            return;
        }
        write_html_start(ofs, "Greedy Sweep Report (Op Scores)");
        ofs << "<div class=meta>";
        ofs << "<div class=metric><div class=lbl>Items</div><div class=val>"<< results.size() <<"</div></div>";
        ofs << "<div class=metric><div class=lbl>Weights</div><div class=val>"<< "aState="<<weights.alpha_state<<" aSens="<<weights.alpha_sens<<" bD="<<weights.beta_D<<" gMP="<<weights.gamma_MPart<<" lMA="<<weights.lambda_MAll<<" pen="<<op_penalty<<"" <<"</div></div>";
        ofs << "<div class=metric><div class=lbl>Mode</div><div class=val>"<<(use_opscore?"OpScorer":"Coverage")<<"</div></div>";
        ofs << "</div>";
        ofs << "<h2>Configurations</h2><div class=cards>";
        OpScorer base_scorer; base_scorer.set_weights(weights); base_scorer.set_group_index(tps);
        for(const auto& cr : results){
            ofs << render_candidate_card_with_ops(cr, base_scorer, op_penalty, use_opscore);
        }
        ofs << "</div>";
        write_html_end(ofs);
    }

private:
    static std::string esc(const std::string& s){
        std::string out; out.reserve(s.size());
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

    // Compose common style and head
    static void write_html_start(std::ostream& os, const std::string& title){
        os << "<!doctype html><html><head><meta charset=utf-8>";
        os << "<title>" << esc(title) << "</title>";
        os << style_block();
        os << "</head><body>";
        os << "<h1>" << esc(title) << "</h1>";
    }
    static void write_html_end(std::ostream& os){
        os << "<div class=footer>Generated by Template Search Engine</div>";
        os << "</body></html>";
    }
    static std::string style_block(){
        return R"(<style>
    :root { --bg:#111; --panel:#1e1e1e; --border:#333; --accent:#4ea3ff; --accent2:#ff9f43; --font:#eaeaea; --muted:#999; --state:#4caf50; --sens:#ff9800; --total:#2196f3; }
    body { background:var(--bg); color:var(--font); font-family:Inter,Segoe UI,system-ui,sans-serif; margin:32px; line-height:1.4; }
    h1 { font-size:28px; margin:0 0 12px; font-weight:600; }
    .meta { display:flex; flex-wrap:wrap; gap:16px; margin-bottom:20px; }
    .metric { background:var(--panel); padding:10px 14px; border:1px solid var(--border); border-radius:8px; min-width:160px; }
    .metric .lbl { font-size:12px; text-transform:uppercase; letter-spacing:.5px; color:var(--muted); }
    .metric .val { font-size:18px; font-weight:600; }
    .cards { display:grid; gap:28px; grid-template-columns:repeat(auto-fit,minmax(420px,1fr)); }
    .card { background:var(--panel); border:1px solid var(--border); border-radius:12px; padding:14px 16px 16px; position:relative; box-shadow:0 2px 4px rgba(0,0,0,.35); }
    .card-head { display:flex; justify-content:space-between; align-items:center; margin-bottom:10px; font-size:13px; }
    .card-head .score { font-weight:600; color:var(--accent); }
    .card-head .ops { color:var(--muted); }
    .cov-row { display:flex; align-items:center; gap:10px; margin:4px 0; font-size:12px; }
    .cov-row .lbl { width:70px; font-weight:500; }
    .bar-wrap { flex:1; background:#222; border:1px solid #2d2d2d; border-radius:6px; height:10px; overflow:hidden; }
    .bar { height:100%; background:linear-gradient(90deg,var(--accent),var(--accent2)); }
    .bar.state { background:var(--state); }
    .bar.sens { background:var(--sens); }
    .bar.total { background:var(--total); }
    .score-break { margin:10px 0 8px; padding:8px 10px; background:#181818; border:1px solid #262626; border-radius:8px; font-size:11px; display:grid; gap:2px; }
    .score-break .sum { margin-top:4px; font-weight:600; color:var(--accent); }
    .seq { font-size:12px; color:var(--muted); margin:8px 0 10px; }
    table.mt { width:100%; border-collapse:collapse; font-size:11px; }
    table.mt td { padding:4px 6px; border-bottom:1px solid #222; }
    td.idx { width:38px; color:var(--muted); }
    td.ord { width:56px; font-weight:600; }
    td.ops { font-family:monospace; font-size:11px; }
    .footer { margin-top:40px; font-size:11px; color:var(--muted); text-align:center; }
    pre.pattern { background:#0f0f0f; border:1px solid #262626; border-radius:8px; padding:10px; font-size:11px; overflow:auto; }
    @media (max-width:700px){ .cards { grid-template-columns:1fr; } }
    </style>)";
    }
    static std::string render_meta_march(const std::string& source_name,
                                         const std::vector<CandidateResult>& results){
        std::ostringstream ofs;
        double best_total = 0.0;
        for(const auto& c: results){
            best_total = std::max(best_total, c.sim_result.total_coverage*100.0);
        }
        ofs << "<div class=meta>";
        ofs << "<div class=metric><div class=lbl>Source</div><div class=val>"<< esc(source_name) <<"</div></div>";
        ofs << "<div class=metric><div class=lbl>Items</div><div class=val>"<< results.size() <<"</div></div>";
        ofs << "<div class=metric><div class=lbl>Best Total%</div><div class=val>"<< fmt_percent_value(best_total) <<"%</div></div>";
        ofs << "</div>";
        return ofs.str();
    }

    static std::string render_march_result_cards(const std::vector<CandidateResult>& results){
        std::ostringstream os;
        for(const auto& cr: results){ os << render_march_result_card(cr); }
        return os.str();
    }

    // Meta section for candidate results
    static std::string render_meta(std::size_t slot_count,
                                   long long greedy_ms,
                                   long long beam_ms,
                                   double w_state,
                                   double w_total,
                                   double op_penalty,
                                   const std::vector<CandidateResult>& combined_list){
        std::ostringstream ofs;
        ofs << "<div class=meta>";
        ofs << "<div class=metric><div class=lbl>Slots</div><div class=val>"<< slot_count <<"</div></div>";
        ofs << "<div class=metric><div class=lbl>Greedy Time</div><div class=val>"<< greedy_ms <<" ms</div></div>";
        ofs << "<div class=metric><div class=lbl>Beam Time</div><div class=val>"<< beam_ms <<" ms</div></div>";
        ofs << "<div class=metric><div class=lbl>Scoring Formula</div><div class=val>w_state="<< w_state <<" / w_total="<< w_total <<" / pen="<< op_penalty <<"</div></div>";
        double best_total = 0.0; for(const auto& c: combined_list) best_total = std::max(best_total, c.sim_result.total_coverage*100.0);
        ofs << "<div class=metric><div class=lbl>Best Total%</div><div class=val>"<< fmt_percent_value(best_total) <<"%</div></div>";
        ofs << "<div class=metric><div class=lbl>Combined Count</div><div class=val>"<< combined_list.size() <<"</div></div>";
        ofs << "</div>";
        return ofs.str();
    }

    static std::string render_candidate_cards(const std::vector<CandidateResult>& combined_list,
                                              double w_state,
                                              double w_total,
                                              double op_penalty){
        std::ostringstream os;
        for(const auto& cr: combined_list){ os << render_candidate_card(cr, w_state, w_total, op_penalty); }
        return os.str();
    }

    static std::string op_to_str(const Op& op){
        switch(op.kind){
            case OpKind::Read:  return std::string("R") + (op.value==Val::One?"1":"0");
            case OpKind::Write: return std::string("W") + (op.value==Val::One?"1":"0");
            case OpKind::ComputeAnd: {
                auto b = [](Val v){ return v==Val::One?"1":"0"; };
                std::ostringstream ss; ss << "C("<<b(op.C_T)<<","<<b(op.C_M)<<","<<b(op.C_B)<<")"; return ss.str();
            }
        }
        return "?";
    }

    static std::string render_march_test_html(const MarchTest& mt){
        std::ostringstream ss;
        ss << "<table class=mt>\n";
        for(std::size_t i=0;i<mt.elements.size();++i){
            const auto& e = mt.elements[i];
            ss << "  <tr><td class=idx>"<<i<<"</td><td class=ord>"<< (e.order==AddrOrder::Up?"Up":(e.order==AddrOrder::Down?"Down":"Any")) <<"</td><td class=ops>";
            for(std::size_t j=0;j<e.ops.size();++j){ if(j) ss<<" "; ss << esc(op_to_str(e.ops[j])); }
            ss << "</td></tr>\n";
        }
        ss << "</table>\n";
        return ss.str();
    }

    static std::string render_candidate_card(const CandidateResult& cr,
                                             double w_state,
                                             double w_total,
                                             double op_penalty){
        std::ostringstream ss;
        std::size_t ops_count = 0; for(const auto& e: cr.march_test.elements) ops_count += e.ops.size();
        double state_cov = cr.sim_result.state_coverage;
        double sens_cov  = cr.sim_result.sens_coverage;
        double total_cov = cr.sim_result.total_coverage;
        auto pct = [](double v){ return (v*100.0); };
        double s_state = w_state * state_cov;
        double s_total = w_total * total_cov;
        double s_pen   = op_penalty * static_cast<double>(ops_count);
        double s_sum   = s_state + s_total - s_pen;

        ss << "<div class=card>\n";
        ss << "  <div class=\"card-head\">";
        ss << "<span class=score>Score: "<< std::fixed << std::setprecision(3) << cr.score << "</span>";
        ss << "<span class=ops>Total Ops: "<< ops_count << "</span>";
        ss << "</div>\n";

        auto bar = [&](const char* label, const char* cls, double value){
            ss << "  <div class=cov-row><span class=lbl>"<<label<<" "<< fmt_percent_fraction(value) <<"%</span><div class=bar-wrap><div class='bar "<<cls<<"' style='width:"<< std::min(100.0, pct(value)) <<"%'></div></div></div>\n";
        };
        bar("State", "state", state_cov);
        bar("Sens",  "sens",  sens_cov);
        bar("Total", "total", total_cov);

        ss << "  <div class=score-break>";
        ss << "<div>state component: "<< std::setprecision(3) << s_state << " (w="<< w_state <<")</div>";
        ss << "<div>total component: "<< std::setprecision(3) << s_total << " (w="<< w_total <<")</div>";
        ss << "<div>op penalty: -"<< std::setprecision(3) << s_pen << " (w="<< op_penalty <<")</div>";
        ss << "<div class=sum>final: "<< std::setprecision(3) << s_sum << "</div>";
        ss << "  </div>\n";

        ss << render_march_test_html(cr.march_test);
        ss << "</div>\n";
        return ss.str();
    }

    // Op-score enriched card (adds dropdown with per-op scores via OpScorer)
    static std::string render_candidate_card_with_ops(const CandidateResult& cr,
                                                      OpScorer& scorer,
                                                      double op_penalty,
                                                      bool use_opscore){
        std::ostringstream ss;
        std::size_t ops_count = 0; for(const auto& e: cr.march_test.elements) ops_count += e.ops.size();
        double state_cov = cr.sim_result.state_coverage;
        double sens_cov  = cr.sim_result.sens_coverage;
        double total_cov = cr.sim_result.total_coverage;
        auto pct = [](double v){ return (v*100.0); };
        ss << "<div class=card>\n";
        ss << "  <div class=\"card-head\">";
        ss << "<span class=score>Score: "<< std::fixed << std::setprecision(3) << cr.score << "</span>";
        ss << "<span class=ops>Total Ops: "<< ops_count << "</span>";
        ss << "</div>\n";
        auto bar = [&](const char* label, const char* cls, double value){
            ss << "  <div class=cov-row><span class=lbl>"<<label<<" "<< fmt_percent_fraction(value) <<"%</span><div class=bar-wrap><div class='bar "<<cls<<"' style='width:"<< std::min(100.0, pct(value)) <<"%'></div></div></div>\n";
        };
        bar("State", "state", state_cov);
        bar("Sens",  "sens",  sens_cov);
        bar("Total", "total", total_cov);
        ss << render_march_test_html(cr.march_test);
        // Per-op scoring using preconfigured OpScorer (group index + weights already set)
        auto outcomes = scorer.score_ops(cr.sim_result.cover_lists);
        ss << "<details><summary>Op Scores ("<< outcomes.size() <<")</summary>";
        ss << "<table class=mt>";
        ss << "<tr><td class=idx>#</td><td class=ord>State_cov</td><td class=ops>Sens_cov</td><td class=ops>D_cov</td><td class=ops>PartM</td><td class=ops>FullM</td><td class=ops>TotalScore</td></tr>";
        for(std::size_t i=0;i<outcomes.size();++i){
            const auto& o = outcomes[i];
            ss << "<tr><td class=idx>"<< i <<"</td><td class=ord>"<< std::setprecision(3) << o.state_cov <<"</td><td class=ops>"<< std::setprecision(3) << o.sens_cov <<"</td><td class=ops>"<< o.D_cov <<"</td><td class=ops>"<< o.part_M_num <<"</td><td class=ops>"<< o.full_M_num <<"</td><td class=ops>"<< std::setprecision(3) << o.total_score <<"</td></tr>";
        }
        double sum_score = 0.0; for(const auto& o: outcomes) sum_score += o.total_score; double penalized = sum_score - op_penalty*ops_count;
        ss << "</table><div class=score-break>";
        ss << "<div>sum(op)="<< std::setprecision(4) << sum_score <<"</div>";
        ss << "<div>op_penalty: -"<< op_penalty*ops_count <<"</div>";
        ss << "<div class=sum>final(opscore approx)="<< std::setprecision(4) << penalized <<"</div>";
        ss << "</div></details>";
        ss << "</div>\n";
        return ss.str();
    }

    // Card renderer for March JSON results: show name, hide score and template IDs.
    static std::string render_march_result_card(const CandidateResult& cr){
        std::ostringstream ss;
        std::size_t ops_count = 0; for(const auto& e: cr.march_test.elements) ops_count += e.ops.size();
        double state_cov = cr.sim_result.state_coverage;
        double sens_cov  = cr.sim_result.sens_coverage;
        double total_cov = cr.sim_result.total_coverage;
        auto pct = [](double v){ return (v*100.0); };
        ss << "<div class=card>\n";
        ss << "  <div class=\"card-head\">";
        ss << "<span class=score>" << esc(cr.march_test.name) << "</span>";
        ss << "<span class=ops>Total Ops: "<< ops_count << "</span>";
        ss << "</div>\n";
        auto bar = [&](const char* label, const char* cls, double value){
            ss << "  <div class=cov-row><span class=lbl>"<<label<<" "<< fmt_percent_fraction(value) <<"%</span><div class=bar-wrap><div class='bar "<<cls<<"' style='width:"<< std::min(100.0, pct(value)) <<"%'></div></div></div>\n";
        };
        bar("State", "state", state_cov);
        bar("Sens",  "sens",  sens_cov);
        bar("Total", "total", total_cov);
        ss << render_march_test_html(cr.march_test);
        ss << "</div>\n";
        return ss.str();
    }

    // Render a simple card from March JSON (name + raw pattern pretty print)
    static std::string render_march_json_card(const std::string& name,
                                              const std::string& pattern){
        std::ostringstream ss;
        ss << "<div class=card>\n";
        ss << "  <div class=\"card-head\">";
        ss << "<span class=score>March: "<< esc(name) << "</span>";
        ss << "</div>\n";
        ss << "  <div class=seq>Pattern</div>";
        ss << "  <pre class=pattern>" << pattern_multiline(pattern) << "</pre>\n";
        ss << "</div>\n";
        return ss.str();
    }
    static std::string pattern_multiline(const std::string& pattern){
        // Replace "; " with ";\n" and escape HTML
        std::string p = pattern; // original
        std::string out; out.reserve(p.size()+16);
        for(size_t i=0;i<p.size();++i){
            if(i+1<p.size() && p[i]==';' && p[i+1]==' '){ out += esc(std::string(1,p[i])); out += "\n"; ++i; continue; }
            out += esc(std::string(1,p[i]));
        }
        return out;
    }
    // Percentage formatting helpers
    static std::string fmt_percent_value(double v){
        double rounded = std::round(v);
        std::ostringstream oss;
        if(std::fabs(v - rounded) < 1e-12){ oss << static_cast<int>(rounded); return oss.str(); }
        oss << std::fixed << std::setprecision(2) << v; return trim_trailing_zeroes(oss.str());
    }
    static std::string fmt_percent_fraction(double fraction){ return fmt_percent_value(fraction*100.0); }
    static std::string trim_trailing_zeroes(std::string s){
        auto pos = s.find('.'); if(pos==std::string::npos) return s;
        while(!s.empty() && s.back()=='0') s.pop_back();
        if(!s.empty() && s.back()=='.') s.pop_back();
        return s;
    }
};
