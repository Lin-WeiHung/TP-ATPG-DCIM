// TemplateSearchers.hpp
// PoC: Greedy + Beam Template Searchers for March-skeleton exploration
// Depends on your existing FaultSimulator.hpp for types and simulate()
// Usage: include this header where FaultSimulator, Fault, TestPrimitive are available.
//
// Author: ChatGPT (PoC for user's project)
//
// NOTE: This file intentionally remains "single-header" for easy drop-in.
//       For production, split into .hpp/.cpp and add unit tests.

#pragma once

#include <vector>
#include <array>
#include <string>
#include <algorithm>
#include <limits>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <functional> // v2: for ScoreFunc
#include <sstream>
#include <cstddef>
#include <queue>
#include <random>    // v5: for multi-start noise

#include "FaultSimulator.hpp" // uses your existing simulator types & simulate(). :contentReference[oaicite:1]{index=1}

namespace css {
namespace template_search {

// -----------------------------
// Lightweight template layer types (PoC)
// -----------------------------
using std::vector;
using std::array;
using std::string;
using std::size_t;

// Template-level op kind (slot-level)
enum class TemplateOpKind { None, Read, Write, Compute };

// Template slot (PoC: currently only kind; can extend with parameters later)
struct TemplateSlot {
    TemplateOpKind kind{TemplateOpKind::None};
};

// ElementTemplate: a small pattern of N ops (dynamically sized) in one march element
class ElementTemplate {
public:
    ElementTemplate() = default;
    // Backward-compatible ctor for 3-slot pattern
    ElementTemplate(AddrOrder ord, TemplateOpKind a, TemplateOpKind b, TemplateOpKind c) {
        order = ord;
        slots_.reserve(3);
        slots_.push_back(TemplateSlot{a});
        slots_.push_back(TemplateSlot{b});
        slots_.push_back(TemplateSlot{c});
    }
    // New ctor: provide arbitrary slot kinds
    ElementTemplate(AddrOrder ord, const std::vector<TemplateOpKind>& kinds) {
        order = ord;
        slots_.reserve(kinds.size());
        for (auto k : kinds) slots_.push_back(TemplateSlot{k});
    }
    // Accessors for generator/consumers
    AddrOrder get_order() const { return order; }
    const std::vector<TemplateSlot>& get_slots() const { return slots_; }
    std::size_t slot_count() const { return slots_.size(); }
    // local validity checks: no "hole" (X - X) and at most one R/W
    bool is_valid() const {
        if (has_hole()) return false;
        // if (has_multiple_rwc()) return false;
        if (has_multiple_rw()) return false;
        return true;
    }
    // Helpers for sequence-level rules & heuristics
    bool has_kind(TemplateOpKind k) const {
        for (const auto& s : slots_) if (s.kind == k) return true;
        return false;
    }
    int count_non_none() const {
        int c=0; for (const auto& s: slots_) if (s.kind!=TemplateOpKind::None) ++c; return c;
    }
private:
    AddrOrder order{AddrOrder::Any};
    std::vector<TemplateSlot> slots_{};

    bool has_hole() const {
        bool seen_none=false;
        for (const auto& s : slots_) {
            if (s.kind == TemplateOpKind::None) {
                seen_none = true;
            } else {
                if (seen_none) return true; // X - NON_X hole
            }
        }
        return false;
    }
    bool has_multiple_rwc() const {
        int cntR=0,cntW=0,cntC=0;
        for (const auto& s : slots_) {
            if (s.kind == TemplateOpKind::Read) ++cntR;
            if (s.kind == TemplateOpKind::Write) ++cntW;
            if (s.kind == TemplateOpKind::Compute) ++cntC;
        }
        return (cntR>1 || cntW>1 || cntC>1);
    }
    bool has_multiple_rw() const {
        int cntR=0,cntW=0;
        for (const auto& s : slots_) {
            if (s.kind == TemplateOpKind::Read) ++cntR;
            if (s.kind == TemplateOpKind::Write) ++cntW;
        }
        return (cntR>1 || cntW>1);
    }
};

// TemplateLibrary: small hand-picked PoC library; extendable
class TemplateLibrary {
public:
    using TemplateId = size_t;

    TemplateLibrary() = default;

