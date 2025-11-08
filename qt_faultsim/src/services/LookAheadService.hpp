#pragma once
#include <QObject>
#include <vector>
#include <optional>
#include "../../../include/FaultSimulator.hpp"

struct CandidateDelta {
    RawCoverLists delta; // covers for the inserted op (as delta vs baseline)
};

struct CandidateScore {
    Op candidate;
    OpScoreOutcome outcome;
    double cumulative_score{0.0};
    CandidateDelta delta;
};

struct StepResult {
    int stepIndex{0};
    std::vector<CandidateScore> candidates;
};

struct LookAheadRequest {
    MarchTest base;
    int elemIndex{0};
    int insertPos{0};
    int maxSteps{1};
    ScoreWeights weights{};
    std::vector<TestPrimitive> tps; // precomputed TPs for scoring
    std::vector<Fault> faults; // normalized faults for simulator
};

class LookAheadService {
public:
    std::vector<StepResult> evaluate(const LookAheadRequest& req);
private:
    void enumerateCandidates(std::vector<Op>& out) const;
    CandidateScore evalOne(const MarchTest& mt, int elemIndex, int insertedPos, const Op& op,
                           const std::vector<TestPrimitive>& tps) const;
};
