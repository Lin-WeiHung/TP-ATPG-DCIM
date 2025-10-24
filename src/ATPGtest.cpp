#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <limits>
#include <cctype>

#include "FpParserAndTpGen.hpp"
#include "FaultSimulator.hpp"
#include "MarchSynth.hpp"

using std::cout; using std::cerr; using std::endl; using std::string; using std::vector;

// Lightweight helpers
static string to_label(GenOp g){
    switch(g){
        case GenOp::W0: return "W0"; case GenOp::W1: return "W1"; case GenOp::R0: return "R0"; case GenOp::R1: return "R1";
        case GenOp::C_0_0_0: return "C(0)(0)(0)"; case GenOp::C_0_0_1: return "C(0)(0)(1)"; case GenOp::C_0_1_0: return "C(0)(1)(0)"; case GenOp::C_0_1_1: return "C(0)(1)(1)";
        case GenOp::C_1_0_0: return "C(1)(0)(0)"; case GenOp::C_1_0_1: return "C(1)(0)(1)"; case GenOp::C_1_1_0: return "C(1)(1)(0)"; case GenOp::C_1_1_1: return "C(1)(1)(1)";
    }
    return "?";
}

static const vector<GenOp>& all_gen_ops(){
    static const vector<GenOp> v{
        GenOp::W0, GenOp::W1, GenOp::R0, GenOp::R1,
        GenOp::C_0_0_0, GenOp::C_0_0_1, GenOp::C_0_1_0, GenOp::C_0_1_1,
        GenOp::C_1_0_0, GenOp::C_1_0_1, GenOp::C_1_1_0, GenOp::C_1_1_1
    };
    return v;
}

struct PreviewNode {
    // tree
    int id{-1};
    GenOp op; // op chosen at this node
    double gain{0.0};
    Delta delta; // per-stage delta
    double cov_state{0.0}, cov_sens{0.0}, cov_det{0.0};
    vector<int> path_ids; // sequence of ids from root -> this
    vector<GenOp> path_ops; // sequence of ops from root -> this
    vector<PreviewNode> children;
};

struct ManualSession {
    // data
    MarchTest cur_mt; // real
    AddrOrder cur_order{AddrOrder::Any};
    SimulationResult cur_sim; // real sim
    const vector<Fault>& faults;
    const vector<TestPrimitive>& tps;
    SimulatorAdaptor sim;
    SynthConfig cfg{};
    DiffScorer scorer;

    ManualSession(const vector<Fault>& faults_, const vector<TestPrimitive>& tps_)
        : faults(faults_), tps(tps_), sim(faults_, tps_), scorer(cfg) {
        cur_mt.name = "ManualSynth";
        // ensure 1 element
        if (cur_mt.elements.empty()) { MarchElement e; e.order = AddrOrder::Any; cur_mt.elements.push_back(e); }
        cur_sim = sim.run(cur_mt);
        cur_order = cur_mt.elements.back().order;
    }
};

static MarchTest append_op_to(const MarchTest& base, AddrOrder ord, GenOp gop){
    RawMarchEditor ed(base);
    ed.set_current_order(ord);
    ed.append_op_to_current_element(gop);
    return ed.get_cur_mt();
}

static void print_summary(const SimulationResult& res, size_t ops_in_table){
    auto pct = [](double x){ return int(x*10000+0.5)/100.0; };
    cout << "\nCurrent Coverage: state="<<pct(res.state_coverage)<<"% sens="<<pct(res.sens_coverage)
         <<"% detect="<<pct(res.detect_coverage)<<"% (total="<<pct(res.total_coverage)<<"%)\n";
    cout << "Ops in table: "<<ops_in_table<<"\n";
}

static void print_delta(const Delta& d){
    auto pct = [](double x){ return int(x*10000+0.5)/100.0; };
    cout << "  Δstate="<<pct(d.dState)<<"% Δsens="<<pct(d.dSens)<<"% Δdet="<<pct(d.dDetect)<<"%";
}

struct BuildParams { int depth=4; int max_branch=12; };

