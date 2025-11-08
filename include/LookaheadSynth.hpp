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
#include <optional>

#include "MarchSynth.hpp"  // Reuse: GenOp, SynthConfig, RawMarchEditor, SimulatorAdaptor, ElementPolicy

using std::vector;

namespace lookahead {
// Helper: get the value (0/1) of the last op if it's Read/Write; otherwise nullopt
static inline std::optional<Val> last_op_value_rw(const MarchTest& mt) {
    if (mt.elements.empty()) return std::nullopt;
    const auto& e = mt.elements.back();
    if (e.ops.empty()) return std::nullopt;
    const Op& last = e.ops.back();
    if (last.kind == OpKind::Read || last.kind == OpKind::Write) {
        if (last.value == Val::Zero || last.value == Val::One) return last.value;
    }
    return std::nullopt;
}

// Rule: If previous op's value is 0, current cannot be R1; if previous is 1, current cannot be R0.
static inline bool violates_prev_value_rule(const MarchTest& mt, GenOp g) {
    auto pv = last_op_value_rw(mt);
    if (!pv.has_value()) return false; // no restriction when no prior RW value
    switch (g) {
        case GenOp::R1: return pv.value() == Val::Zero;
        case GenOp::R0: return pv.value() == Val::One;
        default: return false;
    }
}


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
    double firstScore{0.0};                ///< OpScorer score of the first step (last appended op).
    bool valid{false};                     ///< Whether any path was found.
};

/**
 * @brief Synthesis step log: record the applied op token and its first-step score.
 */
struct StepLog {
    int step_index{0};
    std::string op_token;   ///< e.g., "W0", "R1", or "C(1)(0)(1)"
    double first_score{0.0};
    double total_coverage_after{0.0};
    struct Cand { std::string op; double score; };
    std::vector<Cand> candidates; ///< immediate candidates for this step, sorted by score desc
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
        : cfg_(cfg), simulator_(faults, tps), policy_(cfg), k_(k>0? k:1) {
        // Prepare OpScorer with TP grouping
        op_scorer_.set_group_index(tps);
        // Map CLI weights (alpha/beta/gamma/lambda) to OpScorer weights
        ScoreWeights w;
        w.alpha_S     = static_cast<double>(cfg_.alpha_state);
        w.beta_D      = static_cast<double>(cfg_.beta_sens);
        w.gamma_MPart = static_cast<double>(cfg_.gamma_detect);
        // Note: rename mu->lambda: use cfg.lambda_mask for full masking penalty
        w.lambda_MAll = static_cast<double>(cfg_.lambda_mask);
        op_scorer_.set_weights(w);
    }

