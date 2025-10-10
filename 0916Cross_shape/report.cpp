
// ---------- 小工具：Val → 三值位、六欄 base-3 編碼 ----------
inline uint8_t to3_tp(Val v) { return (v==Val::Zero)?0u : (v==Val::One)?1u : 2u; } // TP: X→2
inline uint8_t to3_op(Val v) { return (v==Val::Zero)?0u : (v==Val::One)?1u : 2u; } // OP: -1/X→2

inline int encode_key6(uint8_t D1, uint8_t D2, uint8_t D3,
                       uint8_t C0, uint8_t C2, uint8_t C4) { //改變: CSS作為輸入
    const uint8_t v[6] = {D1,D2,D3,C0,C2,C4};
    int idx = 0;
    for (int i=0;i<6;++i) idx = idx*3 + v[i];
    return idx; // 0..728
}

inline void decode_key6(int idx, uint8_t out[6]) {
    for (int i=5;i>=0;--i){ out[i]=idx%3; idx/=3; }
}

// ---------- OP 展開表 ----------
struct OpTable {
    std::vector<int>       elem_of;   // [op_id] -> E_i
    std::vector<int>       j_of;      // [op_id] -> j (0-based; 報表 +1)
    std::vector<Op>        op;        // [op_id]
    std::vector<AddrOrder> order;     // [op_id] (from element)
    std::vector<int>       pre_key;   // [op_id] ∈ [0..728]
    // detector 快速跳點：
    std::vector<int> next_id;   // '#': 下一個實際 op；末端=-1
    std::vector<int> head_same; // '^': 本元素第一個 op
    std::vector<int> head_next; // ';': 下一元素第一個 op；無=-1
    int getOpSize() const { return (int)op.size(); }
};

struct CoverLUT {
    bool ok[729][729] = {{false}};
    std::array<std::vector<int>, 729> compatStoreKeys; // compatStoreKeys[input] -> store_keys
    void build();
};

inline void CoverLUT::build() {
    // 逐欄配對：store=0→input=0；store=1→input=1；store=2→input=0/1/2
    for (int store=0; store<729; ++store) {
        uint8_t s[6]; decode_key6(store, s);
        for (int input=0; input<729; ++input) {
            uint8_t in[6]; decode_key6(input, in);
            bool pass=true;
            for (int k=0;k<6;++k){
                if (s[k]==0 && in[k]!=0) { pass=false; break; }
                if (s[k]==1 && in[k]!=1) { pass=false; break; }
                // s[k]==2 → 任意 0/1/2 皆可
            }
            ok[store][input] = pass;
        }
    }
    for (int input=0; input<729; ++input) {
        auto& vec = compatStoreKeys[input];
        vec.clear(); vec.reserve(729);
        for (int store=0; store<729; ++store) if (ok[store][input]) vec.push_back(store);
    }
}

// ---------- Builder：把 MarchTest 平展為 OpTable ----------
class OpTableBuilder {
public:
    OpTable build(const MarchTest& mt);

private:
    // helpers
    static bool is_write(const Op& op);
    static bool is_computeC(const Op& op);

    // 1) 平展：填 elem_of/j_of/op/order，並產生每個 element 的 head id（空 element 設為 -1）
    void flatten(const MarchTest& mt, OpTable& t, std::vector<int>& head_of_elem) const;

    // 2) 建 #/^/; 三種鄰接跳點
    void build_neighbors(const MarchTest& mt, const std::vector<int>& head_of_elem, OpTable& t) const;

    // 3) 計算每個 element 的 D2 哨兵
    void compute_D2_sentinels(const MarchTest& mt, std::vector<Val>& D2_sentinel) const;

    // 4) 計算每個 element 的 C 哨兵（同列不變；跨列平移暫不實作）
    //    若該 element 有 ComputeAnd，取其「最後一個」(T,M,B) 作為 (C0,C2,C4)；
    //    否則 (C0=C2=C4= 上一 element 的 C2；若無上一 element → X)。
    void compute_C_sentinels(const MarchTest& mt, std::vector<std::array<Val,3>>& C_sentinel) const;

