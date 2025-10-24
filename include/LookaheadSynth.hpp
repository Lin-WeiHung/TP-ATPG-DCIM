#pragma once

/**
 * @file LookaheadSynth.hpp
 * @brief k-step lookahead March Test synthesizer (non-intrusive extension).
 *
 * Design goals:
 * - Do NOT break existing flows or types. Reuse MarchSynth.hpp components.
 * - SRP/OOP: clear responsibilities and Doxygen-friendly comments.
 * - Minimal glue: header-only driver, no changes to existing headers.
 * - Layering: reuse data model (MarchTest, Op, etc.), simulator adaptor, scorer.
 */

#include <vector>
#include <limits>
#include <algorithm>
#include <tuple>
#include <cmath>

#include "MarchSynth.hpp"  // Reuse: GenOp, SynthConfig, RawMarchEditor, SimulatorAdaptor, DiffScorer, ElementPolicy

using std::vector;

namespace lookahead {

/**
 * @brief Helper: list all candidate GenOps considered by the search.
 */
static inline const vector<GenOp>& all_candidates() {
    static const vector<GenOp> v{
        GenOp::W0, GenOp::W1, GenOp::R0, GenOp::R1,
        GenOp::C_0_0_0, GenOp::C_0_0_1, GenOp::C_0_1_0, GenOp::C_0_1_1,
        GenOp::C_1_0_0, GenOp::C_1_0_1, GenOp::C_1_1_0, GenOp::C_1_1_1
    };
    return v;
}

/**
 * @brief Immutable evaluation outcome of a depth-limited search from a state.
 */
struct Eval {
    double totalGain{ -std::numeric_limits<double>::infinity() };
    GenOp firstOp{ GenOp::W0 };            ///< First action to take from the root.
    SimulationResult afterFirst{};         ///< State after applying firstOp.
    Delta firstDelta{};                    ///< Delta of the first step.
    bool valid{false};                     ///< Whether any path was found.
};

/**
 * @brief k-step lookahead synthesizer selecting the first op that maximizes cumulative gain.
 *
 * Contract:
 * - Input: current MarchTest, its SimulationResult, current AddrOrder, integer depth k.
 * - Output: best first operation (GenOp) and its evaluation (cumulative gain over k steps).
 * - Gain model: same as DiffScorer (weights in SynthConfig, includes per-step mu_cost).
 * - Policy: element close decision still happens at top level (outside the search).
 */
class KLookaheadSynthDriver {
public:
    /**
     * @brief Construct a driver.
     * @param cfg Synthesizer config (weights, costs, limits).
     * @param faults Fault list for SimulatorAdaptor.
     * @param tps Test primitives for SimulatorAdaptor.
     * @param k Lookahead depth (>=1 recommended; 1 == greedy).
     */
    KLookaheadSynthDriver(const SynthConfig& cfg,
                          const vector<Fault>& faults,
                          const vector<TestPrimitive>& tps,
                          int k)
        : cfg_(cfg), simulator_(faults, tps), scorer_(cfg), policy_(cfg), k_(k>0? k:1) {}

    /**
     * @brief Run synthesizer until reaching target coverage or cfg_.max_ops.
     * @param init_mt Initial MarchTest (will be cloned/extended).
     * @param target_cov Target total coverage in [0,1].
     */
    MarchTest run(const MarchTest& init_mt, double target_cov = 1.0) {
        MarchTest cur = ensure_has_element(init_mt);
        SimulationResult curSim = simulator_.run(cur);
        AddrOrder curOrder = cur.elements.back().order;
        int forbidden_next_index = -1; // rule: if previous step's chosen op had zero gain, forbid same index in this step

        for (int step=0; step<cfg_.max_ops; ++step) {
            if (curSim.total_coverage >= target_cov) break;

            // 1) immediate candidates and deltas for policy decision
            const auto& cand = all_candidates();
            vector<Delta> deltas; deltas.reserve(cand.size());
            for (size_t idx=0; idx<cand.size(); ++idx) {
                if ((int)idx == forbidden_next_index) continue; // apply rule at this step
                auto g = cand[idx];
                MarchTest mt1 = append_op(cur, curOrder, g);
                SimulationResult r1 = simulator_.run(mt1);
                deltas.push_back(scorer_.compute(curSim, r1));
            }
            // 2) element close policy (same semantics as greedy)
            if (cur.elements.back().ops.size() > 3 || policy_.should_close(deltas)) {
                cur = close_element(cur, curOrder);
                curOrder = flip_order(curOrder);
                curSim = simulator_.run(cur);
                continue;
            }

            // 3) depth-limited search to choose the best first step
            Eval best = search_best(cur, curSim, curOrder, k_, forbidden_next_index);
            if (!best.valid) break; // should not happen; fallback guard

            // apply chosen first op
            cur = append_op(cur, curOrder, best.firstOp);
            curSim = best.afterFirst;

            // update forbidden index for next outer step
            {
                int chosen_idx = index_of(best.firstOp);
                double g1 = scorer_.gain(best.firstDelta);
                const double eps = 1e-12;
                forbidden_next_index = (g1 <= eps ? chosen_idx : -1);
            }
        }
        return cur;
    }

private:
    SynthConfig cfg_;
    SimulatorAdaptor simulator_;
    DiffScorer scorer_;
    ElementPolicy policy_;
    int k_;

