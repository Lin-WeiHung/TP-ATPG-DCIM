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
#include <sstream>

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

// Element template: AddrOrder + 3 slots
class ElementTemplate {
public:
    ElementTemplate() = default;
    ElementTemplate(AddrOrder ord,
                    TemplateOpKind a, TemplateOpKind b, TemplateOpKind c) {
        order = ord;
        slots[0].kind = a; slots[1].kind = b; slots[2].kind = c;
    }
    // Accessors for generator/consumers
    AddrOrder get_order() const { return order; }
    const array<TemplateSlot,3>& get_slots() const { return slots; }
    // local validity checks: no "hole" (X - X) and at most one R/W/C
    bool is_valid() const {
        if (has_hole()) return false;
        if (has_multiple_rwc()) return false;
        return true;
    }
    // Helpers for sequence-level rules & heuristics
    bool has_kind(TemplateOpKind k) const {
        for (const auto& s : slots) if (s.kind == k) return true;
        return false;
    }
    int count_non_none() const {
        int c=0; for (const auto& s: slots) if (s.kind!=TemplateOpKind::None) ++c; return c;
    }
private:
    AddrOrder order{AddrOrder::Any};
    array<TemplateSlot,3> slots{};

    bool has_hole() const {
        bool seen_none=false;
        for (const auto& s : slots) {
            if (s.kind == TemplateOpKind::None) {
                seen_none = true;
            } else {
                if (seen_none) return true; // hole inside element
            }
        }
        return false;
    }
    bool has_multiple_rwc() const {
        int cntR=0,cntW=0,cntC=0;
        for (const auto& s : slots) {
            if (s.kind == TemplateOpKind::Read) ++cntR;
            if (s.kind == TemplateOpKind::Write) ++cntW;
            if (s.kind == TemplateOpKind::Compute) ++cntC;
        }
        return (cntR>1 || cntW>1 || cntC>1);
    }
};

// TemplateLibrary: small hand-picked PoC library; extendable
class TemplateLibrary {
public:
    using TemplateId = size_t;

    TemplateLibrary() = default;

    // static TemplateLibrary make_default() {
    //     TemplateLibrary lib;
    //     auto mk = [](AddrOrder ord, TemplateOpKind a, TemplateOpKind b, TemplateOpKind c) {
    //         ElementTemplate et(ord, a, b, c);
    //         if (!et.is_valid()) throw std::runtime_error("make_default created invalid template");
    //         return et;
    //     };
    //     // example set mapped to TailSig types; you can tune these
    //     lib.templates_.push_back(mk(AddrOrder::Up,    TemplateOpKind::Write, TemplateOpKind::None,  TemplateOpKind::None)); // Only W
    //     lib.templates_.push_back(mk(AddrOrder::Up,    TemplateOpKind::Write, TemplateOpKind::Read,  TemplateOpKind::None)); // W -> R
    //     lib.templates_.push_back(mk(AddrOrder::Down,  TemplateOpKind::Compute,TemplateOpKind::None,  TemplateOpKind::None)); // Only C
    //     lib.templates_.push_back(mk(AddrOrder::Down,  TemplateOpKind::Read, TemplateOpKind::Compute, TemplateOpKind::None)); // R -> C
    //     lib.templates_.push_back(mk(AddrOrder::Any,   TemplateOpKind::Write, TemplateOpKind::Compute, TemplateOpKind::None)); // W->C
    //     lib.templates_.push_back(mk(AddrOrder::Any,   TemplateOpKind::Compute,TemplateOpKind::Write, TemplateOpKind::None)); // C->W
    //     return lib;
    // }