    // 5) 逐 OP 推導 pre_key（D 與 C 合成 729 key），不做跨列平移
    void derive_pre_keys_same_row_only(const MarchTest& mt,
                                       const std::vector<int>& head_of_elem,
                                       const std::vector<Val>& D2_sentinel,
                                       const std::vector<std::array<Val,3>>& C_sentinel,
                                       OpTable& t) const;
};

// ====== 實作 ======

inline bool OpTableBuilder::is_write(const Op& op) {
    return op.kind == OpKind::Write;
}
inline bool OpTableBuilder::is_computeC(const Op& op) {
    return op.kind == OpKind::ComputeAnd;
}

inline void OpTableBuilder::flatten(const MarchTest& mt, OpTable& t, std::vector<int>& head_of_elem) const {
    int total = 0;
    for (const auto& e : mt.elements) total += (int)e.ops.size();
    t.elem_of.resize(total); t.j_of.resize(total); t.op.resize(total);
    t.order.resize(total);   t.pre_key.resize(total);
    t.next_id.resize(total); t.head_same.resize(total); t.head_next.resize(total);

    head_of_elem.assign((int)mt.elements.size(), -1);

    int id = 0;
    for (int i = 0; i < (int)mt.elements.size(); ++i) {
        const auto& E = mt.elements[i];
        if (E.ops.empty()) continue;
        head_of_elem[i] = id;
        for (int j = 0; j < (int)E.ops.size(); ++j, ++id) {
            t.elem_of[id] = i;
            t.j_of[id]    = j;
            t.op[id]      = E.ops[j];
            t.order[id]   = E.order;
        }
    }
}

inline void OpTableBuilder::build_neighbors(const MarchTest& mt, const std::vector<int>& head_of_elem, OpTable& t) const {
    const int N = (int)t.op.size();
    for (int k = 0; k < N; ++k) t.next_id[k] = (k+1 < N) ? (k+1) : -1;

    // head_same/head_next：只針對非空 element 填值
    for (int i = 0; i < (int)mt.elements.size(); ++i) {
        int head_i = head_of_elem[i];
        if (head_i < 0) continue;

        // 尋找下一個非空 element 的 head
        int head_next = -1;
        for (int j = i+1; j < (int)mt.elements.size(); ++j) {
            if (head_of_elem[j] >= 0) { head_next = head_of_elem[j]; break; }
        }
        const int m = (int)mt.elements[i].ops.size();
        for (int j = 0; j < m; ++j) {
            int id0 = head_i + j;
            t.head_same[id0] = head_i;
            t.head_next[id0] = head_next;
        }
    }
}

inline void OpTableBuilder::compute_D2_sentinels(const MarchTest& mt, std::vector<Val>& D2_sentinel) const {
    const int E = (int)mt.elements.size();
    D2_sentinel.assign(E+1, Val::X);  // D2_sentinel[0] = X
    for (int i = 0; i < E; ++i) {
        Val D2 = D2_sentinel[i];      // 開頭承前
        for (const auto& op : mt.elements[i].ops) { // element 內只需追末值
            if (op.kind == OpKind::Write) D2 = op.value;
        }
        D2_sentinel[i+1] = D2;         // 下一 element 的起點
    }
}

inline void OpTableBuilder::compute_C_sentinels(const MarchTest& mt, std::vector<std::array<Val,3>>& C_sentinel) const {
    const int E = (int)mt.elements.size();
    C_sentinel.assign(E+1, {Val::X, Val::X, Val::X}); // [C0,C2,C4]，初值全 X
    for (int i = 0; i < E; ++i) {
        const auto& El = mt.elements[i];
        int lastC = -1;
        for (int j = 0; j < (int)El.ops.size(); ++j) {
            if (is_computeC(El.ops[j])) lastC = j;
        }
        if (lastC >= 0) {
            const auto& cop = El.ops[lastC];
            C_sentinel[i] = { cop.C_T, cop.C_M, cop.C_B }; // C0=C_T, C2=C_M, C4=C_B
        } else {
            Val prevC2 = (i > 0) ? C_sentinel[i-1][1] : Val::X;
            C_sentinel[i] = { prevC2, prevC2, prevC2 };    // 無 C → 全用前一 element 的 C2
        }
        // 延續（你若要 reset 可改這行）
        C_sentinel[i+1] = C_sentinel[i];
    }
}

