// MarchSynth.hpp
// Simplified Greedy March Test Generator (no Beam, no Cache).
// -------------------------------------------------------------
// - 直接呼叫 FaultSimulator::simulate() 取得真實結果。
// - 無 SequenceHasher、無 SimCache。
// - 關閉條件：若 Δstate/Δsens/Δdetect 全 0，或僅 detect>0 且 defer_detect_only=true。
// - 寫法：Header-style（介面＋inline 實作），SRP原則，每個函式只負責一件事。

// 編譯:
// g++ -std=c++17 -O2 -Wall -Wextra -Iinclude src/ATPG.cpp -o atpg_demo
// 執行:
// ./atpg_demo input/S_C_faults.json | head -n 60
// 合併:
// g++ -std=c++17 -O2 -Wall -Wextra -Iinclude src/ATPG.cpp -o atpg_demo && ./atpg_demo input/S_C_faults.json | head -n 60

#pragma once

#include <string>
#include <vector>
#include <stdexcept>
#include <algorithm>

#include "FaultSimulator.hpp"
#include "FpParserAndTpGen.hpp"

// ---------- precise usings ----------
using std::string;
using std::vector;
using std::runtime_error;

// ================================================================
// 設定與基本列舉
// ================================================================

/**
 * @brief 可用的操作類型（可擴充）
 */
enum class GenOp {
    W0 = 0,
    W1 = 1,
    R0 = 2,
    R1 = 3,
    C_0_0_0 = 4,
    C_0_0_1 = 5,
    C_0_1_0 = 6,
    C_0_1_1 = 7,
    C_1_0_0 = 8,
    C_1_0_1 = 9,
    C_1_1_0 = 10,
    C_1_1_1 = 11
};

/**
 * @brief 合成器設定參數
 */
struct SynthConfig {
    int alpha_state  = 1;     ///< Δstate 權重
    int beta_sens    = 1;     ///< Δsens 權重
    int gamma_detect = 4;     ///< Δdetect 權重
    int lambda_mask  = 0;     ///< 遮蔽懲罰（暫時不用）
    int mu_cost      = 1;     ///< 每放一個 op 的固定成本
    bool defer_detect_only = true; ///< 純偵測期→關閉 element
    int max_ops      = 60;  ///< 操作數上限
};

// ================================================================
// RawMarchEditor：專責編輯 MarchTest 結構
// ================================================================

class RawMarchEditor {
public:
    explicit RawMarchEditor(const string& name) {
        mt_.name = name;
        ensure_non_empty();
    }

    explicit RawMarchEditor(const MarchTest& base) : mt_(base) {
        ensure_non_empty();
    }

    const MarchTest& get_cur_mt() const { return mt_; }

    void set_current_order(AddrOrder order) { mt_.elements.back().order = order; }

    AddrOrder current_order() const { return mt_.elements.back().order; }

    void close_and_start_new_element(AddrOrder next_order) {
        MarchElement e; 
        e.order = next_order;
        mt_.elements.push_back(e);
    }

    void append_op_to_current_element(GenOp gop) {
        Op op;
        switch (gop) {
            case GenOp::W0:
                op.kind = OpKind::Write;
                op.value = Val::Zero;
                break;
            case GenOp::W1:
                op.kind = OpKind::Write;
                op.value = Val::One;
                break;
            case GenOp::R0:
                op.kind = OpKind::Read;
                op.value = Val::Zero;
                break;
            case GenOp::R1:
                op.kind = OpKind::Read;
                op.value = Val::One;
                break;
            case GenOp::C_0_0_0:
            case GenOp::C_0_0_1:
            case GenOp::C_0_1_0:
            case GenOp::C_0_1_1:
            case GenOp::C_1_0_0:
            case GenOp::C_1_0_1:
            case GenOp::C_1_1_0:
            case GenOp::C_1_1_1:
                op.kind = OpKind::ComputeAnd;
                op.value = Val::X;
                int offset = static_cast<int>(gop) - static_cast<int>(GenOp::C_0_0_0);
                op.C_T = (offset & 0b100) ? Val::One : Val::Zero;
                op.C_M = (offset & 0b010) ? Val::One : Val::Zero;
                op.C_B = (offset & 0b001) ? Val::One : Val::Zero;
                break;
        }
        mt_.elements.back().ops.push_back(op);
    }

private:
    MarchTest mt_;
    void ensure_non_empty() {
        if (mt_.elements.empty()) {
            MarchElement e; 
            e.order = AddrOrder::Any;
            mt_.elements.push_back(e);
        }
    }
};

// ================================================================
// 模擬呼叫封裝器
// ================================================================

/**
 * @brief SimulatorAdaptor：包裝 FaultSimulator::simulate()
 */
class SimulatorAdaptor {
public:
    SimulatorAdaptor(const vector<Fault>& faults, const vector<TestPrimitive>& tps)
        : faults_(faults), tps_(tps) {}

    SimulationResult run(const MarchTest& mt) {
        return fs_.simulate(mt, faults_, tps_);
    }

private:
    const vector<Fault>& faults_;
    const vector<TestPrimitive>& tps_;
    FaultSimulator fs_;
};

// ================================================================
// 差分與評分
// ================================================================