    TemplateLibrary make_bruce() {
        TemplateLibrary lib;
        for (AddrOrder ord : {AddrOrder::Up, AddrOrder::Down}) {
            // brute-force all combinations of the 4 TemplateOpKind values into 3 slots
            for (TemplateOpKind a : {TemplateOpKind::None, TemplateOpKind::Read, TemplateOpKind::Write, TemplateOpKind::Compute}) {
                for (TemplateOpKind b : {TemplateOpKind::None, TemplateOpKind::Read, TemplateOpKind::Write, TemplateOpKind::Compute}) {
                    for (TemplateOpKind c : {TemplateOpKind::None, TemplateOpKind::Read, TemplateOpKind::Write, TemplateOpKind::Compute}) {
                        ElementTemplate et(ord, a, b, c);
                        if (et.is_valid()) lib.templates_.push_back(et);
                    }
                }
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

// TemplateEnumerator: generate all sequences of length L (with optional rule-set pruning later)
class TemplateEnumerator {
public:
    using TemplateId = TemplateLibrary::TemplateId;
    struct Sequence { vector<TemplateId> ids; };

    TemplateEnumerator(const TemplateLibrary& lib) : lib_(lib) {}

    // Enumerate all sequences length num_elements (max_candidates=0 -> unlimited)
    vector<Sequence> enumerate(size_t num_elements, size_t max_candidates = 0) const {
        vector<Sequence> out;
        if (num_elements==0 || lib_.size()==0) return out;
        Sequence cur; cur.ids.resize(num_elements);
        enumerate_rec(0, num_elements, max_candidates, cur, out);
        return out;
    }

private:
    const TemplateLibrary& lib_;
    void enumerate_rec(size_t pos, size_t num_elements, size_t max_candidates, Sequence& cur, vector<Sequence>& out) const {
        if (max_candidates>0 && out.size()>=max_candidates) return;
        if (pos==num_elements) { out.push_back(cur); return; }
        for (size_t tid=0; tid<lib_.size(); ++tid) {
            cur.ids[pos]=tid;
            enumerate_rec(pos+1, num_elements, max_candidates, cur, out);
            if (max_candidates>0 && out.size()>=max_candidates) return;
        }
    }
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
    virtual vector<MarchElement> generate(const TemplateLibrary& lib, TemplateLibrary::TemplateId tid) = 0;
};

class ValueExpandingGenerator : public ICandidateGenerator {
public:
    vector<MarchElement> generate(const TemplateLibrary& lib,
                                  TemplateLibrary::TemplateId tid) override {
        vector<MarchElement> out;
        const auto& et = lib.at(tid);

        const auto& slots = et.get_slots();
        const auto order = et.get_order();

        // 計算所需的 bit 數（R/W 1 bit；Compute 3 bits）
        struct SlotBits { TemplateOpKind kind; int base; };
        array<SlotBits,3> specs{};
        int cursor = 0;
        for (int i = 0; i < 3; ++i) {
            specs[i].kind = slots[i].kind;
            specs[i].base = cursor;
            switch (slots[i].kind) {
                case TemplateOpKind::None:    cursor += 0; break;
                case TemplateOpKind::Read:    cursor += 1; break;
                case TemplateOpKind::Write:   cursor += 1; break;
                case TemplateOpKind::Compute: cursor += 3; break;
            }
        }

        const int total_bits = cursor;
        if (total_bits == 0) {
            // 無任何操作：仍回傳一個空元素（只有 order）
            MarchElement e; e.order = order; // ops 保持空
            out.push_back(e);
            return out;
        }

        const int total_masks = 1 << total_bits;
        out.reserve(total_masks);

        for (int mask = 0; mask < total_masks; ++mask) {
            MarchElement e; e.order = order;
            // 依 slot 順序建立 ops
            for (int i = 0; i < 3; ++i) {
                const auto kind = specs[i].kind;
                const int base = specs[i].base;
                switch (kind) {
                    case TemplateOpKind::None:
                        break;
                    case TemplateOpKind::Read: {
                        Op o; o.kind = OpKind::Read;
                        const int bit = (mask >> base) & 1;
                        o.value = bit ? Val::One : Val::Zero;
                        e.ops.push_back(o);
                        break;
                    }
                    case TemplateOpKind::Write: {
                        Op o; o.kind = OpKind::Write;
                        const int bit = (mask >> base) & 1;
                        o.value = bit ? Val::One : Val::Zero;
                        e.ops.push_back(o);
                        break;
                    }
                    case TemplateOpKind::Compute: {
                        Op o; o.kind = OpKind::ComputeAnd;
                        const int bT = (mask >> (base+0)) & 1;
                        const int bM = (mask >> (base+1)) & 1;
                        const int bB = (mask >> (base+2)) & 1;
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
                           std::unique_ptr<ICandidateGenerator> gen = std::make_unique<ValueExpandingGenerator>())
        : sim_(simulator), lib_(lib), faults_(faults), tps_(tps), gen_(std::move(gen)) {}

    // Run greedy for skeleton length L. Returns chosen CandidateResult (single best path)
    CandidateResult run(size_t L) {
        CandidateResult best_overall;
        best_overall.score = -std::numeric_limits<double>::infinity();

        // We will build the sequence incrementally. Keep current prefix MarchTest and its sim state.
        MarchTest prefix_mt;
        prefix_mt.name = "greedy_prefix";

        // Remaining faults are tracked implicitly by sim reporter result; we always re-simulate the full prefix to get accurate remaining coverage.
        // For efficiency you may modify FaultSimulator to support incremental simulation; PoC uses full simulate each time.

        vector<TemplateLibrary::TemplateId> chosen_ids;
        chosen_ids.reserve(L);

        for (size_t pos = 0; pos < L; ++pos) {
            double best_score_this_pos = -std::numeric_limits<double>::infinity();
            TemplateLibrary::TemplateId best_tid = 0;
            MarchElement best_elem;
            SimulationResult best_sim;

            // For each candidate template id, generate element variants (e.g., different values)
            for (size_t tid = 0; tid < lib_.size(); ++tid) {
                auto elems = gen_->generate(lib_, tid);
                for (auto &elem_variant : elems) {
                    // form a trial MT = prefix + candidate element
                    MarchTest trial_mt = prefix_mt;
                    if (!elem_variant.ops.empty()) trial_mt.elements.push_back(elem_variant);

                    // simulate trial_mt against current (static) fault list to get coverage
                    SimulationResult simres = sim_.simulate(trial_mt, faults_, tps_);
                    double score = simres.total_coverage; // PoC: use total_coverage as metric

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

            // update best_overall if this prefix already looks best (we keep final L-length or best prefix)
            CandidateResult cr;
            cr.sequence = chosen_ids;
            cr.march_test = prefix_mt;
            cr.sim_result = best_sim;
            cr.score = best_sim.total_coverage;
            if (cr.score > best_overall.score) best_overall = std::move(cr);
        }

        // At the end, return best_overall (could be full-length or shorter if stopping earlier)
        return best_overall;
    }

private:
    FaultSimulator& sim_;
    const TemplateLibrary& lib_;
    const vector<Fault>& faults_;
    const vector<TestPrimitive>& tps_;
    std::unique_ptr<ICandidateGenerator> gen_;
};

// -----------------------------
// BeamTemplateSearcher (extensible)
//  - keep beam_width prefixes each level
//  - returns top_k final candidates
// -----------------------------
class BeamTemplateSearcher {
public:
    BeamTemplateSearcher(FaultSimulator& simulator,
                         const TemplateLibrary& lib,
                         const vector<Fault>& faults,
                         const vector<TestPrimitive>& tps,
                         size_t beam_width = 8,
                         std::unique_ptr<ICandidateGenerator> gen = std::make_unique<ValueExpandingGenerator>())
        : sim_(simulator), lib_(lib), faults_(faults), tps_(tps), beam_width_(beam_width), gen_(std::move(gen)) {}

    // Run beam search for length L, produce up to top_k final candidates (sorted by score desc)
    vector<CandidateResult> run(size_t L, size_t top_k = 1) {
        struct BeamNode {
            vector<TemplateLibrary::TemplateId> seq;
            MarchTest mt;
            SimulationResult sim; // simulation of mt
            double score{0.0};
        };

        // initialize beam with empty prefix
        vector<BeamNode> beam;
        BeamNode root; root.mt.name = "beam_root"; root.score = 0.0;
        // Optionally simulate empty root if you want baseline; we just keep empty
        beam.push_back(std::move(root));

        for (size_t pos = 0; pos < L; ++pos) {
            vector<BeamNode> candidates; candidates.reserve(beam.size() * lib_.size());

            // expand each beam node
            for (const auto& node : beam) {
                // for each template id
                for (size_t tid = 0; tid < lib_.size(); ++tid) {
                    auto elems = gen_->generate(lib_, tid);
                    for (auto &elem_variant : elems) {
                        // skip empty element (no ops) to avoid useless candidates, but allow if wanted
                        if (elem_variant.ops.empty()) continue;

                        BeamNode nb;
                        nb.seq = node.seq;
                        nb.seq.push_back(tid);
                        nb.mt = node.mt;
                        nb.mt.elements.push_back(elem_variant);
                        // simulate nb.mt to get accurate coverage given the progressive nature
                        nb.sim = sim_.simulate(nb.mt, faults_, tps_);
                        nb.score = nb.sim.total_coverage;

                        candidates.push_back(std::move(nb));
                    }
                }
            }

            // sort candidates by score desc and pick top beam_width_
            std::sort(candidates.begin(), candidates.end(),
                      [](const BeamNode& a, const BeamNode& b){ return a.score > b.score; });

            if (candidates.empty()) {
                // no expansions -> break early
                break;
            }

            size_t keep = std::min(beam_width_, candidates.size());
            beam.clear();
            beam.insert(beam.end(), candidates.begin(), candidates.begin() + keep);
        }

        // beam now contains final prefixes; convert to CandidateResult and sort by score
        vector<CandidateResult> results;
        for (const auto& node : beam) {
            CandidateResult cr;
            cr.sequence = node.seq;
            cr.march_test = node.mt;
            cr.sim_result = node.sim;
            cr.score = node.score;
            results.push_back(std::move(cr));
        }
        std::sort(results.begin(), results.end(), [](const CandidateResult& a, const CandidateResult& b){
            return a.score > b.score;
        });

        if (top_k>0 && results.size()>top_k) results.resize(top_k);
        return results;
    }

private:
    FaultSimulator& sim_;
    const TemplateLibrary& lib_;
    const vector<Fault>& faults_;
    const vector<TestPrimitive>& tps_;
    size_t beam_width_;
    std::unique_ptr<ICandidateGenerator> gen_;
};

// -----------------------------
// Utility: pretty print CandidateResult
// -----------------------------
inline void print_candidate_result(const CandidateResult& cr, std::ostream& os = std::cout) {
    os << "Score: " << cr.score << "\n";
    os << "Template sequence ids: ";
    for (auto id : cr.sequence) os << id << " ";
    os << "\nMarchTest (elements):\n";
    for (size_t i=0;i<cr.march_test.elements.size();++i) {
        const auto& e = cr.march_test.elements[i];
        os << "  Elem[" << i << "] order=" << (e.order==AddrOrder::Up?"Up":(e.order==AddrOrder::Down?"Down":"Any")) << " ops=";
        for (const auto& op : e.ops) {
            if (op.kind==OpKind::Read) os << "R" << (op.value==Val::Zero?"0":"1") << ",";
            else if (op.kind==OpKind::Write) os << "W" << (op.value==Val::Zero?"0":"1") << ",";
            else if (op.kind==OpKind::ComputeAnd) os << "C(" << (op.C_T==Val::One?"1":"0") << "," << (op.C_M==Val::One?"1":"0") << "," << (op.C_B==Val::One?"1":"0") << "),";
            else os << "?";
        }
        os << "\n";
    }
    os << "Sim total coverage: " << cr.sim_result.total_coverage << "\n";
}

} // namespace template_search
} // namespace css
