#pragma once

#include <vector>
#include <string>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <limits>

#include "FaultSimulator.hpp" // for MarchTest, FaultSimulator, Op, OpKind, Val, Fault, TestPrimitive

// =============================================================
// MarchTestRefiner
// 依據需求：在不降低 state coverage = 100% 的前提下，對 MarchTest 進行局部 patch 插入，
// 用於補齊所有 TP 的 sensitization / detection，並考慮 tp group 的最後活路條件。
// 初版僅使用整體 FaultSimulator；保留 Delta Simulation 擴充點。
// =============================================================

// ---------- Forward / Basic Types ----------
using std::vector; using std::string; using std::optional; using std::unordered_map; using std::unordered_set;

// Insertion site 定義：一個 element 內的相鄰 op 之間的 gap（不含頭前/尾後），或擴充為跨 element 邊界 (可選)。
// 目前：僅允許在 element 內『兩個既有 op 之間』插入，避免破壞 head / tail 語意。
struct InsertionSite {
    int elem_index{-1};      // 所在 MarchElement index
    int after_op_index{-1};  // 在 element.ops 中的索引 i 之後插入 (i 與 i+1 之間)，i >=0
};

// PatchCandidate: 一個備選的 op 序列 (短 patch)。
struct PatchCandidate {
    vector<Op> ops;                  // 欲插入的操作序列
    double detect_gain{0.0};         // 預估 detect coverage 增量
    double sens_gain{0.0};           // 預估 sens coverage 增量
    bool fulfills_group_need{false}; // 是否滿足某些最後活路 group 需求
    int op_cost{0};                  // 操作數量成本 (ops.size())
    double mask_risk{0.0};           // 遮蔽風險（粗估）
    double score{0.0};               // 綜合分數（Scoring 後）
    bool state_ok{false};            // FaultSimulator 驗證後 state coverage 是否仍 100%
    bool coverage_progress{false};   // 是否至少提升或不惡化 detect coverage
    bool evaluated{false};           // 是否完成評估
    string reject_reason;            // 安全/衝突/低分/不改善 等原因
};

// 每次 refine 主迴圈後的狀態概覽
struct RefinementStatus {
    int iteration{0};
    double state_coverage{0.0};
    double detect_coverage{0.0};
    double sens_coverage{0.0};
    size_t uncovered_tp_groups{0};
    bool need_second_round_state{false}; // 若存在最後活路 site 無法補救的 group
    bool no_progress_rounds_exceeded{false}; // 多輪無提升
    bool done{false};
};

// 結果封裝
struct MarchRefineResult {
    MarchTest refined;                 // 修改後的 March test
    vector<RefinementStatus> history;  // 每輪記錄
    vector<string> second_round_groups;// 需要第二輪 state 的 group 標記 (fault_id+OG)
};

// 迭代與評估日誌，便於輸出 HTML
struct PatchEvalLog {
    vector<Op> ops;
    double score{0.0};
    double detect_gain{0.0};
    double sens_gain{0.0};
    bool state_ok{false};
    bool coverage_progress{false};
    bool selected{false};
    string reject_reason; // safety/conflict/low-score/no-improve
};

struct IterationLog {
    int iter{0};
    int site_index{-1};
    int elem_index{-1};
    int after_op_index{-1};
    int op_increment{0};
    double delta_detect{0.0};
    double delta_sens{0.0};
    int last_resort_groups{0};
    vector<PatchEvalLog> patches;
};

struct RefineLog {
    vector<IterationLog> iters;
};

// 參數設定
struct RefineConfig {
    int max_iterations{50};
    int max_no_progress_rounds{3};
    int max_patch_len{4}; // 允許的最長 patch 長度
    bool enable_cross_element_site{false}; // 未來可擴充
};

// Scoring 權重 (可與 OpScorer 類似，這裡為 patch 層級)
struct PatchScoreWeights {
    double w_detect_gain{2.0};
    double w_sens_gain{1.0};
    double w_group_need{3.0};
    double w_op_cost{-0.5};     // 成本為負權重
    double w_mask_risk{-1.0};   // 遮蔽風險懲罰
};