static void build_tree_rec(ManualSession& sess, const MarchTest& mt_base, const SimulationResult& sim_base,
                           AddrOrder ord, int depth, int max_branch, int& next_id,
                           vector<GenOp> path_ops, vector<int> path_ids, PreviewNode& out)
{
    if (depth <= 0) return;
    struct Cand { GenOp g; double gain; Delta d; SimulationResult after; };
    vector<Cand> cands;
    cands.reserve(all_gen_ops().size());
    for (auto gop : all_gen_ops()){
        MarchTest mt_after = append_op_to(mt_base, ord, gop);
    SimulationResult sim_after = sess.sim.run(mt_after);
        Delta d = sess.scorer.compute(sim_base, sim_after);
        double gain = sess.scorer.gain(d);
        cands.push_back({gop, gain, d, sim_after});
    }
    std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b){ return a.gain > b.gain; });
    if ((int)cands.size() > max_branch) cands.resize(max_branch);

    for (const auto& c : cands){
        PreviewNode child;
        child.id = next_id++;
        child.op = c.g;
        child.gain = c.gain;
        child.delta = c.d;
        child.cov_state = c.after.state_coverage;
        child.cov_sens  = c.after.sens_coverage;
        child.cov_det   = c.after.detect_coverage;
        child.path_ops = path_ops; child.path_ops.push_back(c.g);
        child.path_ids = path_ids; child.path_ids.push_back(child.id);
        out.children.push_back(child);
    }
    // recurse on newly added children
    for (auto& ch : out.children){
        MarchTest mt2 = append_op_to(mt_base, ord, ch.op);
        // simulate again to get base for next layer (avoid recomputing deltas against old base)
        SimulationResult sim2 = sess.sim.run(mt2);
        build_tree_rec(sess, mt2, sim2, ord, depth-1, max_branch, next_id, ch.path_ops, ch.path_ids, ch);
    }
}

static PreviewNode build_tree(ManualSession& sess, int depth, int max_branch){
    PreviewNode root; root.id = 0; int next_id = 1; // root is virtual
    build_tree_rec(sess, sess.cur_mt, sess.cur_sim, sess.cur_order, depth, max_branch, next_id, {}, {}, root);
    return root;
}

static void flat_print_and_collect(const PreviewNode& root, vector<const PreviewNode*>& id_index, int max_print = 200){
    // preorder print with indentation
    id_index.clear();
    std::function<void(const PreviewNode&,int)> dfs = [&](const PreviewNode& n, int depth){
        for (const auto& ch : n.children){
            if ((int)id_index.size() >= max_print) return;
            id_index.push_back(&ch);
            auto pct = [](double x){ return int(x*10000+0.5)/100.0; };
            cout << "["<<ch.id<<"] " << std::string(depth*2,' ') << to_label(ch.op)
                 << " | gain="<<ch.gain;
            cout << " | after: s="<<pct(ch.cov_state)<<"% z="<<pct(ch.cov_sens)<<"% d="<<pct(ch.cov_det)<<"%";
            cout << " |"; print_delta(ch.delta); cout << "\n";
            dfs(ch, depth+1);
        }
    };
    dfs(root, 0);
    if ((int)id_index.size() >= max_print){ cout << "... (truncated)\n"; }
}

static bool apply_choice(ManualSession& sess, const PreviewNode& chosen){
    // Insert only the last op of path (node's op)
    MarchTest new_mt = append_op_to(sess.cur_mt, sess.cur_order, chosen.op);
    SimulationResult new_sim = sess.sim.run(new_mt);
    Delta d = sess.scorer.compute(sess.cur_sim, new_sim);
    cout << "\nApply op: "<< to_label(chosen.op) << "\n";
    print_delta(d); cout << "\n";
    sess.cur_mt = new_mt;
    sess.cur_sim = new_sim;
    return true;
}