inline void OpTableBuilder::derive_pre_keys_same_row_only(const MarchTest& mt,
                                                          const std::vector<int>& head_of_elem,
                                                          const std::vector<Val>& D2_sentinel,
                                                          const std::vector<std::array<Val,3>>& C_sentinel,
                                                          OpTable& t) const {
    auto v3 = [](Val v)->uint8_t { return (v==Val::Zero)?0u : (v==Val::One)?1u : 2u; };

    for (int i = 0; i < (int)mt.elements.size(); ++i) {
        int base = head_of_elem[i];
        if (base < 0) continue;                  // 空 element 跳過
        const auto& El = mt.elements[i];
        Val curD2 = D2_sentinel[i];              // 當前 D2（pre 用）
        Val curC0 = C_sentinel[i][0],            // 當前 C 三元（同列不變）
            curC2 = C_sentinel[i][1],
            curC4 = C_sentinel[i][2];

        for (int j = 0; j < (int)El.ops.size(); ++j) {
            int id0 = base + j;
            const auto& op = El.ops[j];

            // ---- D1/D2/D3（依規則）----
            Val D0, D1 = Val::X, D3, D4 = Val::X;
            if (El.order == AddrOrder::Up) {
                D1 = D2_sentinel[i];
                D3 = (i > 0 ? D2_sentinel[i-1] : Val::X);
            } else if (El.order == AddrOrder::Down) {
                D1 = (i > 0 ? D2_sentinel[i-1] : Val::X);
                D3 = D2_sentinel[i];
            } else { // Any
                D1 = D2_sentinel[i];
                D3 = (i > 0 ? D2_sentinel[i-1] : Val::X);
            }
            D0 = D1;
            D4 = D3;
            Val preD2 = curD2;

            // ---- C0/C2/C4（同列不變；跨列平移 TODO）----
            Val preC0 = curC0, preC2 = curC2, preC4 = curC4;

            // ---- 編碼成 0..728 key ----
            t.pre_key[id0] = encode_key6(
                v3(D1), v3(preD2), v3(D3),
                v3(preC0), v3(preC2), v3(preC4)
            );

            // ---- 更新「下一個 pre」狀態（寫/算在下一步生效）----
            if (is_write(op)) curD2 = op.value;
            if (is_computeC(op)) { curC0 = op.C_T; curC2 = op.C_M; curC4 = op.C_B; }

            // TODO(跨列平移)：若未來要支援 row 切換，請在此處（下一 pre 之前）平移 (curC0,curC2,curC4)。
        }
    }
}

inline OpTable OpTableBuilder::build(const MarchTest& mt) {
    OpTable t;
    std::vector<int> head_of_elem;
    std::vector<Val> D2_sentinel;
    std::vector<std::array<Val,3>> C_sentinel;

    flatten(mt, t, head_of_elem);
    build_neighbors(mt, head_of_elem, t);
    compute_D2_sentinels(mt, D2_sentinel);
    compute_C_sentinels(mt, C_sentinel);
    derive_pre_keys_same_row_only(mt, head_of_elem, D2_sentinel, C_sentinel, t);

    return t;
}

// ---------- TP 索引：每個 TP 只進「一個桶」 ----------
struct TPMeta {
    size_t tp_gid{};            // 全域 TP id（連號）
    size_t fault_idx{};         // 所屬 fault
    size_t tp_index_in_fault{}; // fault 內序號
    OrientationGroup group{OrientationGroup::Single}; // A_LT_V / A_GT_V / Single
    const TestPrimitive* tp{nullptr}; // 指回原 TP
    int store_key{};            // 0..728
};