// 代表一個 TP Group 的需求狀態
struct GroupNeedInfo {
    string fault_id;          // 對應 fault
    OrientationGroup og;      // 組向 (A_LT_V / A_GT_V / Single)
    bool covered_state{false};
    bool covered_sens{false};
    bool covered_detect{false};
    // 最後活路相關：能補救的 site 集合
    vector<int> candidate_site_indices; // 對應 MarchTestRefiner 內部 site 列表的索引
};

// -------------------------------------------------------------
// 主類別
// -------------------------------------------------------------
class MarchTestRefiner {
public:
    MarchRefineResult refine(const MarchTest& original,
                             const vector<Fault>& faults,
                             const vector<TestPrimitive>& tps,
                             FaultSimulator& simulator,
                             const RefineConfig& cfg = RefineConfig{},
                             RefineLog* log = nullptr);
private:
    // 1. 建立 insertion sites
    vector<InsertionSite> build_sites(const MarchTest& mt, const RefineConfig& cfg) const;
    // 2. 建立 group 需求 (簡化：僅根據初始模擬結果)
    vector<GroupNeedInfo> build_group_needs(const SimulationResult& base_sim, const vector<TestPrimitive>& tps) const;
    // 3. 依規則排序 site (單回合選擇要處理的 site)
    int select_site(const vector<InsertionSite>& sites,
                    const vector<GroupNeedInfo>& group_needs,
                    const SimulationResult& current_sim) const;
    // 4. 產生候選 patch (Detect-only / Sens+Detect / Minimal+Sens+Detect)
    vector<PatchCandidate> generate_patches(const MarchTest& mt,
                                            const InsertionSite& site,
                                            const vector<GroupNeedInfo>& group_needs,
                                            const RefineConfig& cfg) const;
    // 5. Scoring
    void score_patches(vector<PatchCandidate>& patches,
                       const PatchScoreWeights& w) const;
    // 6. 模擬驗證 + 篩選
    void evaluate_patches(vector<PatchCandidate>& patches,
                          const MarchTest& mt,
                          const InsertionSite& site,
                          const vector<Fault>& faults,
                          const vector<TestPrimitive>& tps,
                          FaultSimulator& simulator,
                          const SimulationResult& baseline_sim,
                          const vector<GroupNeedInfo>& group_needs,
                          int site_index) const;
    // 7. 套用最佳 patch
    void apply_patch(MarchTest& mt, const InsertionSite& site, const PatchCandidate& patch) const;
    // 8. 更新 group 需求 (簡化：重新模擬後再計算)
    void update_group_needs(vector<GroupNeedInfo>& needs,
                            const SimulationResult& sim,
                            const vector<TestPrimitive>& tps) const;
    // 9. 標記最後活路失敗的 group → second_round
    void collect_second_round(const vector<GroupNeedInfo>& needs,
                              vector<string>& second_round_out) const;

    // 10. 從未覆蓋 TP 擷取最短合法 patch（依 tp.ops_before_detect 產生長度<=max_patch_len 的後綴）
    vector<PatchCandidate> generate_minimal_patches_from_uncovered(const vector<TestPrimitive>& tps,
                                                                   const vector<size_t>& tp_gids,
                                                                   int max_patch_len) const;
    // 11. 建立 group→TP 列表對映
    unordered_map<string, vector<size_t>> build_group_to_tp_map(const vector<TestPrimitive>& tps) const;
    // 12. 從 SimulationResult 萃取已偵測到的 group key 集合
    unordered_set<string> detected_groups_set(const SimulationResult& sim,
                                              const vector<TestPrimitive>& tps) const;
    // 13. 為各 group 計算可補救的 site（有任一 patch 通過並使該 group 由未覆蓋→覆蓋）
    void compute_group_site_candidates(const MarchTest& mt,
                                       const vector<InsertionSite>& sites,
                                       const vector<Fault>& faults,
                                       const vector<TestPrimitive>& tps,
                                       FaultSimulator& simulator,
                                       const RefineConfig& cfg,
                                       const SimulationResult& baseline_sim,
                                       vector<GroupNeedInfo>& group_needs) const;
};