    // v3: static factory that enumerates all valid templates with dynamic slot count
    static TemplateLibrary make_bruce(std::size_t slot_count = 3) {
        TemplateLibrary lib;
        if (slot_count == 0) return lib;

        // Build all combinations via iterative counting over 4^slot_count
        const TemplateOpKind kinds[4] = {
            TemplateOpKind::None,
            TemplateOpKind::Read,
            TemplateOpKind::Write,
            TemplateOpKind::Compute
        };

        // iterate for Up and Down
        for (AddrOrder ord : {AddrOrder::Up, AddrOrder::Down}) {
            // total combinations = 4^slot_count (use integer math to avoid fp rounding)
            std::size_t combos = 1;
            for (std::size_t i = 0; i < slot_count; ++i) combos *= 4u;
            for (std::size_t idx = 0; idx < combos; ++idx) {
                std::vector<TemplateOpKind> seq;
                seq.resize(slot_count, TemplateOpKind::None);
                std::size_t t = idx;
                for (std::size_t p = 0; p < slot_count; ++p) {
                    std::size_t digit = t % 4;
                    t /= 4;
                    seq[p] = kinds[digit];
                }
                ElementTemplate et(ord, seq);
                if (et.is_valid()) lib.templates_.push_back(et);
            }
        }
        return lib;
    }

    const ElementTemplate& at(TemplateId id) const { return templates_.at(id); }
    TemplateId size() const { return templates_.size(); }
    void push_back(const ElementTemplate& et) { templates_.push_back(et); }

private:
    vector<ElementTemplate> templates_;
};

// -----------------------------
// Extensible CandidateGenerator
// -----------------------------
// Responsible to expand a template choice into 0..N candidate MarchElement variants.
// DefaultCandidateGenerator returns exactly one mapping (ElementTemplate::to_march_element).
// You can later implement ValueExpandingGenerator to expand R/W/C values, or WindowedGenerator.
class ICandidateGenerator {
public:
    virtual ~ICandidateGenerator() = default;
    // Given library and a template id, produce candidate MarchElements (could be multiple if expanding values)
    virtual vector<MarchElement> generate(const TemplateLibrary& lib, TemplateLibrary::TemplateId tid) const = 0; // v2: make generator const
};

class ValueExpandingGenerator : public ICandidateGenerator {
public:
    vector<MarchElement> generate(const TemplateLibrary& lib,
                                  TemplateLibrary::TemplateId tid) const override { // v2: const override
        vector<MarchElement> out;
        const auto& et = lib.at(tid);

        const auto& slots = et.get_slots();
        const auto order = et.get_order();

        // 計算所需的 bit 數（R/W 1 bit；Compute 3 bits）
        struct SlotBits { TemplateOpKind kind; std::size_t base; };
        std::vector<SlotBits> specs;
        specs.reserve(slots.size());
        std::size_t cursor = 0;
        for (std::size_t i = 0; i < slots.size(); ++i) {
            SlotBits sb{slots[i].kind, cursor};
            switch (slots[i].kind) {
                case TemplateOpKind::None:    cursor += 0; break;
                case TemplateOpKind::Read:    cursor += 1; break;
                case TemplateOpKind::Write:   cursor += 1; break;
                case TemplateOpKind::Compute: cursor += 3; break;
            }
            specs.push_back(sb);
        }

        const std::size_t total_bits = cursor;
        if (total_bits == 0) {
            // 無任何操作：仍回傳一個空元素（只有 order）
            MarchElement e; e.order = order; // ops 保持空
            out.push_back(e);
            return out;
        }

        if (total_bits >= (sizeof(std::size_t) * 8)) {
            // 避免移位溢位；在極端情況下直接返回空（或可改為截斷）
            return out;
        }

        const std::size_t total_masks = (static_cast<std::size_t>(1) << total_bits);
        out.reserve(total_masks);

        for (std::size_t mask = 0; mask < total_masks; ++mask) {
            MarchElement e; e.order = order;
            // 依 slot 順序建立 ops
            for (std::size_t i = 0; i < specs.size(); ++i) {
                const auto kind = specs[i].kind;
                const std::size_t base = specs[i].base;
                switch (kind) {
                    case TemplateOpKind::None:
                        break;
                    case TemplateOpKind::Read: {
                        Op o; o.kind = OpKind::Read;
                        const std::size_t bit = (mask >> base) & 1u;
                        o.value = bit ? Val::One : Val::Zero;
                        e.ops.push_back(o);
                        break;
                    }
                    case TemplateOpKind::Write: {
                        Op o; o.kind = OpKind::Write;
                        const std::size_t bit = (mask >> base) & 1u;
                        o.value = bit ? Val::One : Val::Zero;
                        e.ops.push_back(o);
                        break;
                    }
                    case TemplateOpKind::Compute: {
                        Op o; o.kind = OpKind::ComputeAnd;
                        const std::size_t bT = (mask >> (base+0)) & 1u;
                        const std::size_t bM = (mask >> (base+1)) & 1u;
                        const std::size_t bB = (mask >> (base+2)) & 1u;
                        o.C_T = bT ? Val::One : Val::Zero;
                        o.C_M = bM ? Val::One : Val::Zero;
                        o.C_B = bB ? Val::One : Val::Zero;
                        e.ops.push_back(o);
                        break;
                    }
                }
            }
            out.push_back(std::move(e));
        }

        return out;
    }  
};

// -----------------------------
// Search result container
// -----------------------------
struct CandidateResult {
    vector<TemplateLibrary::TemplateId> sequence; // chosen template ids (length <= L)
    MarchTest march_test; // full march test for final verification
    SimulationResult sim_result;
    double score{0.0};
};

// v2: pluggable scoring function for searchers
using ScoreFunc = std::function<double(const SimulationResult&, const MarchTest&)>;

// v4: factory 建立具權重參數的 ScoreFunc（使用 lambda capture）
// 允許呼叫端自訂 w_state, w_total, op_penalty，而不需要改動搜尋器介面或增加繁雜結構。
inline ScoreFunc make_score_state_total_ops(double w_state, double w_total, double op_penalty) {
    return [=](const SimulationResult& sim, const MarchTest& mt) -> double {
        std::size_t ops_count = 0;
        for (const auto& e : mt.elements) ops_count += e.ops.size();
        return w_state * sim.state_coverage
             + w_total * sim.total_coverage
             - op_penalty * static_cast<double>(ops_count);
    };
}

// v2: lightweight prefix state for sequence constraints
// Replace former DataState with Val (X/Zero/One) from FpParserAndTpGen.hpp
struct PrefixState {
    Val D{Val::X};
    std::size_t length{0};
};

// v2: interface for sequence-level constraints (e.g., first element W-only, D-policed reads)
class ISequenceConstraint {
public:
    virtual ~ISequenceConstraint() = default;