static void repl(ManualSession& sess){
    BuildParams params; // default: depth=4, max_branch=5
    while(true){
        cout << "\n==== Manual Insert Mode ====" << "\n";
        print_summary(sess.cur_sim, sess.cur_sim.op_table.size());
        cout << "Current element order: ";
        if (sess.cur_order==AddrOrder::Up) cout << "Up"; else if (sess.cur_order==AddrOrder::Down) cout << "Down"; else cout << "Any"; cout<<"\n";
        cout << "Commands: [p]review  [d+] depth++  [d-] depth--  [b+] branch++  [b-] branch--  [c]lose element  [q]uit\n";
        cout << "Enter: command or node id to apply that op > ";

        string cmd; if(!std::getline(std::cin, cmd)) break; if(cmd.empty()) continue;
        if (cmd=="q" || cmd=="Q") break;
        if (cmd=="d+" || cmd=="D+") { params.depth = std::min(8, params.depth+1); cout << "depth="<<params.depth<<"\n"; continue; }
        if (cmd=="d-" || cmd=="D-") { params.depth = std::max(1, params.depth-1); cout << "depth="<<params.depth<<"\n"; continue; }
        if (cmd=="b+" || cmd=="B+") { params.max_branch = std::min(12, params.max_branch+1); cout << "branch="<<params.max_branch<<"\n"; continue; }
        if (cmd=="b-" || cmd=="B-") { params.max_branch = std::max(1, params.max_branch-1); cout << "branch="<<params.max_branch<<"\n"; continue; }
        if (cmd=="c" || cmd=="C") {
            // close current element and flip order
            MarchElement e; e.order = (sess.cur_order==AddrOrder::Up ? AddrOrder::Down : AddrOrder::Up);
            sess.cur_mt.elements.push_back(e);
            sess.cur_order = e.order; // new current element
            sess.cur_sim = sess.sim.run(sess.cur_mt);
            cout << "Closed element. New order=" << (sess.cur_order==AddrOrder::Up?"Up":"Down") << "\n";
            continue;
        }

        // preview by default or explicit 'p'
        if (cmd=="p" || cmd=="P" || std::all_of(cmd.begin(), cmd.end(), [](char ch){return std::isdigit((unsigned char)ch);} )){
            auto root = build_tree(sess, params.depth, params.max_branch);
            vector<const PreviewNode*> index; flat_print_and_collect(root, index);
            if (cmd=="p" || cmd=="P") continue;
            // treat cmd as id
            int id = std::stoi(cmd);
            const PreviewNode* chosen = nullptr;
            // search by id across flat index
            std::function<const PreviewNode*(const PreviewNode&)> findById = [&](const PreviewNode& n)->const PreviewNode*{
                for (const auto& ch : n.children){
                    if (ch.id == id) return &ch;
                    if (const PreviewNode* r = findById(ch)) return r;
                }
                return nullptr;
            };
            chosen = findById(root);
            if (!chosen){ cout << "No node id="<<id<<"\n"; continue; }
            apply_choice(sess, *chosen);
            continue;
        }

        cout << "Unknown command.\n";
    }
}

static vector<Fault> load_faults(const string& path){ FaultsJsonParser p; FaultNormalizer n; auto raws=p.parse_file(path); vector<Fault> out; out.reserve(raws.size()); for(const auto& rf: raws) out.push_back(n.normalize(rf)); return out; }
static vector<TestPrimitive> gen_tps(const vector<Fault>& faults){ TPGenerator g; vector<TestPrimitive> tps; for(const auto& f: faults){ auto v=g.generate(f); tps.insert(tps.end(), v.begin(), v.end()); } return tps; }

int main(int argc, char** argv){
    try{
        string fault_path = (argc>=2? argv[1] : string("input/S_C_faults.json"));
        auto faults = load_faults(fault_path);
        auto tps = gen_tps(faults);
        cout << "Loaded faults: "<<faults.size()<<", TPs: "<<tps.size()<<"\n";
        ManualSession sess(faults, tps);
        repl(sess);
    } catch(const std::exception& e){
        cerr << "Error: "<< e.what() << "\n"; return 1;
    }
    return 0;
}