struct Delta {
    double dState{0.0};
    double dSens{0.0};
    double dDetect{0.0};
    double dCoverage{0.0};
};

/**
 * @brief DiffScorer：負責計算 Δ 與 gain 分數
 */
class DiffScorer {
public:
    explicit DiffScorer(const SynthConfig& cfg) : cfg_(cfg) {}

    Delta compute(const SimulationResult& before, const SimulationResult& after) const {
        Delta d;
        d.dState    = after.state_coverage - before.state_coverage;
        d.dSens     = after.sens_coverage  - before.sens_coverage;
        d.dDetect   = after.detect_coverage - before.detect_coverage;
        d.dCoverage = after.total_coverage - before.total_coverage;
        return d;
    }

    double gain(const Delta& d) const {
        // 以 double 計分，維持整數型權重亦可正確隱式轉型
        return static_cast<double>(cfg_.alpha_state)  * d.dState
             + static_cast<double>(cfg_.beta_sens)    * d.dSens
             + static_cast<double>(cfg_.gamma_detect) * d.dDetect
             - static_cast<double>(cfg_.lambda_mask)  * 0.0  // (暫時不用)
             - static_cast<double>(cfg_.mu_cost);
    }

private:
    SynthConfig cfg_;
};

// ================================================================
// Element 關閉策略
// ================================================================

class ElementPolicy {
public:
    explicit ElementPolicy(const SynthConfig& cfg) : cfg_(cfg) {}

    bool should_close(const vector<Delta>& deltas) const {
        if (deltas.empty()) return false;
        // 使用 double 並加入容忍度，避免浮點誤差導致邏輯偏差
        double maxS = 0.0, maxZ = 0.0, maxD = 0.0;
        for (const auto& d : deltas) {
            maxS = std::max(maxS, d.dState);
            maxZ = std::max(maxZ, d.dSens);
            maxD = std::max(maxD, d.dDetect);
        }
        const double eps = 1e-12;
        const bool all_zero = (maxS <= eps && maxZ <= eps && maxD <= eps);
        if (all_zero) return true;
        if (cfg_.defer_detect_only && maxS <= eps && maxZ <= eps) return true;
        return false;
    }

private:
    SynthConfig cfg_;
};

// ================================================================
// 主控制器：GreedySynthDriver
// ================================================================

class GreedySynthDriver {
public:
    GreedySynthDriver(const SynthConfig& cfg,
                      const vector<Fault>& faults,
                      const vector<TestPrimitive>& tps)
        : cfg_(cfg),
          simulator_(faults, tps),
          scorer_(cfg),
          policy_(cfg) {}

    /**
     * @brief 執行貪婪式產生器
     */
    MarchTest run(const MarchTest& init_mt, double target_cov = 1.0) {
        MarchTest get_cur_mt = ensure_has_element(init_mt);
        SimulationResult cur_sim = simulator_.run(get_cur_mt);
        AddrOrder cur_order = get_cur_mt.elements.back().order;

        for (int step = 0; step < cfg_.max_ops; ++step) {
            if (cur_sim.total_coverage >= target_cov) break;

            // 列舉候選
            const vector<GenOp> candidates = { GenOp::W0, GenOp::W1, GenOp::R0, GenOp::R1,
                                              GenOp::C_0_0_0, GenOp::C_0_0_1, GenOp::C_0_1_0, GenOp::C_0_1_1,
                                              GenOp::C_1_0_0, GenOp::C_1_0_1, GenOp::C_1_1_0, GenOp::C_1_1_1 };
            vector<Delta> deltas; deltas.reserve(candidates.size());
            struct CandEval { GenOp gop; double gain; SimulationResult after; Delta d; };
            vector<CandEval> evals; evals.reserve(candidates.size());

            // 模擬每個候選
            for (auto gop : candidates) {
                MarchTest mt_after = append_op(get_cur_mt, cur_order, gop);
                SimulationResult after = simulator_.run(mt_after);
                Delta d = scorer_.compute(cur_sim, after);
                double g = scorer_.gain(d);
                deltas.push_back(d);
                evals.push_back({gop, g, after, d});
            }

            // 檢查關閉條件
            if (policy_.should_close(deltas)) {
                get_cur_mt = close_element(get_cur_mt, cur_order);
                cur_order = flip_order(cur_order);
                cur_sim = simulator_.run(get_cur_mt);
                continue;
            }

            // 選出 gain 最大者
            auto best = std::max_element(evals.begin(), evals.end(),
                                         [](const CandEval& a, const CandEval& b){ return a.gain < b.gain; });
            if (best == evals.end()) break;

            get_cur_mt = append_op(get_cur_mt, cur_order, best->gop);
            cur_sim = best->after;
        }

        return get_cur_mt;
    }

private:
    SynthConfig cfg_;
    SimulatorAdaptor simulator_;
    DiffScorer scorer_;
    ElementPolicy policy_;

    static MarchTest ensure_has_element(MarchTest mt) {
        if (mt.elements.empty()) {
            MarchElement e; 
            e.order = AddrOrder::Any;
            e.ops.push_back({OpKind::Write, Val::Zero}); // 預設放一個 W0
            mt.elements.push_back(e);
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
        MarchElement e; 
        e.order = flip_order(cur_order);
        mt.elements.push_back(e);
        return mt;
    }
};
