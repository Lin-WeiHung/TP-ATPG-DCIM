#include "LookAheadService.hpp"
#include <algorithm>

void LookAheadService::enumerateCandidates(std::vector<Op>& out) const {
    out.clear(); out.reserve(31);
    Op w0; w0.kind=OpKind::Write; w0.value=Val::Zero; out.push_back(w0);
    Op w1; w1.kind=OpKind::Write; w1.value=Val::One;  out.push_back(w1);
    Op r0; r0.kind=OpKind::Read;  r0.value=Val::Zero; out.push_back(r0);
    Op r1; r1.kind=OpKind::Read;  r1.value=Val::One;  out.push_back(r1);
    for (Val t : {Val::Zero, Val::One}){
        for (Val m : {Val::Zero, Val::One}){
            for (Val b : {Val::Zero, Val::One}){
                Op c; c.kind=OpKind::ComputeAnd; c.C_T=t; c.C_M=m; c.C_B=b; out.push_back(c);
            }
        }
    }
}

CandidateScore LookAheadService::evalOne(const MarchTest& mt, int elemIndex, int insertedPos, const Op& op,
                                         const std::vector<TestPrimitive>& tps) const {
    // Build a new test with op inserted
    MarchTest test = mt;
    if (elemIndex < 0 || elemIndex >= (int)test.elements.size()) return {};
    auto& ops = test.elements[elemIndex].ops;
    int pos = std::clamp(insertedPos, 0, (int)ops.size());
    ops.insert(ops.begin()+pos, op);

    // Simulate (faults must be provided by caller through request; here we cannot access them, so evalOne shouldn't be called directly in public API without a request)
    FaultSimulator sim;
    // NOTE: This private helper is now used only from evaluate() which builds faults-aware simulation.
    std::vector<Fault> dummy_faults; // keep signature compatibility if called elsewhere
    auto simres = sim.simulate(test, dummy_faults, tps);

    // Find inserted op's flattened index by matching elem/index_within_elem
    int flatIndex = -1;
    for (size_t i=0;i<simres.op_table.size();++i){
        const auto& oc = simres.op_table[i];
        if (oc.elem_index == elemIndex && oc.index_within_elem == pos) { flatIndex = (int)i; break; }
    }
    CandidateScore out; out.candidate = op; out.cumulative_score = 0.0;
    if (flatIndex < 0) return out;

    // Score all ops and pick the inserted op's outcome
    OpScorer scorer; scorer.set_group_index(tps);
    auto outcomes = scorer.score_ops(simres.cover_lists);
    if ((int)outcomes.size() > flatIndex) out.outcome = outcomes[flatIndex];

    // Delta covers = inserted op's cover lists (since baseline had no op at this index)
    out.delta.delta = simres.cover_lists[flatIndex];
    out.cumulative_score = out.outcome.total_score;
    return out;
}

std::vector<StepResult> LookAheadService::evaluate(const LookAheadRequest& req) {
    std::vector<StepResult> steps;
    if (req.elemIndex < 0 || req.elemIndex >= (int)req.base.elements.size()) return steps;
    // Step 1 only (MVP). Enumerate candidates and evaluate.
    std::vector<Op> cands; enumerateCandidates(cands);
    StepResult s1; s1.stepIndex = 1;
    for (const auto& op : cands) {
        // Insert and simulate with provided faults and tps
        MarchTest test = req.base;
        auto& ops = test.elements[req.elemIndex].ops;
        int pos = std::clamp(req.insertPos, 0, (int)ops.size());
        ops.insert(ops.begin()+pos, op);

        FaultSimulator sim;
        auto simres = sim.simulate(test, req.faults, req.tps);
        // Find flattened index for inserted op
        int flatIndex = -1;
        for (size_t i=0;i<simres.op_table.size();++i){
            const auto& oc = simres.op_table[i];
            if (oc.elem_index == req.elemIndex && oc.index_within_elem == pos) { flatIndex = (int)i; break; }
        }
        CandidateScore cs; cs.candidate = op;
        if (flatIndex >= 0) {
            OpScorer scorer; scorer.set_group_index(req.tps); scorer.set_weights(req.weights);
            auto outcomes = scorer.score_ops(simres.cover_lists);
            if ((int)outcomes.size() > flatIndex) cs.outcome = outcomes[flatIndex];
            cs.delta.delta = simres.cover_lists[flatIndex];
            cs.cumulative_score = cs.outcome.total_score;
        }
        s1.candidates.push_back(cs);
    }
    // Sort by cumulative score descending
    std::sort(s1.candidates.begin(), s1.candidates.end(), [](const CandidateScore& a, const CandidateScore& b){ return a.cumulative_score > b.cumulative_score; });
    steps.push_back(std::move(s1));
    return steps;
}