// -------------------------------------------------------------
// 實作細節（初版：多數為保留擴充接口，僅提供骨架與基本策略）
// -------------------------------------------------------------

inline vector<InsertionSite> MarchTestRefiner::build_sites(const MarchTest& mt, const RefineConfig& cfg) const {
    vector<InsertionSite> out; out.reserve(64);
    for (int ei = 0; ei < (int)mt.elements.size(); ++ei) {
        const auto& elem = mt.elements[ei];
        // 不觸碰 head/tail：gap 僅在 (op[i], op[i+1]) 之間，i from 0 .. size-2
        for (int i = 0; i + 1 < (int)elem.ops.size(); ++i) {
            out.push_back({ei, i});
        }
        if (cfg.enable_cross_element_site) {
            // 可選擴充：跨 element 邊界，但需額外檢查 head/tail 語意；暫不實作
        }
    }
    return out;
}

inline vector<GroupNeedInfo> MarchTestRefiner::build_group_needs(const SimulationResult& base_sim, const vector<TestPrimitive>& tps) const {
    // 建立 TP → group key 映射
    unordered_map<string, GroupNeedInfo> map; // key = fault_id + ":" + orientation enum int
    for (size_t gid = 0; gid < tps.size(); ++gid) {
        const auto& tp = tps[gid];
        string key = tp.parent_fault_id + ":" + std::to_string(int(tp.group));
        auto it = map.find(key);
        if (it == map.end()) {
            map.emplace(key, GroupNeedInfo{tp.parent_fault_id, tp.group, false,false,false,{}});
        }
    }
    // 根據 base_sim 填充 covered 狀態
    for (const auto& kv : base_sim.fault_detail_map) {
        const auto& fd = kv.second;
        // fd.state_tp_gids 等已分組 (但此處需迭代 tp，以判斷 orientation group)
        auto mark = [&](const vector<TpGid>& tp_list, auto setter){
            for (auto tp_gid : tp_list) {
                const auto& tp = tps[tp_gid];
                string key = tp.parent_fault_id + ":" + std::to_string(int(tp.group));
                auto it = map.find(key);
                if (it != map.end()) setter(it->second);
            }
        };
        mark(fd.state_tp_gids, [](GroupNeedInfo& g){ g.covered_state = true; });
        mark(fd.sens_tp_gids,  [](GroupNeedInfo& g){ g.covered_sens  = true; });
        mark(fd.detect_tp_gids,[](GroupNeedInfo& g){ g.covered_detect= true; });
    }
    // 收集
    vector<GroupNeedInfo> out; out.reserve(map.size());
    for (auto& kv : map) out.push_back(kv.second);
    return out;
}

inline int MarchTestRefiner::select_site(const vector<InsertionSite>& sites,
                                         const vector<GroupNeedInfo>& group_needs,
                                         const SimulationResult& current_sim) const {
    if (sites.empty()) return -1;
    // 1) 先找最後活路（某 group 僅剩一個 site 可補救）
    unordered_map<int,int> site_pressure; // site_index -> how many last-resort groups rely on it
    for (const auto& g : group_needs) {
        if (!g.covered_detect && g.candidate_site_indices.size() == 1) {
            site_pressure[g.candidate_site_indices[0]]++;
        }
    }
    int best_site = -1; int best_pressure = -1;
    for (const auto& kv : site_pressure) {
        if (kv.second > best_pressure) { best_pressure = kv.second; best_site = kv.first; }
    }
    if (best_site != -1) return best_site;

    // 2) 否則挑選被最多未覆蓋 group 視為候選的 site（Group imbalance 粗略處理）
    unordered_map<int,int> site_votes; // how many groups include this site
    for (const auto& g : group_needs) {
        if (g.covered_detect) continue;
        for (int si : g.candidate_site_indices) site_votes[si]++;
    }
    int best_vote = -1; int best_idx = -1;
    for (const auto& kv : site_votes) {
        if (kv.second > best_vote) { best_vote = kv.second; best_idx = kv.first; }
    }
    if (best_idx != -1) return best_idx;

    // 3) fallback：round-robin by size
    static size_t rr = 0; rr %= sites.size();
    return (int)(rr++);
}