    static MarchTest ensure_has_element(MarchTest mt) {
        if (mt.elements.empty()) {
            MarchElement e; 
            e.order = AddrOrder::Any;
            e.ops.push_back({OpKind::Write, Val::Zero}); // 預設放一個 W0
            mt.elements.push_back(e);
            e.ops.clear();
            e.order = AddrOrder::Any;
            e.ops.push_back({OpKind::ComputeAnd, Val::X, Val::Zero, Val::One, Val::Zero}); // 預設放一個 C(0)(1)(0)
            mt.elements.push_back(e);
            e.ops.clear();
            e.order = AddrOrder::Up;
            mt.elements.push_back(e);
        }
        return mt;
    }

    static MarchTest append_op(const MarchTest& base, AddrOrder ord, GenOp gop) {
        RawMarchEditor ed(base);
        ed.set_current_order(ord);
        ed.append_op_to_current_element(gop);
        return ed.get_cur_mt();
    }

    static AddrOrder flip_order(AddrOrder ord) {
        if (ord == AddrOrder::Up) return AddrOrder::Down;
        if (ord == AddrOrder::Down) return AddrOrder::Up;
        return AddrOrder::Up;
    }

    static MarchTest close_element(MarchTest mt, AddrOrder cur_order) {
        MarchElement e; e.order = flip_order(cur_order); mt.elements.push_back(e); return mt;
    }

    /**
     * @brief Depth-limited best-first evaluation from a given state.
     * @param cur Current MarchTest.
     * @param curSim Current simulation result.
     * @param ord Current address order.
     * @param depth Remaining lookahead depth.
     * @param forbidden_index If >=0, skip candidate with this index at this depth (rule: previous step's op had zero gain).
     */
    Eval search_best(const MarchTest& cur, const SimulationResult& curSim, AddrOrder ord, int depth, int forbidden_index = -1) {
        Eval out{};
        // base case: no further steps => zero additional gain, invalid to choose an op
        if (depth <= 0) { out.totalGain = 0.0; out.valid = false; return out; }

        double bestGain = -std::numeric_limits<double>::infinity();
        Eval bestEval; bestEval.valid = false;

        const auto& cand = all_candidates();
        for (size_t idx=0; idx<cand.size(); ++idx) {
            if ((int)idx == forbidden_index) continue; // enforce rule at this depth
            auto g = cand[idx];
            // step 1: apply op
            MarchTest mt1 = append_op(cur, ord, g);
            SimulationResult r1 = simulator_.run(mt1);
            Delta d1 = scorer_.compute(curSim, r1);
            double g1 = scorer_.gain(d1);

            double future = 0.0;
            if (depth > 1) {
                // recursively evaluate next steps (without early close here; top-level handles close)
                int child_forbid = (g1 <= 1e-12 ? (int)idx : -1);
                Eval child = search_best(mt1, r1, ord, depth-1, child_forbid);
                if (child.valid) future = child.totalGain; // only add if deeper path exists
            }
            double total = g1 + future;
            if (total > bestGain) {
                bestGain = total;
                bestEval.totalGain = total;
                bestEval.firstOp = g;
                bestEval.afterFirst = r1;
                bestEval.firstDelta = d1;
                bestEval.valid = true;
            }
        }
        return bestEval;
    }

    static int index_of(GenOp g){
        const auto& cand = all_candidates();
        for (size_t i=0;i<cand.size();++i) if (cand[i]==g) return (int)i;
        return -1;
    }
};

} // namespace lookahead