    virtual bool allow(const PrefixState& prefix,
                       const MarchElement& elem,
                       std::size_t pos) const = 0;

    virtual void update(PrefixState& prefix,
                        const MarchElement& elem,
                        std::size_t pos) const {
        (void)elem;
        (void)pos;
        ++prefix.length;
    }
};

// v2: container for multiple constraints
class SequenceConstraintSet {
public:
    void add(std::shared_ptr<ISequenceConstraint> c) {
        constraints_.push_back(std::move(c));
    }

    bool allow(const PrefixState& prefix,
               const MarchElement& elem,
               std::size_t pos) const {
        for (const auto& c : constraints_) {
            if (!c->allow(prefix, elem, pos)) return false;
        }
        return true;
    }

    void update(PrefixState& prefix,
                const MarchElement& elem,
                std::size_t pos) const {
        for (const auto& c : constraints_) {
            c->update(prefix, elem, pos);
        }
    }

private:
    std::vector<std::shared_ptr<ISequenceConstraint>> constraints_;
};

// v2: first element must be W-only
class FirstElementWriteOnlyConstraint : public ISequenceConstraint {
public:
    bool allow(const PrefixState& prefix,
               const MarchElement& elem,
               std::size_t pos) const override {
        // Only restrict the very first element of the sequence
        if (pos != 0 && prefix.length != 0) return true;

        bool has_write = false;
        for (const auto& op : elem.ops) {
            if (op.kind == OpKind::Write) {
                has_write = true;
            } else if (op.kind == OpKind::Read || op.kind == OpKind::ComputeAnd) {
                // Disallow any R/C in the first element
                return false;
            }
        }
        return has_write;
    }
};

// v2: D=0 forbids R1, D=1 forbids R0
class DataReadPolarityConstraint : public ISequenceConstraint {
public:
    bool allow(const PrefixState& prefix,
               const MarchElement& elem,
               std::size_t /*pos*/) const override {
        if (prefix.D == Val::X) return true;

        for (const auto& op : elem.ops) {
            if (op.kind != OpKind::Read) continue;
            if (prefix.D == Val::Zero && op.value == Val::One) {
                return false;
            }
            if (prefix.D == Val::One && op.value == Val::Zero) {
                return false;
            }
        }
        return true;
    }