inline vector<PatchCandidate> MarchTestRefiner::generate_patches(const MarchTest& mt,
                                                                 const InsertionSite& site,
                                                                 const vector<GroupNeedInfo>& group_needs,
                                                                 const RefineConfig& cfg) const {
    vector<PatchCandidate> out;
    // 極簡生成：嘗試插入單一 Write 或 Read；實務上需根據尚未覆蓋的 TP 的 sensitizer / detector pattern 來合成。
    // Detect-only: 嘗試插入一個 Read0 / Read1
    for (Val v : {Val::Zero, Val::One}) {
        Op r; r.kind = OpKind::Read; r.value = v;
        PatchCandidate pc; pc.ops = {r}; pc.op_cost = 1; out.push_back(pc);
    }
    // Sens+Detect: 寫一個值再讀（W0,R0 / W1,R1）
    for (Val v : {Val::Zero, Val::One}) {
        Op w; w.kind = OpKind::Write; w.value = v;
        Op r; r.kind = OpKind::Read; r.value = v;
        PatchCandidate pc; pc.ops = {w,r}; pc.op_cost = 2; out.push_back(pc);
    }
    // Minimal fixes + Sens + Detect: 寫反再讀 (W0,R1 / W1,R0) 作為多樣性
    {
        Op w0; w0.kind = OpKind::Write; w0.value = Val::Zero; Op r1; r1.kind = OpKind::Read; r1.value = Val::One;
        PatchCandidate pc; pc.ops = {w0,r1}; pc.op_cost = 2; out.push_back(pc);
    }
    {
        Op w1; w1.kind = OpKind::Write; w1.value = Val::One; Op r0; r0.kind = OpKind::Read; r0.value = Val::Zero;
        PatchCandidate pc; pc.ops = {w1,r0}; pc.op_cost = 2; out.push_back(pc);
    }
    // TODO: 依未覆蓋 TP 的 sensitizer 序列截取組合生成更精準的 patch。
    (void)mt; (void)site; (void)group_needs; (void)cfg; // 暫不使用
    return out;
}

inline vector<PatchCandidate> MarchTestRefiner::generate_minimal_patches_from_uncovered(
        const vector<TestPrimitive>& tps,
        const vector<size_t>& tp_gids,
        int max_patch_len) const {
    vector<PatchCandidate> out;
    for (auto gid : tp_gids) {
        const auto& tp = tps[gid];
        const auto& ops = tp.ops_before_detect;
        // 取後綴長度 1..max_patch_len
        for (int k = 1; k <= max_patch_len; ++k) {
            if ((int)ops.size() < k) break;
            PatchCandidate pc; pc.op_cost = k;
            pc.ops.insert(pc.ops.end(), ops.end()-k, ops.end());
            out.push_back(pc);
        }
        // 若沒有 sensitizer，且 detector 為 Read 且有值 → Detect-only
        if (ops.empty() && tp.detector.detectOp.kind == OpKind::Read && tp.detector.detectOp.value != Val::X) {
            PatchCandidate pc; pc.ops = { tp.detector.detectOp }; pc.op_cost = 1; out.push_back(pc);
        }
    }
    return out;
}

inline unordered_map<string, vector<size_t>> MarchTestRefiner::build_group_to_tp_map(const vector<TestPrimitive>& tps) const {
    unordered_map<string, vector<size_t>> g2tp;
    for (size_t i = 0; i < tps.size(); ++i) {
        const auto& tp = tps[i];
        string key = tp.parent_fault_id + ":" + std::to_string(int(tp.group));
        g2tp[key].push_back(i);
    }
    return g2tp;
}