struct FaultMeta {
    std::string fault_id;
    CellScope   scope{CellScope::SingleCell};
};

struct StoreBuckets {
    std::array<std::vector<size_t>, 729> tps; // [store_key] -> tp_gid[]
};

class TPIndexer {
public:
    void build(const std::vector<Fault>& faults,
               const std::vector<std::vector<TestPrimitive>>& all_fault_tps);
    const StoreBuckets& buckets() const { return buckets_; }
    const std::vector<TPMeta>& metas() const { return metas_; }
    const std::vector<FaultMeta>& faults() const { return faults_; }
private:
    StoreBuckets buckets_;
    std::vector<TPMeta> metas_;
    std::vector<FaultMeta> faults_;
};

inline void TPIndexer::build(const std::vector<Fault>& faults,
            const std::vector<std::vector<TestPrimitive>>& all_fault_tps) {
    metas_.clear(); faults_.clear();
    std::fill(buckets_.tps.begin(), buckets_.tps.end(), std::vector<size_t>{});
    faults_.reserve(faults.size());
    for (auto& f: faults) faults_.push_back(FaultMeta{f.fault_id, f.cell_scope});

    size_t gid=0;
    for (size_t fi=0; fi<all_fault_tps.size(); ++fi){
        const auto& vec = all_fault_tps[fi];
        for (size_t ti=0; ti<vec.size(); ++ti){
            const auto& tp = vec[ti];
            uint8_t D1 = to3_tp(tp.state.A1.D);
            uint8_t D2 = to3_tp(tp.state.A2_CAS.D);
            uint8_t D3 = to3_tp(tp.state.A3.D);
            uint8_t C0 = to3_tp(tp.state.A0.C);
            uint8_t C2 = to3_tp(tp.state.A2_CAS.C);
            uint8_t C4 = to3_tp(tp.state.A4.C);
            int store_key = encode_key6(D1,D2,D3,C0,C2,C4);

            TPMeta m;
            m.tp_gid = gid;
            m.fault_idx = fi;
            m.tp_index_in_fault = ti;
            m.group = tp.group;
            m.tp = &tp;
            m.store_key = store_key;
            metas_.push_back(m);
            buckets_.tps[store_key].push_back(gid);
            ++gid;
        }
    }
}

// ---------- State cover（用 LUT 聯集桶；用 epoch 去重） ----------
class StateCoverEngine {
public:
    StateCoverEngine(const CoverLUT& lut, const StoreBuckets& sb,
                     const std::vector<TPMeta>& metas)
      : lut_(lut), sb_(sb), metas_(metas) {}
    void cover_one_input(int input_key, std::vector<size_t>& out, std::vector<int>& seen_epoch, int epoch) const;
private:
    const CoverLUT& lut_;
    const StoreBuckets& sb_;
    const std::vector<TPMeta>& metas_;
};

void StateCoverEngine::cover_one_input(int input_key, std::vector<size_t>& out, std::vector<int>& seen_epoch, int epoch) const {
    out.clear();
    const auto& ks = lut_.compatStoreKeys[input_key];
    for (int sk : ks) {
        const auto& v = sb_.tps[sk];
        for (size_t gid : v) {
            if (gid >= seen_epoch.size()) continue; // 以防萬一
            if (seen_epoch[gid] == epoch) continue;
            seen_epoch[gid] = epoch;
            out.push_back(gid);
        }
    }
}

// ---------- Sens cover（同元素 + 連續） ----------
class SensEngine {
public:
    // 成功回傳完成的 op_id；失敗回 -1
    int advance(const OpTable& tbl, int op_id, const TestPrimitive& tp) const;
private:
    static bool op_match(const Op& a, const Op& b);
};