    void update(PrefixState& prefix,
                const MarchElement& elem,
                std::size_t pos) const override {
        (void)pos;
        for (const auto& op : elem.ops) {
            if (op.kind == OpKind::Write) {
                if (op.value == Val::Zero) {
                    prefix.D = Val::Zero;
                } else if (op.value == Val::One) {
                    prefix.D = Val::One;
                }
            }
        }
        ++prefix.length;
    }
};

// -----------------------------
// GreedyTemplateSearcher
//  - sequentially pick next element that maximizes incremental score
//  - O(L * K * C) simulations where C is candidate variants per template
// -----------------------------
class GreedyTemplateSearcher {
public:
    GreedyTemplateSearcher(FaultSimulator& simulator,
                           const TemplateLibrary& lib,
                           const vector<Fault>& faults,
                           const vector<TestPrimitive>& tps,
                           std::unique_ptr<ICandidateGenerator> gen = std::make_unique<ValueExpandingGenerator>(),
                           ScoreFunc scorer = nullptr, // v2: pluggable scoring (default: make_score_state_total_ops(0.9, 0.5, 0.01))
                           const SequenceConstraintSet* constraints = nullptr) // v2: optional sequence constraints
        : sim_(simulator)
        , lib_(lib)
        , faults_(faults)
        , tps_(tps)
        , gen_(std::move(gen))
        , scorer_(scorer ? std::move(scorer) : make_score_state_total_ops(0.9, 0.5, 0.01)) // v2: use default if nullptr
        , constraints_(constraints)   // v2
    {}

    // Run greedy for skeleton length L. Returns chosen CandidateResult (single best path)
    CandidateResult run(size_t L) {
        return run_internal_(L, nullptr);
    }