inline unordered_set<string> MarchTestRefiner::detected_groups_set(const SimulationResult& sim,
                                                                   const vector<TestPrimitive>& tps) const {
    unordered_set<string> out;
    for (const auto& kv : sim.fault_detail_map) {
        const auto& fd = kv.second;
        for (auto gid : fd.detect_tp_gids) {
            const auto& tp = tps[gid];
            out.insert(tp.parent_fault_id + ":" + std::to_string(int(tp.group)));
        }
    }
    return out;
}

inline void MarchTestRefiner::compute_group_site_candidates(const MarchTest& mt,
                                                            const vector<InsertionSite>& sites,
                                                            const vector<Fault>& faults,
                                                            const vector<TestPrimitive>& tps,
                                                            FaultSimulator& simulator,
                                                            const RefineConfig& cfg,
                                                            const SimulationResult& baseline_sim,
                                                            vector<GroupNeedInfo>& group_needs) const {
    if (sites.empty()) return;
    auto g2tp = build_group_to_tp_map(tps);
    auto detected_base = detected_groups_set(baseline_sim, tps);

    for (auto& g : group_needs) {
        g.candidate_site_indices.clear();
        if (g.covered_detect) continue;
        string key = g.fault_id + ":" + std::to_string(int(g.og));
        auto it = g2tp.find(key);
        if (it == g2tp.end()) continue;
        // 為此 group 準備候選 patch（來自未覆蓋 TP）
        auto minimal_patches = generate_minimal_patches_from_uncovered(tps, it->second, cfg.max_patch_len);
        // 對每個 site 測試少量 patch（限前幾個以節流）
        int test_cap = std::min<int>((int)minimal_patches.size(), 8);
        for (int si = 0; si < (int)sites.size(); ++si) {
            bool site_ok = false;
            for (int pi = 0; pi < test_cap; ++pi) {
                const auto& cand = minimal_patches[pi];
                MarchTest tmp = mt;
                // 插入
                if (sites[si].elem_index < 0 || sites[si].elem_index >= (int)tmp.elements.size()) continue;
                auto& elem = tmp.elements[sites[si].elem_index];
                if (sites[si].after_op_index < 0 || sites[si].after_op_index >= (int)elem.ops.size()-1) continue;
                vector<Op> new_ops; new_ops.reserve(elem.ops.size() + cand.ops.size());
                for (int j = 0; j <= sites[si].after_op_index; ++j) new_ops.push_back(elem.ops[j]);
                new_ops.insert(new_ops.end(), cand.ops.begin(), cand.ops.end());
                for (int j = sites[si].after_op_index + 1; j < (int)elem.ops.size(); ++j) new_ops.push_back(elem.ops[j]);
                elem.ops.swap(new_ops);
                // 模擬
                auto sim = simulator.simulate(tmp, faults, tps);
                if (sim.state_coverage + 1e-12 < baseline_sim.state_coverage) {
                    continue; // state coverage 降低則淘汰
                }
                auto detected_after = detected_groups_set(sim, tps);
                bool improved = (detected_after.find(key) != detected_after.end()) && (detected_base.find(key) == detected_base.end());
                if (improved) { site_ok = true; break; }
            }
            if (site_ok) g.candidate_site_indices.push_back(si);
        }
    }
}

inline void MarchTestRefiner::score_patches(vector<PatchCandidate>& patches,
                                            const PatchScoreWeights& w) const {
    for (auto& p : patches) {
        p.score = w.w_detect_gain * p.detect_gain +
                  w.w_sens_gain   * p.sens_gain +
                  (p.fulfills_group_need ? w.w_group_need : 0.0) +
                  w.w_op_cost     * p.op_cost +
                  w.w_mask_risk   * p.mask_risk;
    }
}

