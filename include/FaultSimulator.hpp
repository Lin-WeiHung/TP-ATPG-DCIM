// Interface + inline implementation in one header (like FaultsJsonParser).
// Precise std usings for clarity and limited exposure.
#pragma once

#include <string>
#include <vector>
#include <fstream>
#include <optional>
#include <array>
#include <unordered_map>
#include <unordered_set>

#include <nlohmann/json.hpp>
#include "FpParserAndTpGen.hpp" // reuse Op/Val/OpKind

// ---- precise using ----
using std::string;
using std::vector;
using std::ifstream;
using std::runtime_error;
using std::size_t;
using std::optional;
using std::array;
using std::uint8_t;
using std::unordered_map;
using std::pair;
using std::to_string;
using std::max;
using std::unordered_set;

struct RawMarchTest {
    string name;
    string pattern;
};

class MarchTestJsonParser {
public:
    vector<RawMarchTest> parse_file(const string& path);
};

inline vector<RawMarchTest> MarchTestJsonParser::parse_file(const string& path) {
    ifstream ifs(path);
    if (!ifs) throw runtime_error("Cannot open file: " + path);
    nlohmann::json j; ifs >> j;
    if (!j.is_array()) throw runtime_error("JSON root must be an array: " + path);
    vector<RawMarchTest> out;
    for (const auto& item : j) {
        string name = item.at("March_test").get<string>();
        string pattern = item.at("Pattern").get<string>();
        out.push_back({name, pattern});
    }
    return out;
}

// Address traversal order for a March element
enum class AddrOrder { Up, Down, Any };

// One March element: an order + a list of operations
struct MarchElement {
    AddrOrder order{AddrOrder::Any};
    vector<Op> ops; // operations inside this element
};

struct MarchTest {
    string name;
    vector<MarchElement> elements;
};

class MarchTestNormalizer {
public:
    // Parse a pattern string into a list of MarchElements
    MarchTest normalize(const RawMarchTest& raw);
private:
    // helpers used by normalize()
    string remove_all_space(const string& s);
    // split pattern by ';' into element tokens
    vector<string> split_elements(const string& pattern);
    // extract address/order info from an element token (modifies token to remove the address part)
    // returns AddrOrder::Any if no explicit order found
    AddrOrder extract_addr_order(string& element_token);
    // remove surrounding parentheses from a token, if present
    string remove_parentheses(const string& token);
    // split an element (after addr removed and parens stripped) by ',' into operation tokens
    vector<string> split_ops(const string& element_body);
    // parse a single operation token into an Op (may throw on invalid token)
    Op parse_op_token(const string& op_token);
};

inline MarchTest MarchTestNormalizer::normalize(const RawMarchTest& raw) {
    MarchTest out;
    out.name = raw.name;
    string pattern = remove_all_space(raw.pattern);
    auto element_tokens = split_elements(pattern);
    for (auto& etok : element_tokens) {
        if (etok.empty()) continue;
        AddrOrder order = extract_addr_order(etok);
        etok = remove_parentheses(etok);
        auto op_tokens = split_ops(etok);
        MarchElement me;
        me.order = order;
        for (const auto& ot : op_tokens) {
            Op op = parse_op_token(ot);
            me.ops.push_back(op);
        }
        out.elements.push_back(me);
    }
    return out;
}

inline string MarchTestNormalizer::remove_all_space(const string& s) {
    string out;
    for (char c : s) {
        if (!isspace(static_cast<unsigned char>(c))) out += c;
    }
    return out;
}

