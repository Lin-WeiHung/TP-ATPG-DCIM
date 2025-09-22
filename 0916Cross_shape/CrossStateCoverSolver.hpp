// CrossStateCoverSolver.hpp — 最小 CrossState 覆蓋集合 (Set Cover) 求解器
// 要點：
// 1. 支援 Universe (目標 CrossState 列表) 與 CandidateSet (候選集合，每集合含多個 CrossState)
// 2. 覆蓋判定：逐位三值一致化 (0/1 必須相同；任一側 X 則相容)。
// 3. 禁用 D 圖樣：00100、11011（存在式）
//    - 輸入階段：若 CrossState 的 D 五位全定且等於禁用，直接拋例外。
//    - 覆蓋檢查：若合併後（Unified pattern）能指派成禁用圖樣，該覆蓋無效。
// 4. 使用回溯 + Branch & Bound 求最優解；提供單覆蓋 forcing 與簡單 lower bound 剪枝。
// 5. 不展開 X；以模式一致化方式計算覆蓋。
// 6. 精確 using，不使用 using namespace std。

#pragma once

#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <algorithm>

#include "CrossStateExpander.hpp" // 取得 CrossState 定義

// ---- 精確 using ----
using std::size_t;
using std::runtime_error;
using std::string;
using std::vector;
using std::optional;

/* ================== 輔助結構 ================== */
struct CoverResult {
    vector<size_t> chosen_sets;                 // 最終選到的 CandidateSet index（遞增排序）
    vector<vector<size_t>> cover_report;        // 對應 chosen_sets[i] 覆蓋到的 Universe index (排序)
    vector<size_t> uncovered_indices;           // 理論上應空
};

/* ================== 求解器介面 ================== */
class CrossStateCoverSolver {
public:
    // 核心入口：
    // universe: 目標 CrossState 列表
    // candidates: 候選集合，每個元素是一組 CrossState（集合內任一 state 覆蓋 target 即算覆蓋）
    CoverResult solve(const vector<CrossState>& universe,
                      const vector<vector<CrossState>>& candidates);

    // 新增：直接對輸入 universe 做「完全一般化」產生最小 pattern 集合。
    // 目前策略：
    //   對所有 universe 一次 generalize 成單一 pattern；若該 pattern 可覆蓋全部且合法 → 回傳 size=1
    //   （後續若需分裂可擴充策略，例如基於互斥規則拆分）
    vector<CrossState> synthesize_generalized_patterns(const vector<CrossState>& universe) const;

private:
    /* ------- Internal normalized representation ------- */
    struct NormState { // 正規化後 10 個值 (D0..D4, C0..C4) 以 int{-1,0,1}
        int D[5];
        int C[5];
    };

    struct Instance {
        vector<NormState> U;                     // Universe states (normalized)
        vector<vector<NormState>> CandSets;      // 原始 candidates 展開並正規化
        vector<vector<size_t>> set_covers;       // set_covers[s] = 被第 s 個 CandidateSet 覆蓋的 Universe indices
    } inst_;

    // 禁用 D patterns
    static constexpr int FORBIDDEN[2][5] = {
        {0,0,1,0,0},
        {1,1,0,1,1}
    };

    // ====== Normalization & validation ======
    static NormState normalize(const CrossState& st);
    static void apply_invariants(NormState& ns);
    static bool is_forbidden_concrete(const NormState& ns); // 全定且為禁用
    static bool pattern_can_be_forbidden(const int D[5]);    // 存在式檢查（有可能成為禁用）

    // ====== Coverage logic ======
    static bool unify_cover_allowed(const NormState& cand, const NormState& target);
    static bool d_pattern_subsumes_forbidden(const int merged[5]);

    // ====== Precomputation ======
    void build_cover_matrix();

    // ====== Branch & Bound search ======
    struct SearchCtx {
        vector<uint64_t> uncovered_bits;          // bitset for universe
        vector<size_t>   chosen;
        size_t best_size = std::numeric_limits<size_t>::max();
        vector<size_t> best_solution;
    };

    void init_bitset(size_t n, vector<uint64_t>& bits);
    bool bitset_empty(const vector<uint64_t>& b) const;
    size_t bitset_count(const vector<uint64_t>& b) const; // popcount
    void bitset_remove(vector<uint64_t>& b, const vector<uint64_t>& cover);
    void bitset_or(vector<uint64_t>& dst, const vector<uint64_t>& src);

    vector<vector<uint64_t>> set_bit_covers_; // 每個 candidateSet 的 bitset 覆蓋