inline void MarchTestRefiner::evaluate_patches(vector<PatchCandidate>& patches,
                                               const MarchTest& mt,
                                               const InsertionSite& site,
                                               const vector<Fault>& faults,
                                               const vector<TestPrimitive>& tps,
                                               FaultSimulator& simulator,
                                               const SimulationResult& baseline_sim,
                                               const vector<GroupNeedInfo>& group_needs,
                                               int site_index) const {
    auto detected_base = detected_groups_set(baseline_sim, tps);
    for (auto& p : patches) {
        if (p.ops.empty()) continue;
        MarchTest tmp = mt;
        if (site.elem_index < 0 || site.elem_index >= (int)tmp.elements.size()) { p.evaluated=true; p.reject_reason="conflict"; continue; }
        auto& elem = tmp.elements[site.elem_index];
        if (site.after_op_index < 0 || site.after_op_index >= (int)elem.ops.size()-1) { p.evaluated=true; p.reject_reason="conflict"; continue; }
        vector<Op> new_ops; new_ops.reserve(elem.ops.size() + p.ops.size());
        for (int i = 0; i <= site.after_op_index; ++i) new_ops.push_back(elem.ops[i]);
        new_ops.insert(new_ops.end(), p.ops.begin(), p.ops.end());
        for (int i = site.after_op_index + 1; i < (int)elem.ops.size(); ++i) new_ops.push_back(elem.ops[i]);
        elem.ops.swap(new_ops);

        // 模擬
        SimulationResult sim = simulator.simulate(tmp, faults, tps);
        p.state_ok = (sim.state_coverage + 1e-12 >= baseline_sim.state_coverage);
        p.coverage_progress = (sim.detect_coverage + 1e-12 >= baseline_sim.detect_coverage);
        p.evaluated = true;
        if (!p.state_ok) { p.detect_gain = -1.0; p.reject_reason = "safety"; continue; }
        p.detect_gain = std::max(0.0, sim.detect_coverage - baseline_sim.detect_coverage);
        p.sens_gain   = std::max(0.0, sim.sens_coverage   - baseline_sim.sens_coverage);
        if (!p.coverage_progress) { p.reject_reason = "no-improve"; }

        // 是否滿足最後活路需求：若本 site 在某 group 是唯一候選，且此 patch 使之從未覆蓋→覆蓋
        auto detected_after = detected_groups_set(sim, tps);
        bool fulfill = false;
        for (const auto& g : group_needs) {
            if (g.covered_detect) continue;
            if (g.candidate_site_indices.size() == 1 && g.candidate_site_indices[0] == site_index) {
                string key = g.fault_id + ":" + std::to_string(int(g.og));
                if (detected_base.find(key) == detected_base.end() && detected_after.find(key) != detected_after.end()) {
                    fulfill = true; break;
                }
            }
        }
        p.fulfills_group_need = fulfill;
    }
}

inline void MarchTestRefiner::apply_patch(MarchTest& mt, const InsertionSite& site, const PatchCandidate& patch) const {
    if (site.elem_index < 0 || site.elem_index >= (int)mt.elements.size()) return;
    auto& elem = mt.elements[site.elem_index];
    if (site.after_op_index < 0 || site.after_op_index >= (int)elem.ops.size()-1) return;
    // 插入於 after_op_index 後面位置
    vector<Op> new_ops; new_ops.reserve(elem.ops.size() + patch.ops.size());
    for (int i = 0; i <= site.after_op_index; ++i) new_ops.push_back(elem.ops[i]);
    for (const auto& op : patch.ops) new_ops.push_back(op);
    for (int i = site.after_op_index + 1; i < (int)elem.ops.size(); ++i) new_ops.push_back(elem.ops[i]);
    elem.ops.swap(new_ops);
}

inline void MarchTestRefiner::update_group_needs(vector<GroupNeedInfo>& needs,
                                                 const SimulationResult& sim,
                                                 const vector<TestPrimitive>& tps) const {
    // 重設並重新計算
    for (auto& n : needs) { n.covered_state = n.covered_sens = n.covered_detect = false; }
    for (const auto& kv : sim.fault_detail_map) {
        const auto& fd = kv.second;
        auto mark = [&](const vector<TpGid>& tp_list, auto setter){
            for (auto tp_gid : tp_list) {
                const auto& tp = tps[tp_gid];
                for (auto& g : needs) {
                    if (g.fault_id == tp.parent_fault_id && g.og == tp.group) setter(g);
                }
            }
        };
        mark(fd.state_tp_gids, [](GroupNeedInfo& g){ g.covered_state = true; });
        mark(fd.sens_tp_gids,  [](GroupNeedInfo& g){ g.covered_sens  = true; });
        mark(fd.detect_tp_gids,[](GroupNeedInfo& g){ g.covered_detect= true; });
    }
}

