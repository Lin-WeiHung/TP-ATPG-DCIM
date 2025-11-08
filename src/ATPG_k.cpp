//  g++ -std=c++17 -g -O2 -Wall -Wextra -Iinclude src/ATPG_k.cpp -o atpg_kdemo && ./atpg_kdemo input/S_C_faults.json --k=2 --target=1 --alpha=2.5 --beta=5 --gamma=2 --lambda=3 --max_ops=60 --html=output/March_Sim_Report_atpg.html
#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstdlib>

#include "../include/FpParserAndTpGen.hpp"
#include "../include/FaultSimulator.hpp"
#include "../include/MarchSynth.hpp"
#include "../include/LookaheadSynth.hpp"
#include "../include/SynthConfigCLI.hpp"

using std::cout; using std::cerr; using std::endl; using std::string; using std::vector;

static vector<Fault> load_faults(const string& path){ FaultsJsonParser p; FaultNormalizer n; auto raws = p.parse_file(path); vector<Fault> out; for(const auto& rf: raws) out.push_back(n.normalize(rf)); return out; }
static vector<TestPrimitive> gen_tps(const vector<Fault>& faults){ TPGenerator g; vector<TestPrimitive> tps; for(const auto& f: faults){ auto v=g.generate(f); tps.insert(tps.end(), v.begin(), v.end()); } return tps; }

static void print_mt(const MarchTest& mt){
    cout << "MarchTest '"<< mt.name <<"' ("<< mt.elements.size() <<" elements)\n";
    for (size_t i=0;i<mt.elements.size();++i){
        const auto& e = mt.elements[i];
        cout << "  E"<< i <<" order="<< (e.order==AddrOrder::Up?"Up": e.order==AddrOrder::Down?"Down":"Any") <<": ";
        for (const auto& op: e.ops){
            switch(op.kind){
                case OpKind::Write: cout << (op.value==Val::Zero?"W0":"W1") <<" "; break;
                case OpKind::Read:  cout << (op.value==Val::Zero?"R0":"R1") <<" "; break;
                case OpKind::ComputeAnd:
                    cout << "C(" << (op.C_T==Val::One?1:0)
                         << ")(" << (op.C_M==Val::One?1:0)
                         << ")(" << (op.C_B==Val::One?1:0)
                         << ") ";
                    break;
            }
        }
        cout << "\n";
    }
}

// ---- Helpers for HTML report wiring via MarchSimHtml ----
static std::string op_to_token(const Op& op){
    if (op.kind==OpKind::Write) return std::string("W") + (op.value==Val::One?"1":"0");
    if (op.kind==OpKind::Read)  return std::string("R") + (op.value==Val::One?"1":"0");
    // ComputeAnd
    auto b=[](Val v){ return v==Val::One? '1':'0'; };
    std::ostringstream os; os << "C("<< b(op.C_T) << ")(" << b(op.C_M) << ")(" << b(op.C_B) << ")";
    return os.str();
}

static char order_to_letter(AddrOrder o){
    switch(o){ case AddrOrder::Up: return 'A'; case AddrOrder::Down: return 'D'; case AddrOrder::Any: default: return 'B'; }
}

static std::string to_pattern_string(const MarchTest& mt){
    std::ostringstream ps;
    for (size_t i=0;i<mt.elements.size();++i){
        const auto& e = mt.elements[i];
        ps << order_to_letter(e.order) << "(";
        for (size_t j=0;j<e.ops.size(); ++j){ if(j) ps<<","; ps << op_to_token(e.ops[j]); }
        ps << ")"; if (i+1<mt.elements.size()) ps<<";";
    }
    return ps.str();
}

static std::string find_flag_value(int argc, char** argv, const std::string& name){
    for (int i=2;i<argc;++i){
        std::string tok = argv[i];
        if (tok.rfind("--"+name+"=",0)==0){ return tok.substr(name.size()+3); }
        if (tok=="--"+name && i+1<argc && std::string(argv[i+1]).rfind("--",0)!=0){ return argv[i+1]; }
    }
    return std::string();
}

// Optional: Lookahead step logs embedding into MarchTest JSON under key "LookaheadLogs"
struct StepLogDTO { struct Cand{ std::string op; double score; }; int step; std::string op; double score; double cov_after; std::vector<Cand> candidates; };

static bool write_single_mt_json(const MarchTest& mt, const std::string& path,
                                 const std::vector<StepLogDTO>& logs = {},
                                 const ScoreWeights* w = nullptr){
    try{
        std::filesystem::path p(path);
        if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path());
        std::ofstream ofs(path);
        if (!ofs) return false;
        ofs << "[\n  {\n    \"March_test\": \""<< mt.name <<"\",\n    \"Pattern\": \""<< to_pattern_string(mt) <<"\"";
        if (w){
            ofs << ",\n    \"OpScoreWeights\": {\n"
                << "      \"alpha_S\": "<< std::setprecision(6) << std::fixed << w->alpha_S << ",\n"
                << "      \"beta_D\": " << std::setprecision(6) << std::fixed << w->beta_D << ",\n"
                << "      \"gamma_MPart\": "<< std::setprecision(6) << std::fixed << w->gamma_MPart << ",\n"
                << "      \"lambda_MAll\": "<< std::setprecision(6) << std::fixed << w->lambda_MAll << "\n"
                << "    }";
        }
        if (!logs.empty()){
            ofs << ",\n    \"LookaheadLogs\": [\n";
            for (size_t i=0;i<logs.size();++i){
                const auto& L = logs[i];
                ofs << "      { \"step\": "<< L.step
                    <<", \"op\": \""<< L.op <<"\", \"score\": "<< std::setprecision(6) << std::fixed << L.score
                    <<", \"cov_after\": "<< std::setprecision(6) << std::fixed << L.cov_after;
                if (!L.candidates.empty()){
                    ofs << ", \"candidates\": [";
                    for (size_t j=0;j<L.candidates.size();++j){
                        const auto& C = L.candidates[j];
                        ofs << "{\"op\":\""<< C.op <<"\",\"score\":"<< std::setprecision(6) << std::fixed << C.score <<"}";
                        if (j+1<L.candidates.size()) ofs << ",";
                    }
                    ofs << "]";
                }
                ofs << " }";
                if (i+1<logs.size()) ofs << ",";
                ofs << "\n";
            }
            ofs << "    ]\n  }\n]\n";
        } else {
            ofs << "\n  }\n]\n";
        }
        ofs.close();
        return true;
    } catch(...){ return false; }
}