int SensEngine::advance(const OpTable& tbl, int op_id, const TestPrimitive& tp) const {
    const auto& seq = tp.ops_before_detect;
    if (seq.empty()) return op_id; // 無 sensitizer 序列
    int elem = tbl.elem_of[op_id];
    for (int k=0;k<(int)seq.size();++k){
        int id = op_id + 1 + k;
        if (id >= tbl.getOpSize()) return -1;
        if (tbl.elem_of[id] != elem) return -1; // 不能跨元素
        if (!op_match(seq[k], tbl.op[id])) return -1;
    }
    return op_id + (int)seq.size();
}

bool SensEngine::op_match(const Op& a, const Op& b) {
    if (a.kind != b.kind) return false;
    switch (a.kind) {
    case OpKind::Write:
    case OpKind::Read:
        return a.value == b.value; // 必須值一致
    case OpKind::ComputeAnd:
        // X 視為 don’t-care
        if (a.C_T!=Val::X && a.C_T!=b.C_T) return false;
        if (a.C_M!=Val::X && a.C_M!=b.C_M) return false;
        if (a.C_B!=Val::X && a.C_B!=b.C_B) return false;
        return true;
    }
    return false;
}

// ---------- Detect cover（必要性 → 方位 → 符號） ----------
class DetectEngine {
public:
    // 成功回 det_id，否則 -1
    int detect(const OpTable& tbl, int sens_id, const TestPrimitive& tp) const;
private:
    static bool detector_match(const Detector& d, const Op& op);
};

int DetectEngine::detect(const OpTable& tbl, int sens_id, const TestPrimitive& tp) const {
    // 1) 無需偵測
    if (!tp.detectionNecessary) return sens_id;

    // 2) 位置符號
    int det_id = -1;
    switch (tp.detector.pos) {
        case PositionMark::Adjacent:       det_id = tbl.next_id[sens_id]; break;      // '#'
        case PositionMark::SameElementHead:det_id = tbl.head_same[sens_id]; break;    // '^'
        case PositionMark::NextElementHead:det_id = tbl.head_next[sens_id]; break;    // ';'
    }
    if (det_id < 0) return -1;

    // 3) 偵測類型
    if (!detector_match(tp.detector, tbl.op[det_id])) return -1;

    return det_id;
}

bool DetectEngine::detector_match(const Detector& d, const Op& op) {
    switch (d.kind){
        case DetectKind::Read:
            return (op.kind==OpKind::Read) && (op.value==d.read_expect);
        case DetectKind::ComputeAnd:
            if (op.kind!=OpKind::ComputeAnd) return false;
            if (d.C_T!=Val::X && d.C_T!=op.C_T) return false;
            if (d.C_M!=Val::X && d.C_M!=op.C_M) return false;
            if (d.C_B!=Val::X && d.C_B!=op.C_B) return false;
            return true;
    }
    return false;
}

// ---------- 三階段結果與 Reporter ----------
struct SensDetHit {
    size_t tp_gid{};
    int    sens_id{-1};
    int    det_id{-1};
};

struct CoverLists {
    std::vector<std::vector<size_t>>    state_cover; // [op] -> tp_gid[]
    std::vector<std::vector<size_t>>    sens_cover;  // [op] -> tp_gid[]
    std::vector<std::vector<SensDetHit>>det_cover;   // [op] -> hits
};

struct FaultCoverageDetail {
    std::string fault_id;
    double coverage{0.0}; // 0 / 0.5 / 1
    std::vector<SensDetHit> hits;
};
struct SimulationResult {
    double total_coverage{0.0};
    std::vector<FaultCoverageDetail> per_fault;
};

class Reporter {
public:
    SimulationResult build(const OpTable& tbl, const TPIndexer& idx, const CoverLists& cov) const;
};