inline void MarchTestRefiner::collect_second_round(const vector<GroupNeedInfo>& needs,
                                                   vector<string>& second_round_out) const {
    for (const auto& g : needs) {
        if (!g.covered_detect) {
            // 簡化：若 detect 仍未覆蓋且 (假設) 僅剩單一 site 可補救，但初版未計算 site → 直接視為需要第二輪。
            second_round_out.push_back(g.fault_id + ":" + std::to_string(int(g.og)));
        }
    }
}

inline MarchRefineResult MarchTestRefiner::refine(const MarchTest& original,
                                                  const vector<Fault>& faults,
                                                  const vector<TestPrimitive>& tps,
                                                  FaultSimulator& simulator,
                                                  const RefineConfig& cfg,
                                                  RefineLog* log) {
    MarchRefineResult result; result.refined = original;
    // 初始模擬
    SimulationResult base = simulator.simulate(result.refined, faults, tps);
    double baseline_state = base.state_coverage;
    double baseline_detect = base.detect_coverage;
    double baseline_sens = base.sens_coverage;

    auto sites = build_sites(result.refined, cfg);
    auto group_needs = build_group_needs(base, tps);
    // 建立 group → site 候選（支援最後活路策略）
    compute_group_site_candidates(result.refined, sites, faults, tps, simulator, cfg, base, group_needs);

    PatchScoreWeights weights; // 預設權重
    int no_progress_rounds = 0;
    for (int iter = 1; iter <= cfg.max_iterations; ++iter) {
        int site_index = select_site(sites, group_needs, base);
        if (site_index < 0) break; // 無 site
        const auto& site = sites[site_index];

        // 生成一般候選 patch
        auto patches = generate_patches(result.refined, site, group_needs, cfg);
        // 補上針對本 site 的 group 未覆蓋最短敏化 patch
        auto g2tp = build_group_to_tp_map(tps);
        for (const auto& g : group_needs) {
            if (g.covered_detect) continue;
            if (std::find(g.candidate_site_indices.begin(), g.candidate_site_indices.end(), site_index) == g.candidate_site_indices.end()) continue;
            string key = g.fault_id + ":" + std::to_string(int(g.og));
            auto it = g2tp.find(key);
            if (it == g2tp.end()) continue;
            auto mins = generate_minimal_patches_from_uncovered(tps, it->second, cfg.max_patch_len);
            patches.insert(patches.end(), mins.begin(), mins.end());
        }

        evaluate_patches(patches, result.refined, site, faults, tps, simulator, base, group_needs, site_index);
        score_patches(patches, weights);

        // 選擇合法最高分 patch
        const PatchCandidate* best = nullptr;
        for (const auto& p : patches) {
            if (!p.state_ok) continue; // 必須維持 state coverage 100%
            if (!p.coverage_progress) continue; // 不惡化 detect coverage
            if (!best || p.score > best->score) best = &p;
        }

        bool applied = false;
        IterationLog iterlog; if (log) {
            iterlog.iter = iter;
            iterlog.site_index = site_index;
            iterlog.elem_index = site.elem_index;
            iterlog.after_op_index = site.after_op_index;
            // 先把每個 patch 的評估資料記錄下來（selected 稍後標記）
            for (const auto& p : patches) {
                PatchEvalLog pl; pl.ops = p.ops; pl.score = p.score; pl.detect_gain=p.detect_gain; pl.sens_gain=p.sens_gain;
                pl.state_ok=p.state_ok; pl.coverage_progress=p.coverage_progress; pl.reject_reason=p.reject_reason; pl.selected=false;
                iterlog.patches.push_back(std::move(pl));
            }
            // 計算此 site 的最後活路 group 數
            int lr = 0; for (const auto& g : group_needs){ if(!g.covered_detect && g.candidate_site_indices.size()==1 && g.candidate_site_indices[0]==site_index) ++lr; }
            iterlog.last_resort_groups = lr;
        }
        if (best) {
            apply_patch(result.refined, site, *best);
            applied = true;
            // 重新模擬更新 baseline
            base = simulator.simulate(result.refined, faults, tps);
            update_group_needs(group_needs, base, tps);
            baseline_state = base.state_coverage; baseline_detect = base.detect_coverage; baseline_sens = base.sens_coverage;
            // 由於內容已變，重建 sites 與 group→site 候選
            sites = build_sites(result.refined, cfg);
            compute_group_site_candidates(result.refined, sites, faults, tps, simulator, cfg, base, group_needs);
            no_progress_rounds = (best->detect_gain <= 1e-9 && best->sens_gain <= 1e-9) ? no_progress_rounds + 1 : 0;
            if (log) {
                iterlog.op_increment = (int)best->ops.size();
                iterlog.delta_detect = best->detect_gain;
                iterlog.delta_sens   = best->sens_gain;
                // 標記選中的 patch
                for (auto& pl : iterlog.patches) {
                    if (pl.ops.size()==best->ops.size()) {
                        bool same = std::equal(pl.ops.begin(), pl.ops.end(), best->ops.begin(), best->ops.end(), [](const Op& a, const Op& b){
                            if (a.kind!=b.kind) return false; if (a.kind==OpKind::Read||a.kind==OpKind::Write) return a.value==b.value; if (a.kind==OpKind::ComputeAnd) return a.C_T==b.C_T && a.C_M==b.C_M && a.C_B==b.C_B; return false;
                        });
                        if (same) { pl.selected=true; }
                    }
                }
            }
        } else {
            no_progress_rounds++;
            if (log) {
                iterlog.op_increment = 0;
                iterlog.delta_detect = 0.0;
                iterlog.delta_sens   = 0.0;
            }
        }

        // 低分拒絕標記：對已通過安全且有改善者但未被選中的，標註 low-score
        if (log) {
            // 找已經標記 selected 的 patch signature
            for (auto& pl : iterlog.patches) {
                if (pl.selected) continue;
                if (pl.state_ok && pl.coverage_progress) {
                    if (pl.reject_reason.empty() || pl.reject_reason=="no-improve") pl.reject_reason = "low-score";
                }
            }
            log->iters.push_back(std::move(iterlog));
        }

        RefinementStatus st;
        st.iteration = iter;
        st.state_coverage = base.state_coverage;
        st.detect_coverage = base.detect_coverage;
        st.sens_coverage = base.sens_coverage;
        st.uncovered_tp_groups = std::count_if(group_needs.begin(), group_needs.end(), [](const GroupNeedInfo& g){ return !g.covered_detect; });
        st.no_progress_rounds_exceeded = (no_progress_rounds >= cfg.max_no_progress_rounds);
        result.history.push_back(st);

        if (st.uncovered_tp_groups == 0) { st.done = true; break; }
        if (st.no_progress_rounds_exceeded) { break; }
    }

    // 終止後判斷第二輪 state 需求
    collect_second_round(group_needs, result.second_round_groups);
    if (!result.second_round_groups.empty()) {
        if (!result.history.empty()) result.history.back().need_second_round_state = true;
    }
    if (!result.history.empty()) result.history.back().done = (result.history.back().uncovered_tp_groups == 0);
    return result;
}

// =============================================================
// TODO 擴充點：
// 1. Delta Simulation：evaluate_patches 時只重算受影響 op/state，不需全量 simulate。
// 2. 更精準的 patch generation：根據未覆蓋 TP 的 sensitizer / detector 模式生成最短補丁。
// 3. Last-Resort Site 標記：為每個 group 蒐集可補救 site，若僅剩一個則強制該 site 優先。
// 4. 遮蔽風險估計：基於 FaultSimulator 的 masked 資訊推算 patch 中各 Op 的遮蔽可能性。
// =============================================================
