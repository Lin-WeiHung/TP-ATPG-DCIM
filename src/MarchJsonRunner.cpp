// Minimal runner: parse MarchTest.json, simulate each pattern, and render HTML
#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <algorithm>
#include <nlohmann/json.hpp>

#include "TemplateSearchReport.hpp"
#include "TemplateSearchers.hpp"
#include "FpParserAndTpGen.hpp"
#include "FaultSimulator.hpp"

using css::template_search::CandidateResult;

// Trim helpers
static inline void ltrim(std::string& s){ s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch){ return !std::isspace(ch); })); }
static inline void rtrim(std::string& s){ s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch){ return !std::isspace(ch); }).base(), s.end()); }
static inline void trim(std::string& s){ ltrim(s); rtrim(s); }

// Parse a token like R0/W1 or C(0)(1)(0)
static bool parse_op(const std::string& tok, Op& out){
    if(tok.size()>=2 && (tok[0]=='R' || tok[0]=='W') && (tok[1]=='0' || tok[1]=='1')){
        out.kind = (tok[0]=='R')? OpKind::Read : OpKind::Write;
        out.value = (tok[1]=='1')? Val::One : Val::Zero;
        return true;
    }
    if(tok.size()>=10 && tok[0]=='C'){
        // Expect C(x)(y)(z) where x,y,z in {0,1}
        int vals[3] = {0,0,0}; int vi=0; for(size_t i=1;i<tok.size() && vi<3; ++i){ if(tok[i]=='('){ if(i+1<tok.size() && (tok[i+1]=='0' || tok[i+1]=='1')){ vals[vi++] = (tok[i+1]=='1'); } } }
        if(vi==3){ out.kind = OpKind::ComputeAnd; out.C_T = vals[0]?Val::One:Val::Zero; out.C_M = vals[1]?Val::One:Val::Zero; out.C_B = vals[2]?Val::One:Val::Zero; return true; }
    }
    return false;
}

// Parse a single element like: a(R0, W1, C(1)(0)(1))
static bool parse_element(const std::string& s, MarchElement& elem){
    if(s.size()<3) return false;
    char a = s[0]; if(!(a=='a'||a=='b'||a=='d')) return false;
    elem.order = (a=='a')? AddrOrder::Up : ((a=='d')? AddrOrder::Down : AddrOrder::Any);
    auto l = s.find('('); auto r = s.rfind(')'); if(l==std::string::npos || r==std::string::npos || r<=l) return false;
    std::string inside = s.substr(l+1, r-l-1);
    // split by comma at top-level (no nested paren issues except C has separate parens but commas are top-level)
    std::vector<std::string> toks; std::string cur; int depth=0; for(char ch: inside){
        if(ch=='(') depth++; if(ch==')') depth--; if(ch==',' && depth==0){ trim(cur); if(!cur.empty()) toks.push_back(cur); cur.clear(); }
        else cur.push_back(ch);
    }
    trim(cur); if(!cur.empty()) toks.push_back(cur);
    elem.ops.clear(); elem.ops.reserve(toks.size());
    for(auto& t: toks){ trim(t); if(t.empty()) continue; Op op{}; if(!parse_op(t, op)) return false; elem.ops.push_back(op); }
    return true;
}

// Parse a pattern string into MarchTest
static bool parse_pattern_to_mt(const std::string& name, const std::string& pattern, MarchTest& mt){
    mt = MarchTest{}; mt.name = name; mt.elements.clear();
    // split by ';'
    std::string buf; for(char ch: pattern){ if(ch==';'){ std::string seg=buf; trim(seg); if(!seg.empty()){ MarchElement e; if(!parse_element(seg, e)) return false; mt.elements.push_back(std::move(e)); } buf.clear(); } else buf.push_back(ch); }
    std::string seg=buf; trim(seg); if(!seg.empty()){ MarchElement e; if(!parse_element(seg, e)) return false; mt.elements.push_back(std::move(e)); }
    return true;
}

static std::vector<Fault> load_faults(const std::string& path){
    FaultsJsonParser p; FaultNormalizer n; auto raws=p.parse_file(path); std::vector<Fault> out; out.reserve(raws.size()); for(const auto& rf: raws) out.push_back(n.normalize(rf)); return out;
}
static std::vector<TestPrimitive> gen_tps(const std::vector<Fault>& faults){
    TPGenerator g; std::vector<TestPrimitive> tps; tps.reserve(faults.size()*2); for(const auto& f: faults){ auto v=g.generate(f); tps.insert(tps.end(), v.begin(), v.end()); } return tps;
}

int main(int argc, char** argv){
    std::string json_path = (argc > 1) ? argv[1] : std::string("input/MarchTest.json");
    std::string faults_path = (argc > 2) ? argv[2] : std::string("input/S_C_faults.json");
    std::string out_path  = (argc > 3) ? argv[3] : std::string("output/March_Sim_Report_from_json.html");

    // Read JSON array of { March_test, Pattern }
    std::ifstream ifs(json_path);
    if(!ifs.is_open()){ std::cerr << "[Runner] Failed to open JSON: "<< json_path << std::endl; return 2; }
    nlohmann::json arr; try{ ifs >> arr; } catch(const std::exception& e){ std::cerr << "[Runner] JSON parse error: "<< e.what() << std::endl; return 2; }
    if(!arr.is_array()){ std::cerr << "[Runner] JSON is not an array: "<< json_path << std::endl; return 2; }

    // Build faults and tps
    auto faults = load_faults(faults_path);
    auto tps    = gen_tps(faults);
    FaultSimulator sim;

    std::vector<CandidateResult> results; results.reserve(arr.size());
    for(const auto& obj : arr){
        if(!obj.is_object()) continue;
        std::string name = obj.value("March_test", std::string("(unnamed)"));
        std::string pattern = obj.value("Pattern", std::string());
        MarchTest mt; if(!parse_pattern_to_mt(name, pattern, mt)){
            std::cerr << "[Runner] Skip invalid pattern for: "<< name << std::endl; continue;
        }
        auto simres = sim.simulate(mt, faults, tps);
        CandidateResult cr; cr.march_test = std::move(mt); cr.sim_result = simres; cr.score = 0.0; // score not used in JSON mode
        results.push_back(std::move(cr));
    }
    std::sort(results.begin(), results.end(), [](const CandidateResult& a, const CandidateResult& b){ return a.sim_result.total_coverage > b.sim_result.total_coverage; });

    TemplateSearchReport rpt;
    std::string src_name = std::filesystem::path(json_path).filename().string();
    bool ok = rpt.gen_html_from_march_json(src_name, results, out_path);
    if(!ok){ std::cerr << "[Runner] Failed to write HTML: "<< out_path << std::endl; return 3; }
    std::cout << "[Runner] HTML written: " << out_path << std::endl;
    return 0;
}