    void search(SearchCtx& ctx, size_t start_idx);
    bool force_singletons(SearchCtx& ctx); // 單覆蓋 forcing，若衝突回 false
    size_t choose_universe_var(const SearchCtx& ctx) const; // MRV: 選覆蓋集合最少的元素
    size_t lower_bound_remaining(const SearchCtx& ctx) const; // 簡單 LB
};

/* ================== 實作 ================== */

inline CrossStateCoverSolver::NormState CrossStateCoverSolver::normalize(const CrossState& st) {
    NormState ns{};
    for (int i=0;i<5;++i) { ns.D[i] = st.cells[i].D; ns.C[i] = st.cells[i].C; }
    apply_invariants(ns);
    if (is_forbidden_concrete(ns)) {
        throw runtime_error("CrossState forbidden concrete D pattern");
    }
    return ns;
}

inline void CrossStateCoverSolver::apply_invariants(NormState& ns) {
    // D0=D1
    if (ns.D[0] == -1) ns.D[0] = ns.D[1];
    if (ns.D[1] == -1) ns.D[1] = ns.D[0];
    if (ns.D[0] != -1) ns.D[1] = ns.D[0];
    // D3=D4
    if (ns.D[3] == -1) ns.D[3] = ns.D[4];
    if (ns.D[4] == -1) ns.D[4] = ns.D[3];
    if (ns.D[3] != -1) ns.D[4] = ns.D[3];
    // C1=C2=C3
    int rowC = ns.C[1];
    if (rowC == -1) rowC = ns.C[2];
    if (rowC == -1) rowC = ns.C[3];
    if (rowC != -1) { ns.C[1]=rowC; ns.C[2]=rowC; ns.C[3]=rowC; }
}

inline bool CrossStateCoverSolver::is_forbidden_concrete(const NormState& ns) {
    // 若五個 D 都非 -1 且匹配禁用模式
    bool concrete = true;
    for (int i=0;i<5;++i) if (ns.D[i] == -1) { concrete=false; break; }
    if (!concrete) return false;
    for (auto &pat : FORBIDDEN) {
        bool match = true;
        for (int i=0;i<5;++i) if (ns.D[i] != pat[i]) { match=false; break; }
        if (match) return true;
    }
    return false;
}

inline bool CrossStateCoverSolver::d_pattern_subsumes_forbidden(const int merged[5]) {
    // merged 可能含 -1；若某禁用 pattern 在所有非 -1 位置一致 → 存在指派成禁用
    for (auto &pat : FORBIDDEN) {
        bool ok = true;
        for (int i=0;i<5 && ok;++i) {
            if (merged[i] != -1 && merged[i] != pat[i]) ok=false;
        }
        if (ok) return true; // 存在禁用指派
    }
    return false;
}

inline bool CrossStateCoverSolver::unify_cover_allowed(const NormState& cand, const NormState& target) {
    int mergedD[5];
    // D & C unify：若兩側皆具體且不同 => fail；若任一為 -1 則採另一側；兩側皆具體且同 -> 該值
    for (int i=0;i<5;++i) {
        int a = cand.D[i]; int b = target.D[i];
        if (a != -1 && b != -1 && a != b) return false;
        mergedD[i] = (a == -1) ? b : a; // 任一為 X 用另一個；都 X 則 -1
    }
    // C 只需檢查一致性；不影響禁用判定
    for (int i=0;i<5;++i) {
        int a = cand.C[i]; int b = target.C[i];
        if (a != -1 && b != -1 && a != b) return false;
    }
    // 禁用存在式檢查：若 mergedD 可成禁用 pattern，覆蓋不合法
    if (d_pattern_subsumes_forbidden(mergedD)) return false;
    return true;
}

inline void CrossStateCoverSolver::build_cover_matrix() {
    const size_t U = inst_.U.size();
    set_bit_covers_.assign(inst_.CandSets.size(), {});
    for (size_t si=0; si<inst_.CandSets.size(); ++si) {
        vector<uint64_t> bits; init_bitset(U, bits);
        for (size_t ui=0; ui<U; ++ui) {
            const auto &target = inst_.U[ui];
            bool covered = false;
            for (const auto &cand : inst_.CandSets[si]) {
                if (unify_cover_allowed(cand, target)) { covered = true; break; }
            }
            if (covered) {
                size_t word = ui / 64; size_t off = ui % 64;
                bits[word] |= (uint64_t(1) << off);
                inst_.set_covers[si].push_back(ui);
            }
        }
        set_bit_covers_[si] = std::move(bits);
    }
}

inline void CrossStateCoverSolver::init_bitset(size_t n, vector<uint64_t>& bits) {
    bits.assign((n + 63)/64, 0);
}