    // v5: Multi-start greedy — run multiple times with score perturbation, return best.
    //     First run is deterministic (original greedy); subsequent runs add Gaussian noise
    //     to the scorer to explore alternative paths.
    CandidateResult run_multistart(size_t L, int num_starts = 5, double noise_scale = 0.03) {
        // First run: deterministic (identical to original run())
        CandidateResult best = run_internal_(L, nullptr);
        if (num_starts <= 1 || best.sim_result.total_coverage >= 1.0 - 1e-9) return best;

        std::mt19937 rng(std::random_device{}());
        std::normal_distribution<double> dist(0.0, noise_scale);
        std::function<double()> noise_fn = [&]() -> double { return dist(rng); };

        for (int i = 1; i < num_starts; ++i) {
            CandidateResult result = run_internal_(L, &noise_fn);
            // Re-score without noise for fair comparison
            result.score = scorer_(result.sim_result, result.march_test);
            if (result.sim_result.total_coverage > best.sim_result.total_coverage ||
                (std::abs(result.sim_result.total_coverage - best.sim_result.total_coverage) < 1e-9
                 && result.score > best.score)) {
                best = std::move(result);
            }
            if (best.sim_result.total_coverage >= 1.0 - 1e-9) break; // early exit on 100%
        }
        return best;
    }

private:
    // v5: Internal greedy with optional score noise function
    CandidateResult run_internal_(size_t L, std::function<double()>* noise_fn) {
        CandidateResult best_overall;
        best_overall.score = -std::numeric_limits<double>::infinity();

        // We will build the sequence incrementally. Keep current prefix MarchTest and its sim state.
        MarchTest prefix_mt;
        prefix_mt.name = "greedy_prefix";

        PrefixState prefix_state; // v2: track D / length for sequence constraints

        // Remaining faults are tracked implicitly by sim reports; for now we always
        // re-simulate the full prefix to get accurate remaining coverage.
        // For efficiency you may modify FaultSimulator to support incremental simulation; PoC uses full simulate each time.

        // Optimization: Pre-generate all candidate elements once to avoid redundant expansion at each position
        vector<vector<MarchElement>> all_candidates;
        all_candidates.reserve(lib_.size());
        for (size_t tid = 0; tid < lib_.size(); ++tid) {
            all_candidates.push_back(gen_->generate(lib_, tid));
        }

        vector<TemplateLibrary::TemplateId> chosen_ids;
        chosen_ids.reserve(L);

        for (size_t pos = 0; pos < L; ++pos) {
            double best_score_this_pos = -std::numeric_limits<double>::infinity();
            TemplateLibrary::TemplateId best_tid = 0;
            MarchElement best_elem;
            SimulationResult best_sim;

            // For each candidate template id, use pre-generated element variants
            for (size_t tid = 0; tid < lib_.size(); ++tid) {
                const auto& elems = all_candidates[tid];
                for (const auto &elem_variant : elems) {
                    // v2: apply sequence-level constraints before simulating
                    if (constraints_ && !constraints_->allow(prefix_state, elem_variant, pos)) {
                        continue;
                    }

                    // form a trial MT = prefix + candidate element
                    MarchTest trial_mt = prefix_mt;
                    if (!elem_variant.ops.empty()) trial_mt.elements.push_back(elem_variant);

                    // simulate trial_mt against current (static) fault list to get coverage
                    SimulationResult simres = sim_.simulate(trial_mt, faults_, tps_);
                    double score = scorer_(simres, trial_mt); // v2: use pluggable scorer
                    // v5: add noise for multi-start exploration
                    if (noise_fn) score += (*noise_fn)();

                    if (score > best_score_this_pos) {
                        best_score_this_pos = score;
                        best_tid = tid;
                        best_elem = elem_variant;
                        best_sim = std::move(simres);
                    }
                }
            }

            // commit chosen best for this position
            if (best_score_this_pos == -std::numeric_limits<double>::infinity()) {
                // no candidate (e.g., lib empty) -> break
                break;
            }
            // push to prefix
            prefix_mt.elements.push_back(best_elem);
            chosen_ids.push_back(best_tid);

            // v2: update prefix_state for constraints (D / length)
            if (constraints_) {
                constraints_->update(prefix_state, best_elem, pos);
            } else {
                ++prefix_state.length;
            }

            // update best_overall if this prefix already looks best (we keep final L-length or best prefix)
            CandidateResult cr;
            cr.sequence = chosen_ids;
            cr.march_test = prefix_mt;
            cr.sim_result = best_sim;
            cr.score = scorer_(cr.sim_result, cr.march_test); // v2: keep score consistent with scorer_
            if (cr.score > best_overall.score) best_overall = std::move(cr);
        }

        // At the end, return best_overall (could be full-length or shorter if stopping earlier)
        return best_overall;
    }

public:
    // v6: Refine (local search) — try replacing each element position with all candidates
    //     to improve coverage. Repeats until no improvement or max_passes reached.
    CandidateResult refine(const CandidateResult& initial, int max_passes = 3) {
        if (initial.march_test.elements.empty()) return initial;

        // Pre-generate all candidate elements once
        vector<vector<MarchElement>> all_candidates;
        all_candidates.reserve(lib_.size());
        for (size_t tid = 0; tid < lib_.size(); ++tid) {
            all_candidates.push_back(gen_->generate(lib_, tid));
        }

        CandidateResult current = initial;
        current.score = scorer_(current.sim_result, current.march_test);

        for (int pass = 0; pass < max_passes; ++pass) {
            bool improved = false;
            size_t L = current.march_test.elements.size();

            for (size_t pos = 0; pos < L; ++pos) {
                // Try replacing element at pos with every candidate
                for (size_t tid = 0; tid < lib_.size(); ++tid) {
                    for (const auto& elem_variant : all_candidates[tid]) {
                        // Check constraints for this position
                        if (constraints_) {
                            PrefixState ps;
                            for (size_t p = 0; p < pos; ++p) {
                                constraints_->update(ps, current.march_test.elements[p], p);
                            }
                            if (!constraints_->allow(ps, elem_variant, pos)) continue;
                            // Also check rest of sequence still valid after this change
                            PrefixState ps2 = ps;
                            constraints_->update(ps2, elem_variant, pos);
                            bool rest_ok = true;
                            for (size_t p = pos + 1; p < L; ++p) {
                                if (!constraints_->allow(ps2, current.march_test.elements[p], p)) {
                                    rest_ok = false;
                                    break;
                                }
                                constraints_->update(ps2, current.march_test.elements[p], p);
                            }
                            if (!rest_ok) continue;
                        }

                        // Build trial: replace element at pos
                        MarchTest trial = current.march_test;
                        trial.elements[pos] = elem_variant;

                        SimulationResult simres = sim_.simulate(trial, faults_, tps_);
                        double score = scorer_(simres, trial);

                        if (simres.total_coverage > current.sim_result.total_coverage ||
                            (std::abs(simres.total_coverage - current.sim_result.total_coverage) < 1e-9
                             && score > current.score)) {
                            current.march_test = std::move(trial);
                            current.sim_result = std::move(simres);
                            current.score = score;
                            improved = true;
                        }
                    }
                }
            }
            if (!improved) break;
        }
        return current;
    }

private:
    FaultSimulator& sim_;
    const TemplateLibrary& lib_;
    const vector<Fault>& faults_;
    const vector<TestPrimitive>& tps_;
    std::unique_ptr<ICandidateGenerator> gen_;
    ScoreFunc scorer_;                        // v2: scoring strategy
    const SequenceConstraintSet* constraints_; // v2: optional sequence constraints
};

} // namespace template_search
} // namespace css