SimulationResult Reporter::build(const OpTable& tbl, const TPIndexer& idx, const CoverLists& cov) const {
    SimulationResult R;
    const auto& metas = idx.metas();
    const auto& faults= idx.faults();
    R.per_fault.resize(faults.size());
    for (size_t i=0;i<faults.size();++i){
        R.per_fault[i].fault_id = faults[i].fault_id;
        R.per_fault[i].coverage = 0.0;
    }
    // 蒐集命中與方向
    std::vector<bool> has_any(faults.size(), false);
    std::vector<bool> has_lt (faults.size(), false);
    std::vector<bool> has_gt (faults.size(), false);

    for (int did=0; did<(int)cov.det_cover.size(); ++did){
        for (const auto& h: cov.det_cover[did]){
            const auto& m = metas[h.tp_gid];
            auto& fd = R.per_fault[m.fault_idx];
            fd.hits.push_back(h);
            has_any[m.fault_idx] = true;
            if (m.group == OrientationGroup::A_LT_V) has_lt[m.fault_idx] = true;
            if (m.group == OrientationGroup::A_GT_V) has_gt[m.fault_idx] = true;
        }
    }
    // 計 coverage
    double sum = 0.0;
    for (size_t fi=0; fi<faults.size(); ++fi){
        double c = 0.0;
        if (faults[fi].scope == CellScope::SingleCell) {
            c = has_any[fi] ? 1.0 : 0.0;
        } else {
            bool lt = has_lt[fi], gt = has_gt[fi];
            c = (lt && gt) ? 1.0 : (lt || gt) ? 0.5 : 0.0;
        }
        R.per_fault[fi].coverage = c;
        sum += c;
    }
    R.total_coverage = faults.empty()? 0.0 : (sum / faults.size());
    return R;
}

// ---------- 外觀：FaultSimulator（組裝整條流水） ----------
class FaultSimulator {
public:
    SimulationResult run(const MarchTest& mt,
                         const std::vector<Fault>& faults,
                         const std::vector<std::vector<TestPrimitive>>& all_fault_tps);
};

SimulationResult FaultSimulator::run(const MarchTest& mt,
                                      const std::vector<Fault>& faults,
                                      const std::vector<std::vector<TestPrimitive>>& all_fault_tps) {
    // 1) LUT
    CoverLUT lut; lut.build();
    // 2) OP 展開
    OpTableBuilder bld; OpTable tbl = bld.build(mt);
    // 3) TP 索引
    TPIndexer tpidx; tpidx.build(faults, all_fault_tps);
    // 4) 三階段引擎
    StateCoverEngine scover(lut, tpidx.buckets(), tpidx.metas());
    SensEngine       sengine;
    DetectEngine     dengine;

    CoverLists cov;
    cov.state_cover.resize(tbl.getOpSize());
    cov.sens_cover.resize(tbl.getOpSize());
    cov.det_cover.resize(tbl.getOpSize());

    // epoch 去重用
    std::vector<int> seen_epoch(tpidx.metas().size(), -1);
    int epoch=0;

    // (A) State cover：每個 op 一次
    std::vector<size_t> scratch;
    for (int id=0; id<tbl.getOpSize(); ++id){
        ++epoch;
        scover.cover_one_input(tbl.pre_key[id], cov.state_cover[id], seen_epoch, epoch);
    }

    // (B) Sens cover：同元素連續匹配；把完成點掛到完成的 op 上
    for (int id=0; id<tbl.getOpSize(); ++id){
        for (size_t gid : cov.state_cover[id]){
            const auto& tp = *tpidx.metas()[gid].tp;
            int sid = sengine.advance(tbl, id, tp);
            if (sid>=0) cov.sens_cover[sid].push_back(gid);
        }
    }

    // (C) Detect cover：必要性→方位→符號
    for (int sid=0; sid<tbl.getOpSize(); ++sid){
        for (size_t gid : cov.sens_cover[sid]){
            const auto& tp = *tpidx.metas()[gid].tp;
            int did = dengine.detect(tbl, sid, tp);
            if (did>=0){
                cov.det_cover[did].push_back(SensDetHit{gid, sid, did});
            }
        }
    }

    // (D) 報表
    Reporter rep;
    return rep.build(tbl, tpidx, cov);
}