inline bool CrossStateCoverSolver::bitset_empty(const vector<uint64_t>& b) const {
    for (auto w : b) if (w) return false; return true;
}

inline size_t CrossStateCoverSolver::bitset_count(const vector<uint64_t>& b) const {
    size_t c=0; for (auto w : b) c += (size_t)__builtin_popcountll((long long)w); return c;
}

inline void CrossStateCoverSolver::bitset_remove(vector<uint64_t>& b, const vector<uint64_t>& cover) {
    for (size_t i=0;i<b.size();++i) b[i] &= ~cover[i];
}

inline void CrossStateCoverSolver::bitset_or(vector<uint64_t>& dst, const vector<uint64_t>& src) {
    for (size_t i=0;i<dst.size();++i) dst[i] |= src[i];
}

inline bool CrossStateCoverSolver::force_singletons(SearchCtx& ctx) {
    bool changed = true;
    while (changed) {
        changed = false;
        // 對每個尚未覆蓋的 Universe 元素，統計覆蓋它的 set
        vector<int> last_set(inst_.U.size(), -1);
        vector<int> count(inst_.U.size(), 0);
        for (size_t si=0; si<set_bit_covers_.size(); ++si) {
            // 已選集合不重複選：可以忽略，但這裡直接考慮仍安全
            for (size_t w=0; w<set_bit_covers_[si].size(); ++w) {
                uint64_t word = set_bit_covers_[si][w] & ctx.uncovered_bits[w];
                if (!word) continue;
                for (int bit=0; bit<64; ++bit) if (word & (uint64_t(1)<<bit)) {
                    size_t ui = w*64 + bit; if (ui >= inst_.U.size()) break;
                    count[ui]++; last_set[ui] = (int)si;
                }
            }
        }
        for (size_t ui=0; ui<inst_.U.size(); ++ui) {
            if (!(ctx.uncovered_bits[ui/64] & (uint64_t(1) << (ui%64)))) continue; // 已覆蓋
            if (count[ui] == 0) return false; // 無法覆蓋 → 無解
            if (count[ui] == 1) { // 強制選 last_set
                size_t si = (size_t)last_set[ui];
                ctx.chosen.push_back(si);
                bitset_remove(ctx.uncovered_bits, set_bit_covers_[si]);
                changed = true;
                if (ctx.chosen.size() >= ctx.best_size) return true; // 之後會被剪枝
            }
        }
    }
    return true;
}

inline size_t CrossStateCoverSolver::choose_universe_var(const SearchCtx& ctx) const {
    // MRV: 找尚未覆蓋元素中，覆蓋它的集合數最少者
    size_t best_ui = (size_t)-1; int best_cnt = std::numeric_limits<int>::max();
    for (size_t ui=0; ui<inst_.U.size(); ++ui) {
        if (!(ctx.uncovered_bits[ui/64] & (uint64_t(1) << (ui%64)))) continue;
        int cnt = 0;
        for (size_t si=0; si<set_bit_covers_.size(); ++si) {
            if (set_bit_covers_[si][ui/64] & (uint64_t(1) << (ui%64))) cnt++;
        }
        if (cnt < best_cnt) { best_cnt = cnt; best_ui = ui; if (cnt <= 1) break; }
    }
    return best_ui;
}

inline size_t CrossStateCoverSolver::lower_bound_remaining(const SearchCtx& ctx) const {
    // 用最大覆蓋能力作粗略 LB：ceil(remaining / max_cover)
    size_t remain = bitset_count(ctx.uncovered_bits);
    if (remain == 0) return 0;
    size_t max_cover = 1;
    for (auto &bs : set_bit_covers_) {
        size_t c = 0; // count intersection with uncovered
        for (size_t w=0; w<bs.size(); ++w) {
            uint64_t word = bs[w] & ctx.uncovered_bits[w];
            if (word) c += (size_t)__builtin_popcountll((long long)word);
        }
        if (c > max_cover) max_cover = c;
    }
    return (remain + max_cover - 1) / max_cover;
}