static bool ensure_marchsimhtml_built(std::string& exeOut){
    // prefer root-level executable name
    exeOut = "./atpg_march_html";
    if (std::filesystem::exists(exeOut)) return true;
    // try to compile from source
    std::string cmd = "g++ -std=c++17 -O2 -Wall -Wextra -Iinclude src/MarchSimHtml.cpp -o " + exeOut;
    int code = std::system(cmd.c_str());
    return (code==0) && std::filesystem::exists(exeOut);
}

int main(int argc, char** argv){
    try{
        if (argc < 2){ cerr << "Usage: "<<(argc?argv[0]:"atpg_kdemo")<<" <faults.json> [k=2] [target=1.0]\n"; return 2; }
        string faults_json = argv[1];
        int k = (argc>=3 && std::string(argv[2]).rfind("--",0)!=0 ? std::max(1, std::atoi(argv[2])) : 2);
        double target = 1.0;
        if (argc>=4 && std::string(argv[3]).rfind("--",0)!=0) {
            target = std::max(0.0, std::min(1.0, std::atof(argv[3])));
        }

        auto faults = load_faults(faults_json);
        auto tps = gen_tps(faults);

        SynthConfig cfg; // defaults; can be overridden by flags
        // Parse optional flags anywhere after faults.json (positional k/target may be overridden by flags)
        synthcli::parse_cli_flags(argc, argv, 2, cfg, k, target);
        lookahead::KLookaheadSynthDriver driver(cfg, faults, tps, k);

        MarchTest mt; mt.name = "LookaheadSynth"; // empty => will get one Any element
        mt = driver.run(mt, target);

        // extract lookahead step logs
        std::vector<StepLogDTO> logs;
        for (const auto& lg : driver.get_step_logs()){
            StepLogDTO dto; dto.step = lg.step_index; dto.op = lg.op_token; dto.score = lg.first_score; dto.cov_after = lg.total_coverage_after;
            for (const auto& c : lg.candidates){ dto.candidates.push_back({c.op, c.score}); }
            logs.push_back(std::move(dto));
        }
        // build weights used in lookahead (must match LookaheadSynth mapping)
        ScoreWeights usedW; usedW.alpha_S = cfg.alpha_state; usedW.beta_D = cfg.beta_sens; usedW.gamma_MPart = cfg.gamma_detect; usedW.lambda_MAll = cfg.lambda_mask;

        // simulate final
        SimulatorAdaptor sim(faults, tps);
        auto res = sim.run(mt);

        cout.setf(std::ios::fixed);
        cout << std::setprecision(2);
        cout << "Faults="<< faults.size() <<" TPs="<< tps.size() <<"\n";
        cout << "Coverage: state="<< res.state_coverage*100 <<"% sens="<< res.sens_coverage*100 <<"% detect="<< res.detect_coverage*100 <<"% total="<< res.total_coverage*100 <<"%\n";
        print_mt(mt);
        // If user provides --html=<path>, generate HTML report via MarchSimHtml
        if (auto htmlOut = find_flag_value(argc, argv, "html"); !htmlOut.empty()){
            // 1) dump single MarchTest into a temp json
            std::string mtJson = "output/Lookahead_Final.json";
            if (!write_single_mt_json(mt, mtJson, logs, &usedW)){
                cerr << "[warn] fail to write temp MarchTest json: "<< mtJson <<"\n";
            } else {
                // 2) ensure MarchSimHtml exists (compile if needed)
                std::string exe;
                if (!ensure_marchsimhtml_built(exe)){
                    cerr << "[warn] fail to build MarchSimHtml executable, skip html export\n";
                } else {
                    // 3) run it: exe <faults.json> <MarchTest.json> <output.html>
                    std::filesystem::path outp(htmlOut);
                    if (outp.has_parent_path()) std::filesystem::create_directories(outp.parent_path());
                    std::ostringstream cmd;
                    cmd << exe << " " << faults_json << " " << mtJson << " " << htmlOut;
                    int rc = std::system(cmd.str().c_str());
                    if (rc==0) cout << "HTML report written to: "<< htmlOut <<"\n";
                    else cerr << "[warn] MarchSimHtml returned code "<< rc <<", please check.\n";
                }
            }
        }
        return 0;
    } catch(const std::exception& e){
        cerr << "atpg_kdemo error: "<< e.what() <<"\n"; return 1;
    }
}