inline vector<string> MarchTestNormalizer::split_elements(const string& pattern) {
    vector<string> out;
    string cur; 
    cur.reserve(pattern.size());
    for (char c : pattern) {
        if (c == ';') {
            out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

inline AddrOrder MarchTestNormalizer::extract_addr_order(string& element_token) {
    if (element_token.size() >= 1) {
        char c = element_token[0];
        element_token = element_token.substr(1);
        if (c == 'b' || c == 'B') {
            return AddrOrder::Any;
        } else if (c == 'a' || c == 'A') {
            return AddrOrder::Up;
        } else if (c == 'd' || c == 'D') {
            return AddrOrder::Down;
        }
    }
    throw runtime_error("extract_addr_order: invalid or missing address order in element token: '" + element_token + "'");
    return AddrOrder::Any; // default
}

inline string MarchTestNormalizer::remove_parentheses(const string& token) {
    if (token.size() >= 2 && token.front() == '(' && token.back() == ')') {
        return token.substr(1, token.size() - 2);
    }
    return token;
}

inline vector<string> MarchTestNormalizer::split_ops(const string& element_body) {
    vector<string> out;
    string cur; 
    cur.reserve(element_body.size());
    for (char c : element_body) {
        if (c == ',') {
            out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

inline Op MarchTestNormalizer::parse_op_token(const string& op_token) {
    if (op_token.empty()) throw runtime_error("parse_op_token: empty operation token");
    Op op;
    if (op_token[0] == 'R') {
        // Read operation
        if (op_token.size() != 2 || (op_token[1] != '0' && op_token[1] != '1')) {
            throw runtime_error("parse_op_token: invalid Read operation token: '" + op_token + "'");
        }
        op.kind = OpKind::Read;
        op.value = (op_token[1] == '0') ? Val::Zero : Val::One;
    } else if (op_token[0] == 'W') {
        // Write operation
        if (op_token.size() != 2 || (op_token[1] != '0' && op_token[1] != '1')) {
            throw runtime_error("parse_op_token: invalid Write operation token: '" + op_token + "'");
        }
        op.kind = OpKind::Write;
        op.value = (op_token[1] == '0') ? Val::Zero : Val::One;
    } else if (op_token[0] == 'C') {
        // Compute operation
        if (op_token.size() < 9) {
            throw runtime_error("parse_op_token: invalid Compute operation token: '" + op_token + "'");
        }
        op.kind = OpKind::ComputeAnd;
        op.C_T = (op_token[2] == '0') ? Val::Zero : Val::One;
        op.C_M = (op_token[5] == '0') ? Val::Zero : Val::One;
        op.C_B = (op_token[8] == '0') ? Val::Zero : Val::One;
    } else {
        throw runtime_error("parse_op_token: unknown operation type in token: '" + op_token + "'");
    }
    return op;
}

const int KEY_BIT = 6;
const int CSS_EXPANDED_NUM = 729; // 3^6

const int KEY_CARRY = 3;
inline size_t encode_to_key(const CrossState& input) {
    size_t key = 0;
    auto valto3 = [](Val v){ return (v==Val::Zero)?0u : (v==Val::One)?1u : 2u; };
    key = key * KEY_CARRY + valto3(input.A1.D);
    key = key * KEY_CARRY + valto3(input.A2_CAS.D);
    key = key * KEY_CARRY + valto3(input.A3.D);
    key = key * KEY_CARRY + valto3(input.A0.C);
    key = key * KEY_CARRY + valto3(input.A2_CAS.C);
    key = key * KEY_CARRY + valto3(input.A4.C);
    return key; // 0..728
}

class CoverLUT {
public:
    CoverLUT();
    const vector<size_t>& get_compatible_tp_keys(const CrossState& op_css) const;
    const vector<size_t>& get_compatible_tp_keys_by_key(size_t op_css_key) const { return compatible_tp_keys[op_css_key]; }
private:
    vector<size_t> compatible_tp_keys[CSS_EXPANDED_NUM]; // for each store key, which input keys are compatible
};

inline CoverLUT::CoverLUT() {
    size_t decoded_key[CSS_EXPANDED_NUM][KEY_BIT];
    for (int key=0; key<CSS_EXPANDED_NUM; ++key) {
        int k=key;
        for (int i=KEY_BIT-1; i>=0; --i){ decoded_key[key][i]=k%3; k/=3; }
    }
    // 我們要支援：輸入 op_css_key -> 回傳所有 "tp_key"（可被覆蓋的 TP）
    for (int op_css = 0; op_css < CSS_EXPANDED_NUM; ++op_css) {
        for (int tp = 0; tp < CSS_EXPANDED_NUM; ++tp) {
            bool compatible = true;
            for (int k=0;k<KEY_BIT;++k){
                // TP 位元是 0/1 則必須相等；TP 位元 2 表 don’t care
                if (decoded_key[tp][k] != 2 && decoded_key[tp][k] != decoded_key[op_css][k]) {
                    compatible = false; break;
                }
            }
            if (compatible) {
                // 注意：索引要用 op_css，內容塞進「tp_key」
                compatible_tp_keys[op_css].push_back(tp);
            }
        }
    }
}

inline const vector<size_t>& CoverLUT::get_compatible_tp_keys(const CrossState& op_css) const {
    return compatible_tp_keys[encode_to_key(op_css)];
}

// ==============================
//  March-based Fault Simulator (OP-driven + 729x729 LUT)
//  -- append right after MarchTestNormalizer --
// ==============================

using OpId = int;  // 操作索引

struct OpContext {
    Op op; // operation
    int elem_index; // which MarchElement this op belongs to
    int index_within_elem;    // index within the element
    AddrOrder order; // address order of the element

    CrossState pre_state; // state before this op
    size_t pre_state_key{0}; // cached key for pre_state (0..728)

    OpId next_op_index{-1}; // index of next op in the same element, or -1 if last
    OpId head_same{-1}; // index of first op in the same element, or -1 if last
    OpId head_next{-1}; // index of first op in the next element, or -1 if last
};

class OpTableBuilder {
public:
    vector<OpContext> build(const MarchTest& mt);
private:
    vector<Val> D2_sentinel; // per element, D2 at start of element
    vector<array<Val,3>> C_sentinel; // per element, (C0,C2,C4) at start of element
    vector<AddrOrder> elem_orders; // cache element orders for derive stage

    // 1) 平展：填 elem_of/j_of/op/order，並產生每個 element 的 head id（空 element 設為 -1）
    void flatten(const MarchTest& mt, vector<OpContext>& opt) const;

    // 2) 建 #/^/; 三種鄰接跳點
    void build_neighbors(const MarchTest& mt, vector<OpContext>& opt) const;

    // 3) 計算每個 element 的 D2 哨兵
    void build_D2_sentinels(const MarchTest& mt);

    // 4) 計算每個 element 的 C 哨兵（同列不變；跨列平移暫不實作）
    //    若該 element 有 ComputeAnd，取其「最後一個」(T,M,B) 作為 (C0,C2,C4)；
    //    否則 (C0=C2=C4= 上一 element 的 C2；若無上一 element → X)。
    void build_C_sentinels(const MarchTest& mt);

    // 5) 逐 OP 推導 pre state, 不做跨列平移
    void derive_pre_state_in_same_row(vector<OpContext>& opt) const;
};

inline vector<OpContext> OpTableBuilder::build(const MarchTest& mt){
    vector<OpContext> opt;
    // 1) 平展
    flatten(mt, opt);
    // 2) 鄰接 (#/^/;)
    build_neighbors(mt, opt);
    // 3) D2 哨兵
    build_D2_sentinels(mt);
    // 4) C 哨兵
    build_C_sentinels(mt);
    // cache element orders
    elem_orders.clear(); elem_orders.reserve(mt.elements.size());
    for(const auto& e: mt.elements) elem_orders.push_back(e.order);
    // 5) 推導 pre state（同列）
    derive_pre_state_in_same_row(opt);
    return opt;
}

inline void OpTableBuilder::flatten(const MarchTest& mt, vector<OpContext>& opt) const {
    int totalElems = mt.elements.size();
    int total = 0;
    for (const auto& e : mt.elements) total += (int)e.ops.size();
    opt.resize(total);

    int id = 0;
    for (int i = 0; i < totalElems; ++i) {
        const auto& elem = mt.elements[i];
        if (elem.ops.empty()) continue;
        for (int j = 0; j < (int)elem.ops.size(); ++j, ++id) {
            opt[id].elem_index = i;
            opt[id].index_within_elem = j;
            opt[id].op = elem.ops[j];
            opt[id].order = elem.order;
        }
    } 
}

inline void OpTableBuilder::build_neighbors(const MarchTest& mt, vector<OpContext>& opt) const {
    int totalElems = mt.elements.size();
    int totalOps = opt.size();
    int id = 0;
    for (int i = 0; i < totalElems; ++i) {
        const auto& elem = mt.elements[i];
        if (elem.ops.empty()) continue;
        int this_head_id = id;
        int next_head_id = (this_head_id + (int)elem.ops.size() < totalOps) ? (this_head_id + (int)elem.ops.size()) : -1;
        for (int j = 0; j < (int)elem.ops.size(); ++j, ++id) {
            if (j + 1 < (int)elem.ops.size()) {
                opt[id].next_op_index = id + 1;
            } else {
                opt[id].next_op_index = -1;
            }
            opt[id].head_same = this_head_id;
            opt[id].head_next = next_head_id;
        }
    }
}

inline void OpTableBuilder::build_D2_sentinels(const MarchTest& mt) {
    const int elem_num = (int)mt.elements.size();
    D2_sentinel.assign(elem_num+1, Val::X);  // D2_sentinel[0] = X
    for (int i = 0; i < elem_num; ++i) {
        for (const auto& op : mt.elements[i].ops) { // element 內只需追末值
            if (op.kind == OpKind::Write) D2_sentinel[i] = op.value;
        }
        D2_sentinel[i+1] = D2_sentinel[i];         // 下一 element 的起點
    }
}

inline void OpTableBuilder::build_C_sentinels(const MarchTest& mt) {
    const int elem_num = (int)mt.elements.size();
    C_sentinel.assign(elem_num, array<Val,3>{Val::X, Val::X, Val::X}); // 預設全 X
    for (int i = 0; i < elem_num; ++i) {
        Val C0 = (i > 0) ? C_sentinel[i-1][1] : Val::X; // 承前 C2
        Val C2 = C0;
        Val C4 = C0;
        for (const auto& op : mt.elements[i].ops) {
            if (op.kind == OpKind::ComputeAnd) {
                C0 = op.C_T;
                C2 = op.C_M;
                C4 = op.C_B;
            }
        }
        C_sentinel[i] = {C0, C2, C4};
    }
}

inline void OpTableBuilder::derive_pre_state_in_same_row(vector<OpContext>& opt) const {
    if(opt.empty()) return;
    // 需要能反查 element 的第一個 op id：掃一次建立
    // 也需要 element 的最後一個 op id 以便在本 element 的最後寫入 D2 哨兵與 C 哨兵演進
    struct ElemRange { int first{-1}; int last{-1}; };
    int maxElem = 0; for(const auto& oc: opt) if(oc.elem_index+1>maxElem) maxElem = oc.elem_index+1;
    std::vector<ElemRange> ranges(maxElem);
    for(int i=0;i<(int)opt.size();++i){ auto e=opt[i].elem_index; if(ranges[e].first==-1) ranges[e].first=i; ranges[e].last=i; }

    // 先準備一個工作 cross state
    // 根據規則：
    // D2: element 開頭 = 上一 element 哨兵 D2 (D2_sentinel[elem_index])
    // D1: 若 element order == Up -> 本 element 哨兵 D2 (D2_sentinel[elem_index])
    //      否則 -> 上一 element 哨兵 D2 (elem_index>0? D2_sentinel[elem_index-1] : X)
    // D3: 若 element order == Down -> 本 element 哨兵 D2
    //      否則 -> 上一 element 哨兵 D2 (elem_index>0? D2_sentinel[elem_index-1] : X)
    // C0,C2,C4 element 開頭 = 該 element 哨兵 C_sentinel[elem_index]
    // 遇 Write -> 下一 OP 的 D2 變 value
    // 遇 Compute -> 下一 OP 的 C0,C2,C4 更新為該 op 的 T,M,B
    auto getD2Sent = [&](int elem)->Val { return D2_sentinel[elem]; }; // 本 element 開頭 D2
    auto getPrevD2Sent = [&](int elem)->Val { return (elem>0)? D2_sentinel[elem-1] : Val::X; }; // 上一 element 開頭 D2
    auto getCSent = [&](int elem)->array<Val,3> { return C_sentinel[elem]; }; // 本 element 開頭 (C0,C2,C4)

    // 建立每個 element 走訪
    for(int elem=0; elem<maxElem; ++elem){
        if(ranges[elem].first==-1) continue; // 空 element 已被跳過
        Val baseD2_up   = getD2Sent(elem);
        Val baseD2_prev = getPrevD2Sent(elem);
        AddrOrder ord = elem_orders[elem];
        Val d1_init = (ord==AddrOrder::Up || ord==AddrOrder::Any)? baseD2_up : baseD2_prev;
        Val d3_init = (ord==AddrOrder::Down)? baseD2_up : baseD2_prev;
        Val curD2 = baseD2_prev;
        auto cSent = getCSent(elem);
        Val c0 = (ord==AddrOrder::Down) ? cSent[0] : cSent[1]; 
        Val c2 = (ord==AddrOrder::Down) ? cSent[2] : cSent[0];
        Val c4 = (ord==AddrOrder::Down) ? cSent[1] : cSent[2];

        // element 內逐 op
        for(int id = ranges[elem].first; id <= ranges[elem].last; ++id){
            // 填 pre_state (before this op)
            opt[id].pre_state.A2_CAS.D = curD2; // D2
            opt[id].pre_state.A1.D = d1_init;   // D1
            opt[id].pre_state.A3.D = d3_init;   // D3
            opt[id].pre_state.A0.C = c0;        // C0
            opt[id].pre_state.A2_CAS.C = c2;    // C2
            opt[id].pre_state.A4.C = c4;        // C4
            opt[id].pre_state.enforceDCrule();
            // 順手計算 pre_state 的 key 並快取
            opt[id].pre_state_key = encode_to_key(opt[id].pre_state);
            // 遇此 op 之後，更新影響下一 op 的狀態
            const Op& opv = opt[id].op;
            if(opv.kind == OpKind::Write){
                curD2 = opv.value; // 下一 pre D2
                // 若 order 為 Up 或 Down 不影響 D1/D3 (它們對 element 固定)，故不改
            } else if(opv.kind == OpKind::ComputeAnd){
                c0 = opv.C_T; c2 = opv.C_M; c4 = opv.C_B; // 下一 pre C* 更新
            }
        }
    }
}

using TpGid = size_t; // Test Primitive global ID

class StateCoverEngine {
public:
    void build_tp_buckets(const vector<TestPrimitive>& tps);
    vector<TpGid> cover(size_t op_css_key);
private:
    CoverLUT lut;
    array<vector<TpGid>, CSS_EXPANDED_NUM> tp_buckets; // tp_buckets[tp_key] -> list of test pattern indices
};

inline vector<TpGid> StateCoverEngine::cover(size_t op_css_key) {
    vector<TpGid> out;
    const auto& tp_keys = lut.get_compatible_tp_keys_by_key(op_css_key);
    for (int key : tp_keys) {
        const auto& v = tp_buckets[key];
        out.insert(out.end(), v.begin(), v.end());
    }
    return out;
}

inline void StateCoverEngine::build_tp_buckets(const vector<TestPrimitive>& tps) {
    tp_buckets.fill(vector<TpGid>()); // clear
    for (size_t i = 0; i < tps.size(); ++i) {
        int tp_key = encode_to_key(tps[i].state);
        tp_buckets[tp_key].push_back(i);
    }
}

struct SensOutcome {
    enum class Status { SensAll, SensPartial, SensNone, DontNeedSens };
    Status status{Status::SensNone};
    int sens_end_op{-1}; // 最後一個成功致敏的 op 索引
    int sens_mask_at_op{-1}; // 第一個對sensitizer造成遮蔽的 op
};

class SensEngine {
public:
    // 成功回傳完成的 op_id；失敗回 -1
    SensOutcome cover(const vector<OpContext>& opt, OpId opt_begin, const TestPrimitive& tp) const;
private:
    bool op_match(const OpContext& march_test_op, const Op& tp_op) const;
};

inline SensOutcome SensEngine::cover(const vector<OpContext>& opt, OpId opt_begin, const TestPrimitive& tp) const {
    const int op_length = static_cast<int>(tp.ops_before_detect.size());
    if (op_length == 0) return {SensOutcome::Status::DontNeedSens, opt_begin, -1}; // 無 sensitizer 序列：不前進、保留 state cover 位置

    if (opt_begin < 0 || opt_begin >= static_cast<int>(opt.size())) return SensOutcome{};

    const int last = opt_begin + op_length - 1;            // 最後一個要匹配到的 op 索引
    if (last < 0 || last >= static_cast<int>(opt.size())) return SensOutcome{};

    // 必須都在同一個 element 內
    if (opt[opt_begin].elem_index != opt[last].elem_index) return SensOutcome{};

    // 逐 op 比對
    for (int i = opt_begin, j = 0; i <= last; ++i, ++j) {
        if (!op_match(opt[i], tp.ops_before_detect[j])) return SensOutcome{SensOutcome::Status::SensPartial, -1, i}; // 在 op i 被遮蔽
    }
    return SensOutcome{SensOutcome::Status::SensAll, last, -1}; // 回傳最後一個成功匹配到的 op 索引
}

inline bool SensEngine::op_match(const OpContext& march_test_op, const Op& tp_op) const {
    // march_test_op.op 與 tp_op 比對
    if (march_test_op.op.kind != tp_op.kind) return false; // 不同 kind
    if (tp_op.kind == OpKind::Read || tp_op.kind == OpKind::Write) {
        if (tp_op.value != Val::X && march_test_op.op.value != tp_op.value) return false; // 讀寫值不符 (tp X 可任意
    } 
    if (tp_op.kind == OpKind::ComputeAnd) {
        if (tp_op.C_T != Val::X && march_test_op.op.C_T != tp_op.C_T) return false;
        if (tp_op.C_M != Val::X && march_test_op.op.C_M != tp_op.C_M) return false;
        if (tp_op.C_B != Val::X && march_test_op.op.C_B != tp_op.C_B) return false;
    }
    return true;
}

struct DetectOutcome {
    enum class Status { Found, MaskedOnD, NoDetectorReachable };
    Status status{Status::NoDetectorReachable};
    int det_op{-1};
    int mask_at_op{-1}; // 第一個對 D 造成遮蔽的 op
};

class DetectEngine {
public:
    DetectOutcome cover(const vector<OpContext>& opt, OpId sens_end_id, const TestPrimitive& tp) const;
protected:
    bool detect_match(const OpContext& op, const Detector& dec) const;
    bool is_masking_on_D(const OpContext& op) const { return (op.op.kind == OpKind::Write); }
};

inline DetectOutcome DetectEngine::cover(const vector<OpContext>& opt, OpId sens_end_id, const TestPrimitive& tp) const {
    if (sens_end_id < 0 || sens_end_id >= (OpId)opt.size()) return DetectOutcome{};
    if (tp.R_has_value) return DetectOutcome{DetectOutcome::Status::Found, sens_end_id, -1}; // 特例：R有值-偵測成功

    // 1) 先把 # / ^ / ; 轉成錨點（原本定位點）
    OpId anchor = -1;
    switch (tp.detector.pos) {
        case PositionMark::Adjacent:
            // 無 sensitizer 時，偵測與 state 同一個 op；有 sensitizer 時取同 element 的下一個 op
            anchor = tp.ops_before_detect.empty() ? sens_end_id : opt[sens_end_id].next_op_index;
            break;
        case PositionMark::SameElementHead:
            anchor = opt[sens_end_id].head_same;
            break;
        case PositionMark::NextElementHead:
            anchor = opt[sens_end_id].head_next;
            break;
    }
    if (anchor < 0 || anchor >= (OpId)opt.size()) return DetectOutcome{};

    // 2) 決定是否啟用「往後掃描」模式
    //    僅當 F 在 D 面有具體值時（對應到 detector.kind==Read 且 value!=X）才啟用。
    if (!tp.F_has_value) {
        // 舊語意：只在錨點做一次比對
        return detect_match(opt[anchor], tp.detector) ? DetectOutcome{DetectOutcome::Status::Found, anchor, -1} : DetectOutcome{};
    }

    // 3) 新語意（F 有值）：自錨點起往後掃描，直到找到 detector 或被 mask
    //    這裡的「往後」是針對同一目標 cell 的操作序列（同一地址的執行序），
    //    因 opt 是 per-address/element 的攤平成序關係（參見 OpTableBuilder 的鄰接建構）。:contentReference[oaicite:3]{index=3}
    for (OpId i = anchor; i < (OpId)opt.size(); ++i) {
        // 先檢查遮罩：一旦遇到會寫 D 的操作，代表 fault effect 被洗掉 → 失敗
        if (is_masking_on_D(opt[i])) {
            return DetectOutcome{DetectOutcome::Status::MaskedOnD, -1, i}; // mask at i
        }
        // 符合 detector（例如 R0/R1）→ 成功
        if (detect_match(opt[i], tp.detector)) {
            return DetectOutcome{DetectOutcome::Status::Found, i, -1};
        }
        // 否則持續往後找下一個 op
    }
    return DetectOutcome{}; // 沒找到 detector 且未被寫 D 擋到 → 視為未偵測
}


inline bool DetectEngine::detect_match(const OpContext& op, const Detector& dec) const {
    if (op.op.kind != dec.detectOp.kind) return false; // 不同 kind
    if (dec.detectOp.kind == OpKind::Read) {
        if (dec.detectOp.value != Val::X && op.op.value != dec.detectOp.value) return false; // 讀值不符 (tp X 可任意
    } else if (dec.detectOp.kind == OpKind::ComputeAnd) {
        if (dec.detectOp.C_T != Val::X && op.op.C_T != dec.detectOp.C_T) return false;
        if (dec.detectOp.C_M != Val::X && op.op.C_M != dec.detectOp.C_M) return false;
        if (dec.detectOp.C_B != Val::X && op.op.C_B != dec.detectOp.C_B) return false;
    } else if (dec.detectOp.kind == OpKind::Write) {
        throw runtime_error("DetectEngine::detect_match: detector op kind cannot be Write");
    }
    return true;
}

struct MaskOutcome {
    TpGid tp_gid;
    enum class Status { AllMasked, PartMasked, NoMasking };
    Status status{Status::NoMasking};
    // 未來要加上partial masking的資訊
};

// ---------- 三階段結果與 Reporter ----------
struct RawCoverLists {
    vector<TpGid>       state_cover; // [op] -> tp_gid[]
    vector<TpGid>       sens_cover;  // [op] -> tp_gid[]
    vector<TpGid>       det_cover;   // [op] -> tp_gid[] (偵測命中)
    vector<MaskOutcome> masked;      // [op] -> tp_gid[] (遮蔽誰)
};

struct FaultCoverageDetail {
    string fault_id;
    // Backward-compatible overall coverage (kept equal to detect_coverage)
    double coverage{0.0}; // 0 / 0.5 / 1 (alias of detect_coverage)

    // Per-stage coverages
    double state_coverage{0.0};  // computed like detect_coverage
    double sens_coverage{0.0};   // computed like detect_coverage
    double detect_coverage{0.0}; // previously "coverage"

    // Per-stage hit collections (minimal info sufficient to compute coverage)
    vector<TpGid> state_tp_gids;  // TPs that state-covered this fault
    vector<TpGid> sens_tp_gids;   // TPs that sensitized this fault
    vector<TpGid> detect_tp_gids; // TPs that detected this fault
};
struct SimulationResult {
    double state_coverage{0.0};
    double sens_coverage{0.0};
    double detect_coverage{0.0};
    double total_coverage{0.0};
    vector<RawCoverLists> cover_lists; // per op
    vector<OpContext> op_table; // for reference
    unordered_map<string, FaultCoverageDetail> fault_detail_map; 
};

class Reporter {
public:
    void build(const vector<TestPrimitive>& tps, const vector<Fault>& faults, SimulationResult& result) const;
private:
    void build_fault_map(const vector<Fault>& faults, SimulationResult& result) const;
    void analyze_fault_detail(const vector<TestPrimitive>& tps, SimulationResult& result) const;
    void compute_fault_coverage(const vector<Fault>& faults, const vector<TestPrimitive>& tps, SimulationResult& result) const;
    void compute_final_coverage(SimulationResult& result) const;
};

inline void Reporter::build(const vector<TestPrimitive>& tps, const vector<Fault>& faults,
                                        SimulationResult& result) const {
    build_fault_map(faults, result);
    analyze_fault_detail(tps, result);
    compute_fault_coverage(faults, tps, result);
    compute_final_coverage(result);
}

inline void Reporter::build_fault_map(const vector<Fault>& faults, SimulationResult& result) const {
    for (const auto& f : faults) {
        if (result.fault_detail_map.find(f.fault_id) != result.fault_detail_map.end()) {
            throw runtime_error("Reporter::build_fault_map: duplicate fault id: " + f.fault_id);
        }
        result.fault_detail_map[f.fault_id] = FaultCoverageDetail{f.fault_id,
                                                                  /*coverage*/0.0,
                                                                  /*state_coverage*/0.0,
                                                                  /*sens_coverage*/0.0,
                                                                  /*detect_coverage*/0.0,
                                                                  /*state_tp_gids*/{},
                                                                  /*sens_tp_gids*/{},
                                                                  /*detect_tp_gids*/{}};
    }
}

inline void Reporter::analyze_fault_detail(const vector<TestPrimitive>& tps, SimulationResult& result) const {
    for (const auto& cover_list : result.cover_lists) {
        // 1) collect state phase tp gids
        for (const auto& tp_gid : cover_list.state_cover) {
            const auto& fid = tps[tp_gid].parent_fault_id;
            auto it = result.fault_detail_map.find(fid);
            if (it != result.fault_detail_map.end()) {
                it->second.state_tp_gids.push_back(tp_gid);
            }
        }
        // 2) collect sens phase tp gids
        for (const auto& tp_gid : cover_list.sens_cover) {
            const auto& fid = tps[tp_gid].parent_fault_id;
            auto it = result.fault_detail_map.find(fid);
            if (it != result.fault_detail_map.end()) {
                it->second.sens_tp_gids.push_back(tp_gid);
            }
        }
        // 3) collect detect tp gids
        for (const auto& tp_gid : cover_list.det_cover) {
            const auto& fid = tps[tp_gid].parent_fault_id;
            auto it = result.fault_detail_map.find(fid);
            if (it != result.fault_detail_map.end()) {
                it->second.detect_tp_gids.push_back(tp_gid);
            }
        }
    }
}

inline void Reporter::compute_fault_coverage(const vector<Fault>& faults, const vector<TestPrimitive>& tps, 
                                             SimulationResult& result) const {
    for (const auto& fault : faults) {
        auto& fault_detail = result.fault_detail_map[fault.fault_id];
        auto compute_cov_from_tp_gids = [&](const vector<size_t>& tp_gids) -> double {
            bool has_any = false;
            bool has_lt = false;
            bool has_gt = false;
            for (const auto& gid : tp_gids) {
                const auto& tp = tps[gid];
                if (tp.group == OrientationGroup::Single) has_any = true;
                else if (tp.group == OrientationGroup::A_LT_V) has_lt = true;
                else if (tp.group == OrientationGroup::A_GT_V) has_gt = true;
            }
            if (fault.cell_scope == CellScope::SingleCell) {
                return has_any ? 1.0 : 0.0;
            } else if (fault.cell_scope == CellScope::TwoCellCrossRow ||
                       fault.cell_scope == CellScope::TwoCellSameRow ||
                       fault.cell_scope == CellScope::TwoCellRowAgnostic) {
                double cov = 0.0;
                cov += has_lt ? 0.5 : 0.0;
                cov += has_gt ? 0.5 : 0.0;
                return cov;
            }
            return 0.0;
        };
    
        fault_detail.state_coverage = compute_cov_from_tp_gids(fault_detail.state_tp_gids);
        fault_detail.sens_coverage  = compute_cov_from_tp_gids(fault_detail.sens_tp_gids);
        fault_detail.detect_coverage = compute_cov_from_tp_gids(fault_detail.detect_tp_gids);

        // keep backward compatible overall coverage equal to detect coverage
        fault_detail.coverage = fault_detail.detect_coverage;
    }
}

inline void Reporter::compute_final_coverage(SimulationResult& result) const {
    if (result.fault_detail_map.empty()) {
        result.state_coverage = result.sens_coverage = result.detect_coverage = result.total_coverage = 0.0;
        return;
    }
    double sum_state = 0.0;
    double sum_sens = 0.0;
    double sum_detect = 0.0;
    for (const auto& kv : result.fault_detail_map) {
        const auto& detail = kv.second;
        sum_state += detail.state_coverage;
        sum_sens += detail.sens_coverage;
        sum_detect += detail.detect_coverage;
    }
    double n = static_cast<double>(result.fault_detail_map.size());
    result.state_coverage  = sum_state / n;
    result.sens_coverage   = sum_sens  / n;
    result.detect_coverage = sum_detect / n;
    // total coverage follows detect coverage
    result.total_coverage = result.detect_coverage;
}

class FaultSimulator {
public:
    SimulationResult simulate(const MarchTest& mt, const vector<Fault>& faults, const vector<TestPrimitive>& tps);
protected:
    OpTableBuilder op_table_builder;
    StateCoverEngine state_cover_engine;
    SensEngine sens_engine;
    DetectEngine detect_engine;
    Reporter reporter;
};

inline SimulationResult FaultSimulator::simulate(const MarchTest& mt, const vector<Fault>& faults, const vector<TestPrimitive>& tps) {
    SimulationResult result;
    if (mt.elements.empty()) return result; // empty March test
    if (faults.empty()) return result; // no faults
    // 1) 建立 Op table
    result.op_table = op_table_builder.build(mt);
    // 2) 建立 State Cover LUT 與 buckets
    state_cover_engine.build_tp_buckets(tps);
    // 3) 三階段模擬
    result.cover_lists.resize(result.op_table.size());
    for (size_t op_id = 0; op_id < result.op_table.size(); ++op_id) {
        // 1) State cover
        result.cover_lists[op_id].state_cover = state_cover_engine.cover(result.op_table[op_id].pre_state_key);

        // 2) Sens + Detect 必須串在一起檢查
        for (size_t tp_gid : result.cover_lists[op_id].state_cover) {
            auto sens_result = sens_engine.cover(result.op_table, (int)op_id, tps[tp_gid]);
            if (sens_result.status == SensOutcome::Status::SensNone) {
                continue; // 沒致敏就不做偵測
            }
            if (sens_result.status == SensOutcome::Status::SensPartial) {
                result.cover_lists[sens_result.sens_mask_at_op].masked.push_back(
                    MaskOutcome{tp_gid, MaskOutcome::Status::PartMasked});
                continue; // 致敏被遮蔽就不做偵測
            }
            result.cover_lists[sens_result.sens_end_op].sens_cover.push_back(tp_gid);

            auto det_result = detect_engine.cover(result.op_table, sens_result.sens_end_op, tps[tp_gid]);
            if (det_result.det_op != -1) {
                result.cover_lists[det_result.det_op].det_cover.push_back(tp_gid);
            }
            if (det_result.mask_at_op != -1) {
                result.cover_lists[det_result.mask_at_op].masked.push_back(
                    MaskOutcome{tp_gid, MaskOutcome::Status::AllMasked});
            }
        }
    }
    // 3) Reporter
    reporter.build(tps, faults, result);
    return result;
}

using GroupId = size_t;         // 覆蓋群組 id

struct GroupKey {
    string fault_id;
    OrientationGroup og;
    bool operator==(const GroupKey& o) const {
        return fault_id == o.fault_id && og == o.og;
    }
};

struct GroupKeyHash {
    std::size_t operator()(const GroupKey& k) const noexcept {
        return std::hash<string>{}(k.fault_id) ^ (std::hash<int>{}(int(k.og))<<1);
    }
};

class GroupIndex {
public:
    // 構建：根據 faults 與 tps 建 tp→group 映射與 group_meta
    void build(const vector<TestPrimitive>& tps);
    void reset_coverage() { std::fill(group_covered_.begin(), group_covered_.end(), false); }
    void reset_state_flags() { 
        std::fill(group_state_flagged_.begin(), group_state_flagged_.end(), false); 
        std::fill(group_sens_flagged_.begin(), group_sens_flagged_.end(), false);
    }
    void reset_all() { reset_coverage(); reset_state_flags(); }
    int group_of_tp(size_t tp_gid) const; // tp → group
    bool is_tp_covered(int tp_gid) const;
    bool mark_covered_if_new(int tp_gid);      // true 表示這次是新覆蓋
    bool is_tp_static(int tp_gid) const;
    bool mark_group_state_flagged_if_new(int tp_gid);
    bool mark_group_sens_flagged_if_new(int tp_gid);
    bool release_groupflag_if_flagged(int tp_gid);
    
    size_t total_groups() const { return group_meta_.size(); }
    size_t uncovered_groups() const { return std::count(group_covered_.begin(), group_covered_.end(), 0); }
    size_t uncovered_tps() const;
private:
    vector<int> tp2group_; // index: tp_gid, value: group_id
    vector<bool> group_covered_; // index: group_id, value: 0/1
    vector<GroupKey> group_meta_; // index: group_id, value: GroupKey
    vector<size_t> group_sizes_; // index: group_id, value: number of TPs in this group
    vector<bool> group_is_static_; // index: group_id, value: is static group
    vector<bool> group_state_flagged_; // index: group_id, value: has state-covered
    vector<bool> group_sens_flagged_;  // index: group_id, value: has sens-covered
};

inline void GroupIndex::build(const vector<TestPrimitive>& tps) {
    tp2group_.resize(tps.size(), -1);
    unordered_map<GroupKey, GroupId, GroupKeyHash> key2gid;
    for (TpGid t = 0; t < tps.size(); ++t) {
        const auto& tp = tps[t];
        GroupKey gk{tp.parent_fault_id, tp.group};
        auto it = key2gid.find(gk);
        if (it == key2gid.end()) {
            GroupId gid = (GroupId)group_meta_.size();
            key2gid.emplace(gk, gid);
            group_meta_.push_back(gk);
            key2gid[gk] = gid;
            tp2group_[t] = gid;
            group_sizes_.push_back(1);
            group_is_static_.push_back(tp.ops_before_detect.empty());
        } else {
            tp2group_[t] = it->second;
            group_sizes_[it->second]++;
        }
    }
    group_state_flagged_.resize(group_meta_.size(), false);
    group_sens_flagged_.resize(group_meta_.size(), false);
    group_covered_.resize(group_meta_.size(), false);
}

inline int GroupIndex::group_of_tp(size_t tp_gid) const {
    if (tp_gid >= tp2group_.size()) {
        throw runtime_error("GroupIndex::group_of_tp: invalid tp_gid: " + to_string(tp_gid));
    }
    return tp2group_[tp_gid];
}

inline bool GroupIndex::is_tp_covered(int tp_gid) const {
    int gid = group_of_tp(tp_gid);
    return group_covered_[gid] != false;
}

inline bool GroupIndex::mark_covered_if_new(int tp_gid) {
    int gid = group_of_tp(tp_gid);
    if (group_covered_[gid] == false) {
        group_covered_[gid] = true;
        return true;
    }
    return false;
}

inline bool GroupIndex::is_tp_static(int tp_gid) const {
    int gid = group_of_tp(tp_gid);
    return group_is_static_[gid];
}

inline size_t GroupIndex::uncovered_tps() const {
    size_t count = 0;
    for(size_t i = 0; i < group_sizes_.size(); ++i){
        if(!group_covered_[i]){
            count += group_sizes_[i];
        }
    }
    return count;
}

inline bool GroupIndex::mark_group_state_flagged_if_new(int tp_gid) {
    int gid = group_of_tp(tp_gid);
    if (group_state_flagged_[gid] == false) {
        group_state_flagged_[gid] = true;
        return true;
    }
    return false;
}

inline bool GroupIndex::mark_group_sens_flagged_if_new(int tp_gid) {
    int gid = group_of_tp(tp_gid);
    if (group_sens_flagged_[gid] == false) {
        group_sens_flagged_[gid] = true;
        return true;
    }
    return false;
}

inline bool GroupIndex::release_groupflag_if_flagged(int tp_gid) {
    int gid = group_of_tp(tp_gid);
    bool switched = false;
    if (group_state_flagged_[gid] == true) {
        group_state_flagged_[gid] = false;
        switched = true;
    }
    if (group_sens_flagged_[gid] == true) {
        group_sens_flagged_[gid] = false;
        switched = true;
    }
    return switched;
}

struct OpScore {
    double S_cov{0.0}; // raw-state & sens 中屬未覆蓋群組的加權總和
    int D_cov{0}; // raw-detect 中實際增加 coverage 的數量
    int M_50{0};    // 遮蔽當下只完成 state（一般 TP）
    int M_100{0};   // 遮蔽當下已完成 state+sens（或屬 state-only TP）
    int LastPath{0};// 切斷最後活路的遮蔽數（可做倍率）
};

struct ScoreWeights {
    double alpha_S = 1.00; // S_cov 權重
    double beta_D = 2.00; // D_cov 權重
    double gamma_MPart = 0.50; // 遮蔽 50% 懲罰
    double lambda_MAll    = 1.00; // 遮蔽 100% 懲罰
};

struct OpScoreOutcome {
    double total_score{0.0};
    double S_cov{0.0};
    int D_cov{0};
    int part_M_num{0};
    int full_M_num{0};
    double norm_factor{1.0};
};

class OpScorer {
public:
    vector<OpScoreOutcome> score_ops(const vector<RawCoverLists>& sim_results);
    void set_group_index(const vector<TestPrimitive>& tps) { group_index_.build(tps); }
    void set_weights(const ScoreWeights& w) { weights_ = w; }
private:
    GroupIndex group_index_;
    ScoreWeights weights_;                  
    double calculate_S_cov(const vector<TpGid>& state_list, const vector<TpGid>& sens_list);
    int calculate_D_cov(const vector<TpGid>& detect_list);
    int calculate_part_masking_num(const vector<MaskOutcome>& masked_list);
    int calculate_full_masking_num(const vector<MaskOutcome>& masked_list);
};

inline vector<OpScoreOutcome> OpScorer::score_ops(const vector<RawCoverLists>& sim_results) {
    vector<OpScoreOutcome> outcomes;
    // 重置所有 coverage 與階段旗標，確保每次打分彼此獨立
    group_index_.reset_all();
    for (const auto& sim_result : sim_results) {
        OpScoreOutcome out;
        out.norm_factor = max(1.0, (double)group_index_.uncovered_groups());
        out.D_cov = calculate_D_cov(sim_result.det_cover);
        out.S_cov = calculate_S_cov(sim_result.state_cover, sim_result.sens_cover);
        out.part_M_num = calculate_part_masking_num(sim_result.masked);
        out.full_M_num = calculate_full_masking_num(sim_result.masked);
        out.total_score = weights_.alpha_S * out.S_cov / out.norm_factor +
                        weights_.beta_D * static_cast<double>(out.D_cov) / out.norm_factor -
                        weights_.gamma_MPart * out.part_M_num / out.norm_factor -
                        weights_.lambda_MAll * out.full_M_num / out.norm_factor;
        outcomes.push_back(out);
    }
    return outcomes;
}

inline double OpScorer::calculate_S_cov(const vector<TpGid>& state_list, const vector<TpGid>& sens_list) {
    double total_S_cov = 0.0;
    for (const auto& tp_gid : state_list) {
        if (!group_index_.is_tp_covered(tp_gid) && group_index_.mark_group_state_flagged_if_new(tp_gid)) {
            total_S_cov ++;
        }
    }
    for (const auto& tp_gid : sens_list) {
        if (!group_index_.is_tp_covered(tp_gid) && group_index_.mark_group_sens_flagged_if_new(tp_gid)) {
            total_S_cov ++;
        }
    }
    return total_S_cov;
}

inline int OpScorer::calculate_D_cov(const vector<TpGid>& detect_list) {
    int total_D_cov = 0;
    for (const auto& tp_gid : detect_list) {
        if (group_index_.mark_covered_if_new(tp_gid)) {
            total_D_cov ++;
        }
    }
    return total_D_cov;
}

inline int OpScorer::calculate_part_masking_num(const vector<MaskOutcome>& masked_list) {
    int total_M_num = 0;
    for (const auto& mo : masked_list) {
        if (mo.status == MaskOutcome::Status::PartMasked && !group_index_.is_tp_covered(mo.tp_gid)) {
            group_index_.release_groupflag_if_flagged(mo.tp_gid);
            total_M_num ++;
        }
    }
    return total_M_num;
}

inline int OpScorer::calculate_full_masking_num(const vector<MaskOutcome>& masked_list) {
    int total_M_num = 0;
    for (const auto& mo : masked_list) {
        if (mo.status == MaskOutcome::Status::AllMasked && !group_index_.is_tp_covered(mo.tp_gid)) {
            group_index_.release_groupflag_if_flagged(mo.tp_gid);
            total_M_num ++;
        }
    }
    return total_M_num;
}
