// Interface + inline implementation in one header (like FaultsJsonParser).
// Precise std usings for clarity and limited exposure.
#pragma once

#include <string>
#include <vector>
#include <fstream>
#include <optional>
#include <array>
#include <unordered_map>

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

// A named March test consisting of multiple elements
struct RawMarchTest {
    string name;
    string pattern;
};

// Parser for March tests (JSON -> unordered_map<name, MarchTest>)
class MarchTestJsonParser {
public:
    vector<RawMarchTest> parse_file(const string& path);
};

// ---- inline implementation ----

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

// ==============================
//  March-based Fault Simulator (OP-driven + 729x729 LUT)
//  -- append right after MarchTestNormalizer --
// ==============================

struct OpContext {
    Op op; // operation
    int elem_index; // which MarchElement this op belongs to
    int index_within_elem;    // index within the element
    AddrOrder order; // address order of the element

    CrossState pre_state; // state before this op

    int next_op_index{-1}; // index of next op in the same element, or -1 if last
    int head_same{-1}; // index of first op in the same element, or -1 if last
    int head_next{-1}; // index of first op in the next element, or -1 if last
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

const int KEY_BIT = 6;
const int CSS_EXPANDED_NUM = 729; // 3^6

inline size_t encode_to_key(const CrossState& input) {
    size_t key = 0;
    auto valto3 = [](Val v){ return (v==Val::Zero)?0u : (v==Val::One)?1u : 2u; };
    key = key * 3 + valto3(input.A1.D);
    key = key * 3 + valto3(input.A2_CAS.D);
    key = key * 3 + valto3(input.A3.D);
    key = key * 3 + valto3(input.A0.C);
    key = key * 3 + valto3(input.A2_CAS.C);
    key = key * 3 + valto3(input.A4.C);
    return key; // 0..728
}

class CoverLUT {
public:
    CoverLUT();
    const vector<size_t>& get_compatible_tp_keys(const CrossState& op_css) const;
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

class StateCoverEngine {
public:
    void build_tp_buckets(const vector<TestPrimitive>& tps);
    vector<size_t> cover(const CrossState& op_css);
private:
    CoverLUT lut;
    array<vector<size_t>, CSS_EXPANDED_NUM> tp_buckets; // tp_buckets[tp_key] -> list of test pattern indices
};

inline vector<size_t> StateCoverEngine::cover(const CrossState& op_css) {
    vector<size_t> out;
    const auto& tp_keys = lut.get_compatible_tp_keys(op_css);
    for (int key : tp_keys) {
        const auto& v = tp_buckets[key];
        out.insert(out.end(), v.begin(), v.end());
    }
    return out;
}

inline void StateCoverEngine::build_tp_buckets(const vector<TestPrimitive>& tps) {
    tp_buckets.fill(vector<size_t>()); // clear
    for (size_t i = 0; i < tps.size(); ++i) {
        int tp_key = encode_to_key(tps[i].state);
        tp_buckets[tp_key].push_back(i);
    }
}

class SensEngine {
public:
    // 成功回傳完成的 op_id；失敗回 -1
    int cover(const vector<OpContext>& opt, int opt_begin, const TestPrimitive& tp) const;
private:
    bool op_match(const OpContext& march_test_op, const Op& tp_op) const;
};

inline int SensEngine::cover(const vector<OpContext>& opt, int opt_begin, const TestPrimitive& tp) const {
    const int tp_size = static_cast<int>(tp.ops_before_detect.size());
    if (tp_size == 0) return opt_begin;                  // 無 sensitizer 序列：不前進、保留 state cover 位置

    if (opt_begin < 0 || opt_begin >= static_cast<int>(opt.size())) return -1;

    const int last = opt_begin + tp_size - 1;            // 最後一個要匹配到的 op 索引
    if (last < 0 || last >= static_cast<int>(opt.size())) return -1;

    // 必須都在同一個 element 內
    if (opt[opt_begin].elem_index != opt[last].elem_index) return -1;

    // 逐 op 比對
    for (int i = opt_begin, j = 0; i <= last; ++i, ++j) {
        if (!op_match(opt[i], tp.ops_before_detect[j])) return -1;
    }
    return last; // 回傳最後一個成功匹配到的 op 索引
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

class DetectEngine {
public:
    int cover(const vector<OpContext>& opt, int sens_end_id, const TestPrimitive& tp) const;
private:
    bool detect_match(const OpContext& op, const Detector& dec) const;
    bool is_masking_on_D(const OpContext& op) const { return (op.op.kind == OpKind::Write); }
};

inline int DetectEngine::cover(const vector<OpContext>& opt, int sens_end_id, const TestPrimitive& tp) const {
    if (sens_end_id < 0 || sens_end_id >= (int)opt.size()) return -1;
    if (tp.R_has_value) return sens_end_id;

    // 1) 先把 # / ^ / ; 轉成錨點（原本定位點）
    int anchor = -1;
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
    if (anchor < 0 || anchor >= (int)opt.size()) return -1;

    // 2) 決定是否啟用「往後掃描」模式
    //    僅當 F 在 D 面有具體值時（對應到 detector.kind==Read 且 value!=X）才啟用。
    if (!tp.F_has_value) {
        // 舊語意：只在錨點做一次比對
        return detect_match(opt[anchor], tp.detector) ? anchor : -1;
    }

    // 3) 新語意（F 有值）：自錨點起往後掃描，直到找到 detector 或被 mask
    //    這裡的「往後」是針對同一目標 cell 的操作序列（同一地址的執行序），
    //    因 opt 是 per-address/element 的攤平成序關係（參見 OpTableBuilder 的鄰接建構）。:contentReference[oaicite:3]{index=3}
    for (int i = anchor; i < (int)opt.size(); ++i) {
        // 先檢查遮罩：一旦遇到會寫 D 的操作，代表 fault effect 被洗掉 → 失敗
        if (is_masking_on_D(opt[i])) {
            return -1;
        }
        // 符合 detector（例如 R0/R1）→ 成功
        if (detect_match(opt[i], tp.detector)) {
            return i;
        }
        // 否則持續往後找下一個 op
    }
    return -1; // 沒找到 detector 且未被寫 D 擋到 → 視為未偵測
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

// ---------- 三階段結果與 Reporter ----------
struct CoverLists {
    vector<size_t>     state_cover; // [op] -> tp_gid[]
    vector<size_t>     sens_cover;  // [op] -> tp_gid[]
    vector<size_t>     det_cover;   // [op] -> tp_gid[] (偵測命中)
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
    vector<size_t> state_tp_gids;  // TPs that state-covered this fault
    vector<size_t> sens_tp_gids;   // TPs that sensitized this fault
    vector<size_t> detect_tp_gids; // TPs that detected this fault
};
struct SimulationResult {
    double total_coverage{0.0};
    vector<CoverLists> cover_lists; // per op
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
    void compute_total_coverage(SimulationResult& result) const;
};

inline void Reporter::build(const vector<TestPrimitive>& tps, const vector<Fault>& faults,
                                        SimulationResult& result) const {
    build_fault_map(faults, result);
    analyze_fault_detail(tps, result);
    compute_fault_coverage(faults, tps, result);
    compute_total_coverage(result);
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

inline void Reporter::compute_total_coverage(SimulationResult& result) const {
    if (result.fault_detail_map.empty()) {
        result.total_coverage = 0.0;
        return;
    }
    double sum = 0.0;
    for (const auto& [fid, detail] : result.fault_detail_map) {
        sum += detail.coverage;
    }
    result.total_coverage = sum / result.fault_detail_map.size();
}

class FaultSimulator {
public:
    SimulationResult simulate(const MarchTest& mt, const vector<Fault>& faults, const vector<TestPrimitive>& tps);
private:
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
        result.cover_lists[op_id].state_cover = state_cover_engine.cover(result.op_table[op_id].pre_state);

        // 2) Sens + Detect 必須串在一起檢查
        for (size_t tp_gid : result.cover_lists[op_id].state_cover) {
            int sens_end_id = sens_engine.cover(result.op_table, (int)op_id, tps[tp_gid]);
            if (sens_end_id == -1) continue;                // 沒致敏就不做偵測
            result.cover_lists[sens_end_id].sens_cover.push_back(tp_gid);

            int det_id = detect_engine.cover(result.op_table, sens_end_id, tps[tp_gid]);
            if (det_id != -1) {
                result.cover_lists[det_id].det_cover.push_back(tp_gid);
            }
        }
    }
    // 3) Reporter
    reporter.build(tps, faults, result);
    return result;
}
