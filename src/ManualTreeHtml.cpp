#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <stdexcept>

#include "../include/FpParserAndTpGen.hpp"
#include "../include/FaultSimulator.hpp"
#include "../include/MarchSynth.hpp"

using std::string; using std::vector; using std::cout; using std::endl;

static const vector<GenOp>& all_gen_ops(){
    static const vector<GenOp> v{
        GenOp::W0, GenOp::W1, GenOp::R0, GenOp::R1,
        GenOp::C_0_0_0, GenOp::C_0_0_1, GenOp::C_0_1_0, GenOp::C_0_1_1,
        GenOp::C_1_0_0, GenOp::C_1_0_1, GenOp::C_1_1_0, GenOp::C_1_1_1
    };
    return v;
}

static string to_label(GenOp g){
    switch(g){
        case GenOp::W0: return "W0"; case GenOp::W1: return "W1"; case GenOp::R0: return "R0"; case GenOp::R1: return "R1";
        case GenOp::C_0_0_0: return "C(0)(0)(0)"; case GenOp::C_0_0_1: return "C(0)(0)(1)"; case GenOp::C_0_1_0: return "C(0)(1)(0)"; case GenOp::C_0_1_1: return "C(0)(1)(1)";
        case GenOp::C_1_0_0: return "C(1)(0)(0)"; case GenOp::C_1_0_1: return "C(1)(0)(1)"; case GenOp::C_1_1_0: return "C(1)(1)(0)"; case GenOp::C_1_1_1: return "C(1)(1)(1)";
    }
    return "?";
}

static MarchTest append_op_to(const MarchTest& base, AddrOrder ord, GenOp gop){
    RawMarchEditor ed(base);
    ed.set_current_order(ord);
    ed.append_op_to_current_element(gop);
    return ed.get_cur_mt();
}

struct Session {
    vector<Fault> faults;
    vector<TestPrimitive> tps;
    SimulatorAdaptor sim;
    SynthConfig cfg{}; // default weights
    DiffScorer scorer;
    Session(const vector<Fault>& f, const vector<TestPrimitive>& t)
        : faults(f), tps(t), sim(f,t), scorer(cfg) {}
};

struct NodeInfo { string idPath; GenOp op; double gain; Delta d; double s,z,dct; };

static void emit_html_head(std::ostream& os, int depth){
    os << "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><title>Manual Synth Tree</title>";
    os << "<style>body{font-family:sans-serif}details{margin:6px 0}summary{cursor:pointer} .meta{color:#666} .op{display:inline-block;min-width:90px} .pct{min-width:140px;display:inline-block} .gain{min-width:120px;display:inline-block} .delta{min-width:180px;display:inline-block}</style>";
    os << "</head><body>";
    os << "<h2>Manual Synth Tree (depth="<< depth << ")</h2>";
}

static inline string pct(double v){ std::ostringstream os; os.setf(std::ios::fixed); os<< std::setprecision(2) << (v*100.0) << "%"; return os.str(); }

static void emit_node(std::ostream& os, const NodeInfo& n){
    os << "<summary>";
    os << "<span class=\"op\">["<< n.idPath << "] "<< to_label(n.op) << "</span>";
    os << "<span class=\"gain\">gain="<< n.gain << "</span>";
    os << "<span class=\"pct\"> after: s="<< pct(n.s) << " z="<< pct(n.z) << " d="<< pct(n.dct) << "</span>";
    os << "<span class=\"delta\"> Δs="<< pct(n.d.dState) << " Δz="<< pct(n.d.dSens) << " Δd="<< pct(n.d.dDetect) << "</span>";
    os << "</summary>";
}

static void build_tree(Session& sess, std::ostream& os, const MarchTest& base, const SimulationResult& baseRes,
                       AddrOrder ord, int depth, vector<int>& idxStack, size_t& nodeCount, size_t maxNodes){
    if (depth == 0) return;
    for (size_t i=0;i<all_gen_ops().size();++i){
        GenOp g = all_gen_ops()[i];
        MarchTest mt2 = append_op_to(base, ord, g);
        SimulationResult r2 = sess.sim.run(mt2);
        Delta d = sess.scorer.compute(baseRes, r2);
        double gain = sess.scorer.gain(d);
        idxStack.push_back(int(i+1));
        // id path
        std::ostringstream path; for (size_t k=0;k<idxStack.size();++k){ if(k) path<<"."; path<< idxStack[k]; }
        NodeInfo n{path.str(), g, gain, d, r2.state_coverage, r2.sens_coverage, r2.detect_coverage};
        os << "<details>"; emit_node(os, n);
        ++nodeCount; if (nodeCount >= maxNodes){ os << "<div class=\"meta\">(truncated)</div></details>"; idxStack.pop_back(); return; }
        build_tree(sess, os, mt2, r2, ord, depth-1, idxStack, nodeCount, maxNodes);
        os << "</details>";
        idxStack.pop_back();
    }
}

static vector<Fault> load_faults(const string& path){ FaultsJsonParser p; FaultNormalizer n; auto raws = p.parse_file(path); vector<Fault> out; for(const auto& rf: raws) out.push_back(n.normalize(rf)); return out; }
static vector<TestPrimitive> gen_tps(const vector<Fault>& faults){ TPGenerator g; vector<TestPrimitive> tps; for(const auto& f: faults){ auto v=g.generate(f); tps.insert(tps.end(), v.begin(), v.end()); } return tps; }

int main(int argc, char** argv){
    try{
        if (argc < 2){ std::cerr << "Usage: "<<(argc?argv[0]:"ManualTreeHtml")<<" <faults.json> [depth=4] [output.html]\n"; return 2; }
        string faults_json = argv[1];
        int depth = (argc>=3 ? std::max(1, std::min(8, std::atoi(argv[2]))) : 4);
        string out_html = (argc>=4 ? argv[3] : string("output/Manual_Tree.html"));

        auto faults = load_faults(faults_json);
        auto tps = gen_tps(faults);
        Session sess(faults, tps);

        MarchTest mt; mt.name = "ManualSynthGUI"; if (mt.elements.empty()){ MarchElement e; e.order=AddrOrder::Any; mt.elements.push_back(e);} 
        AddrOrder ord = mt.elements.back().order;
        auto baseRes = sess.sim.run(mt);

        std::ofstream ofs(out_html);
        if (!ofs){ throw std::runtime_error("Cannot open output html: "+out_html); }
        emit_html_head(ofs, depth);
        ofs << "<div class=\"meta\">Faults="<<faults.size()<<" TPs="<<tps.size()<<"</div>";
        ofs << "<details open><summary>Root (current) s="<< pct(baseRes.state_coverage) <<" z="<< pct(baseRes.sens_coverage) <<" d="<< pct(baseRes.detect_coverage) <<"</summary>";
        vector<int> idx; size_t nodeCount=0; size_t maxNodes = 60000; // safety cap
        build_tree(sess, ofs, mt, baseRes, ord, depth, idx, nodeCount, maxNodes);
        ofs << "</details>";
        ofs << "</body></html>";
        ofs.close();
        cout << "HTML written: "<< out_html << " (nodes="<< nodeCount <<")\n";
    } catch(const std::exception& e){
        std::cerr << "ManualTreeHtml error: "<< e.what() <<"\n"; return 1;
    }
    return 0;
}