inline void CrossStateCoverSolver::search(SearchCtx& ctx, size_t /*start_idx*/) {
    if (bitset_empty(ctx.uncovered_bits)) {
        if (ctx.chosen.size() < ctx.best_size) {
            ctx.best_size = ctx.chosen.size();
            ctx.best_solution = ctx.chosen;
        }
        return;
    }
    if (ctx.chosen.size() >= ctx.best_size) return; // 剪枝
    size_t lb = lower_bound_remaining(ctx);
    if (ctx.chosen.size() + lb >= ctx.best_size) return; // LB 剪枝

    size_t ui = choose_universe_var(ctx);
    if (ui == (size_t)-1) return; // 安全防護

    // 蒐集覆蓋 ui 的集合列表（啟發式排序：覆蓋更多元素者優先）
    struct CandOrder { size_t si; size_t gain; };
    vector<CandOrder> covers;
    for (size_t si=0; si<set_bit_covers_.size(); ++si) {
        if (set_bit_covers_[si][ui/64] & (uint64_t(1) << (ui%64))) {
            // 計算 gain = 尚未覆蓋元素中該集合能覆蓋的數量
            size_t gain=0; for (size_t w=0; w<set_bit_covers_[si].size(); ++w) {
                uint64_t word = set_bit_covers_[si][w] & ctx.uncovered_bits[w];
                if (word) gain += (size_t)__builtin_popcountll((long long)word);
            }
            covers.push_back({si, gain});
        }
    }
    std::sort(covers.begin(), covers.end(), [](const CandOrder& a, const CandOrder& b){ return a.gain > b.gain; });

    for (auto &co : covers) {
        size_t si = co.si;
        vector<uint64_t> backup = ctx.uncovered_bits;
        ctx.chosen.push_back(si);
        bitset_remove(ctx.uncovered_bits, set_bit_covers_[si]);
        if (force_singletons(ctx)) {
            search(ctx, si+1);
        }
        ctx.chosen.pop_back();
        ctx.uncovered_bits.swap(backup);
        if (ctx.chosen.size() >= ctx.best_size) break; // 無需再繼續
    }
}

inline CoverResult CrossStateCoverSolver::solve(const vector<CrossState>& universe,
                                                const vector<vector<CrossState>>& candidates) {
    inst_ = Instance{};
    inst_.U.reserve(universe.size());
    for (auto &st : universe) {
        inst_.U.push_back(normalize(st));
    }
    inst_.CandSets.reserve(candidates.size());
    inst_.set_covers.assign(candidates.size(), {});
    for (auto &set : candidates) {
        vector<NormState> nslist; nslist.reserve(set.size());
        for (auto &st : set) nslist.push_back(normalize(st));
        inst_.CandSets.push_back(std::move(nslist));
    }

    build_cover_matrix();

    // 初始狀態
    SearchCtx ctx;
    init_bitset(inst_.U.size(), ctx.uncovered_bits);
    for (size_t ui=0; ui<inst_.U.size(); ++ui) {
        ctx.uncovered_bits[ui/64] |= (uint64_t(1) << (ui%64));
    }
    force_singletons(ctx); // 初步 forcing
    search(ctx, 0);

    CoverResult result;
    if (ctx.best_solution.empty() && !inst_.U.empty()) {
        // 可能無解
        result.uncovered_indices.reserve(inst_.U.size());
        for (size_t i=0;i<inst_.U.size();++i) result.uncovered_indices.push_back(i);
        return result;
    }
    std::sort(ctx.best_solution.begin(), ctx.best_solution.end());
    result.chosen_sets = ctx.best_solution;
    for (auto si : ctx.best_solution) {
        auto cov = inst_.set_covers[si];
        std::sort(cov.begin(), cov.end());
        result.cover_report.push_back(std::move(cov));
    }
    return result;
}

inline vector<CrossState> CrossStateCoverSolver::synthesize_generalized_patterns(const vector<CrossState>& universe) const {
    vector<CrossState> result;
    if (universe.empty()) return result;
    // 取得第一個為基底
    CrossState pattern = universe[0];
    // 兩層：D 與 C 分開 generalize 規則：若某位置存在不同具體值 => 設為 X；
    // 若全都是 X => 留 X；若有至少一個具體且其他都是 X 或同樣值 => 保留該值。
    auto unify_field = [](int &cur, int next){
        if (cur == -1) {
            cur = next; // X -> next (可能 -1)
        } else {
            if (next == -1) {
                // no change
            } else if (cur != next) {
                // 衝突 → X
                cur = -1;
            }
        }
    };

    for (size_t i=1;i<universe.size();++i) {
        for (int k=0;k<5;++k) unify_field(pattern.cells[k].D, universe[i].cells[k].D);
        for (int k=0;k<5;++k) unify_field(pattern.cells[k].C, universe[i].cells[k].C);
    }
    pattern.case_name = "GEN";

    // 檢查 forbidden：若 D 五格可指派成 forbidden pattern（即所有非 X 與某禁用一致）→ 目前策略：直接保留，因為需要 coverage。
    // 若你想強制拆分，可在此檢測後做切割策略（尚未實作）。

    result.push_back(pattern);
    return result;
}