    /**
     * @brief Run synthesizer until reaching target coverage or cfg_.max_ops.
     * @param init_mt Initial MarchTest (will be cloned/extended).
     * @param target_cov Target total coverage in [0,1].
     */
    MarchTest run(const MarchTest& init_mt, double target_cov = 1.0) {
        step_logs_.clear();
        MarchTest cur = ensure_has_element(init_mt);
        SimulationResult curSim = simulator_.run(cur);
        AddrOrder curOrder = cur.elements.back().order;
        int forbidden_next_index = -1; // rule: if previous step's chosen op had zero score, forbid same index in this step

    for (int step=0; step<cfg_.max_ops; ++step) {
            if (curSim.total_coverage >= target_cov) break;

            // 1) immediate candidates and scores for policy/close decision
            const auto& cand = all_candidates();
            double max_eligible_score = -std::numeric_limits<double>::infinity();
            bool any_eligible = false; // 可被插入（first score >= 0）的候選是否存在
            // Collect immediate candidate scores for logging
            std::vector<StepLog::Cand> step_cands; step_cands.reserve(cand.size());
            for (size_t idx=0; idx<cand.size(); ++idx) {
                if ((int)idx == forbidden_next_index) continue; // apply zero-gain-forbid rule
                if (violates_prev_value_rule(cur, cand[idx])) continue; // apply RW-value constraint
                MarchTest mt1 = append_op(cur, curOrder, cand[idx]);
                SimulationResult r1 = simulator_.run(mt1);
                double s1 = last_op_score(r1);
                if (s1 >= 0.0) {
                    any_eligible = true;
                    max_eligible_score = std::max(max_eligible_score, s1);
                }
                step_cands.push_back(StepLog::Cand{ genop_to_token(cand[idx]), s1 });
            }
            // sort candidates by score desc
            std::sort(step_cands.begin(), step_cands.end(), [](const StepLog::Cand& a, const StepLog::Cand& b){ return a.score > b.score; });
            // 2) Early close policy
            //    - Always close if the current element grows too long (policy limit).
            //    - Close if no candidates were considered (shouldn't happen in practice).
            //    - New rule: only non-negative first-score candidates are eligible for insertion.
            //      For k==1, if no eligible or max_eligible_score <= 0, close; for k>=2, allow search if there exists eligible.
            const bool close_due_to_no_eligible = !any_eligible;
            if (cur.elements.back().ops.size() > 3 || close_due_to_no_eligible || (max_eligible_score <= 0.0 && k_ <= 1)) {
                // New element's order: flip only if the last two elements share the same order.
                AddrOrder newOrder;
                if (close_due_to_no_eligible) {
                    // 為了跳脫無候選的局面，強制翻轉一次位址順序
                    newOrder = flip_order(cur.elements.back().order);
                } else {
                    if (cur.elements.size() >= 2 &&
                        cur.elements[cur.elements.size()-1].order == cur.elements[cur.elements.size()-2].order) {
                        newOrder = flip_order(cur.elements.back().order);
                    } else {
                        newOrder = cur.elements.back().order;
                    }
                }
                MarchElement e; e.order = newOrder;
                cur.elements.push_back(e);

                curOrder = cur.elements.back().order;
                curSim = simulator_.run(cur);
                continue;
            }

            // 3) depth-limited search to choose the best first step
            Eval best = search_best(cur, curSim, curOrder, k_, forbidden_next_index);
            if (!best.valid) break; // should not happen; fallback guard

            // apply chosen first op
            cur = append_op(cur, curOrder, best.firstOp);
            curSim = best.afterFirst;

            // log this applied step
            StepLog log;
            log.step_index = step;
            log.op_token = genop_to_token(best.firstOp);
            log.first_score = best.firstScore;
            log.total_coverage_after = curSim.total_coverage;
            log.candidates = std::move(step_cands);
            step_logs_.push_back(log);

            // update forbidden index for next outer step (based on OpScorer score)
            {
                int chosen_idx = index_of(best.firstOp);
                double g1 = best.firstScore;
                const double eps = 1e-12;
                forbidden_next_index = (g1 <= eps ? chosen_idx : -1);
            }
        }
        return cur;
    }

    const vector<StepLog>& get_step_logs() const { return step_logs_; }

private:
    SynthConfig cfg_;
    SimulatorAdaptor simulator_;
    OpScorer op_scorer_;
    ElementPolicy policy_;
    int k_;
    vector<StepLog> step_logs_;

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

    static std::string genop_to_token(GenOp g) {
        switch(g){
            case GenOp::W0: return "W0"; case GenOp::W1: return "W1";
            case GenOp::R0: return "R0"; case GenOp::R1: return "R1";
            case GenOp::C_0_0_0: return "C(0)(0)(0)"; case GenOp::C_0_0_1: return "C(0)(0)(1)";
            case GenOp::C_0_1_0: return "C(0)(1)(0)"; case GenOp::C_0_1_1: return "C(0)(1)(1)";
            case GenOp::C_1_0_0: return "C(1)(0)(0)"; case GenOp::C_1_0_1: return "C(1)(0)(1)";
            case GenOp::C_1_1_0: return "C(1)(1)(0)"; case GenOp::C_1_1_1: return "C(1)(1)(1)";
        }
        return "?";
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
            if ((int)idx == forbidden_index) continue; // enforce zero-gain-forbid at this depth
            if (violates_prev_value_rule(cur, cand[idx])) continue; // enforce RW-value constraint
            auto g = cand[idx];
            // step 1: apply op
            MarchTest mt1 = append_op(cur, ord, g);
            SimulationResult r1 = simulator_.run(mt1);
            // Score only the newly appended op (back of outcomes)
            double g1 = last_op_score(r1);
            // New rule: score < 0 的候選不允許插入，直接略過
            if (g1 < 0.0) continue;

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
                bestEval.firstScore = g1;
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

    // Helper: compute the OpScorer score for the last appended op (back of cover_lists)
    double last_op_score(const SimulationResult& sim) {
        const auto outcomes = op_scorer_.score_ops(sim.cover_lists);
        if (outcomes.empty()) return 0.0;
        return outcomes.back().total_score;
    }
};

} // namespace lookahead
