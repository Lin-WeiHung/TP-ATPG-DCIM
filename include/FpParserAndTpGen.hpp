// html: g++ -std=c++20 -Wall -Wextra -Iinclude -I0916Cross_shape 0916Cross_shape/TPReportHtml.cpp -o 0916Cross_shape/tp_html && ./0916Cross_shape/tp_html
// Readme: g++ -std=c++20 -Wall -Wextra -Iinclude -I0916Cross_shape 0916Cross_shape/TPGeneratorTest.cpp -o 0916Cross_shape/tpg && ./0916Cross_shape/tpg

// Parser.hpp - Fault list parser
// 使用 nlohmann/json.hpp
// 單一函式 parse_file 回傳 vector<FaultEntry>

#pragma once

#include <string>
#include <vector>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <utility>
#include <string_view>
#include <cstddef>
#include <cctype>
#include <algorithm>

#include "nlohmann/json.hpp"

// ---- 精確 using ----
using std::string;
using std::vector;
using std::ifstream;
using std::optional;
using std::runtime_error;
using std::move;
using std::string_view;
using std::size_t;

// === Json Parser ===
/**
 * Parse a JSON file containing an array of fault descriptions into a vector of RawFault.
 *
 * Usage:
 *   auto faults = FaultsJsonParser::parse_file("/path/to/faults.json");
 *   for (const RawFault& f : faults) {
 *       // access f.fault_id, f.category, f.cell_scope, f.fp_raw, ...
 *   }
 *
 * Expected JSON format (top-level array):
 * [
 *   {
 *     "fault_id": "<string>",
 *     "category": "<string>",
 *     "cell_scope": "<string>",
 *     "fault_primitives": ["<string>", "<string>", ...]
 *   },
 *   ...
 * ]
 *
 * Parameters:
 * - path: filesystem path to a JSON file. The file is opened for reading.
 *
 * Returns:
 * - std::vector<RawFault> where each RawFault is populated with:
 *     - fault_id  : copied from JSON "fault_id"
 *     - category  : copied from JSON "category"
 *     - cell_scope: copied from JSON "cell_scope"
 *     - fp_raw    : sequence of strings copied from JSON "fault_primitives"
 *
 * Error handling / Exceptions:
 * - Throws std::runtime_error if the file cannot be opened or if the top-level
 *   JSON value is not an array.
 * - May throw exceptions from nlohmann::json (e.g., out_of_range, type_error)
 *   when required fields are missing or have the wrong types.
 *
 * Notes:
 * - The function reads and parses the entire JSON document into memory.
 * - The returned vector is reserved to the number of top-level array elements
 *   to reduce reallocations; RawFaults are moved into the vector.
 * - The function is stateless and may be called concurrently from multiple
 *   threads provided that each call uses a distinct path.
 */

// 基本型別定義
enum class CellScope { SingleCell, TwoCellRowAgnostic, TwoCellSameRow, TwoCellCrossRow };
enum class Category  { EitherReadOrCompute, MustRead, MustCompute };
enum class OpKind { Write, Read, ComputeAnd }; // 操作種類：Write / Read / Compute(AND) —— 之後要擴其他 Compute 也容易加
enum class Val { Zero, One, X }; // 三值邏輯，用於 D/C

struct RawFault { // JSON 解析結果的中介結構
    string fault_id;                 // "SA0"
    string category;                 // "either_read_or_compute" | ...
    string cell_scope;               // e.g. "single cell"
    vector<string> fp_raw;               // 原始 primitive 字串陣列
};

class FaultsJsonParser { // FaultsJsonParser：讀入 faults.json，回傳 RawFault 列表
public:
    vector<RawFault> parse_file(const string& path);
};

// 直接解析檔案；失敗時丟出 std::runtime_error
inline vector<RawFault> FaultsJsonParser::parse_file(const string& path) {
    ifstream ifs(path);
    if (!ifs) {
        throw runtime_error("Cannot open file: " + path);
    }
    nlohmann::json j; 
    ifs >> j;
    if (!j.is_array()) {
        throw runtime_error("Top-level JSON is not an array");
    }
    vector<RawFault> out;
    out.reserve(j.size());
    for (const auto& item : j) {
        RawFault fe;
        fe.fault_id = item.at("fault_id").get<string>();
        fe.category = item.at("category").get<string>();
        fe.cell_scope = item.at("cell_scope").get<string>();
        for (const auto& p : item.at("fault_primitives")) {
            fe.fp_raw.push_back(p.get<string>());
        }
        out.push_back(move(fe));
    }
    return out;
}

// === FaultNormalizer ===
/*
FaultNormalizer — 簡易說明與使用範例
----------------------------------

功能概述
    FaultNormalizer 將由 FaultsJsonParser 產生的 RawFault（即 JSON 原始欄位）轉換為結構化、便於後續 TP（test primitive）產生的 Fault 資訊。
    主要工作：
        - 解析每個 primitive 的「五段式」敘述（Sa / Sv / F / R / C）
        - 將字串 token 解析為結構化欄位（SSpec / FSpec / RSpec / CSpec / Op 等）
        - 根據 category / cell_scope 對 Sa 欄位的存在性與解析行為做差別處理
        - 產生一個 Fault（含多個解析後的 FPExpr）

輸入
    - RawFault (來源通常為 FaultsJsonParser::parse_file)
        RawFault 包含：
            - fault_id      : std::string
            - category      : std::string (如 "either_read_or_compute", "must_read", "must_compute")
            - cell_scope    : std::string (如 "single cell", "two-cell row-agnostic", ...)
            - fault_primitives : vector<string>（每個 primitive 為像 "<.../.../.../.../...>" 的五段式）

輸出
    - Fault
        包含：
            - fault_id (複製自 RawFault)
            - category (enum Category)
            - cell_scope (enum CellScope)
            - primitives : vector<FPExpr>
                每個 FPExpr 內含解析後的 Sa (optional<SSpec>), Sv (SSpec), F (FSpec), R (RSpec), C (CSpec)
                以及衍生欄位 s_has_any_op（若 Sa 或 Sv 任一側有 ops 則為 true）

主要介面
    - Fault FaultNormalizer::normalize(const RawFault& rf);
        - 直接將一筆 RawFault 轉為解析後的 Fault（包含所有 primitives）
    - Category to_category(const string& s);
    - CellScope to_scope(const string& s);
    - FPExpr parse_fp(const string& raw, CellScope scope);
        - parse_sa / parse_sv / parse_f / parse_r / parse_c 為輔助解析函式

使用範例（示意）
    // 假設已由 FaultsJsonParser 取得 raw_faults
    // for (const RawFault& rf : raw_faults) {
    //     FaultNormalizer normalizer;
    //     Fault f = normalizer.normalize(rf);
    //     // 使用 f.primitives 做 TP 產生
    // }

解析規則重點（注意事項）
    - 五段式切分
        - parse_fp 會先移除左右角括號 '<' '>'（若存在），再以 '/' 或 ';' 分割五段式欄位（Sa / Sv / F / R / C）。
        - 至少需包含 4 個段（在 single-cell 情況下 Sa 可省略）。

    - Sa 與 Sv 的差異
        - Sa（side-a）：parse_side(s, allow_and=false)
            - 不允許 AND token（例如 "AND0Ci1D"）出現，主要解析 Ci、寫入鏈([01]W...D) 或 Read( R0/R1)。
        - Sv（side-v）：parse_side(s, allow_and=true)
            - 允許 AND token（"AND{m}Ci{d}D"）的解析，且如有需要會自動插入 Write 以對齊 last_D（確保 compute 輸入的 D 存在）。
            - 會處理可連續的寫入鏈，例如 "1W0D, AND0Ci1D" 這類複合情形。

    - parse_side 共通要點
        - 解析出的 SSpec 包含：
            - pre_D（optional）：解析到的初始 D（若有）
            - Ci（optional）：若出現 Ci token，會設置 Ci 值
            - ops（vector<Op>）：依序記錄寫入、讀取、ComputeAnd 等操作；最後的 D（last_D）由 ops 推導
            - last_D 規則：若無 ops 則 last_D=pre_D；若有 ops 則取最後一個 Op 對應的 D（或由 ensure_D 自動插入 Write）
        - Read token 以 'R0' / 'R1' 表示，會被解析成 OpKind::Read 並存放 value。
        - Write token 為格式 [01]W，串接後必須以 D 結尾（...D）。若出現寫入但未以 D 結尾則拋例外。

    - Compute AND token（僅 Sv）
        - 格式：AND{m}Ci{d}D，例如 "AND0Ci1D" 意味著 Compute 操作的中位元 C_M = 0，且期望的 D 為 1。
        - 解析時會：
            - 使用 ensure_D 對 Sv 的 last_D 做必要補寫（可能插入一個 Write Op）
            - 將 ComputeAnd 操作加入 ops（C_M 設定為所給值，C_T/C_B 保持 X）

    - F / R / C 欄位解析
        - F、R 欄位接受 "-" 或空字串表示缺省（值為 X），或 "[01]D" 表示具體的 D 值（0D/1D）。
        - C 欄位接受 "-" 或空字串，或 "[01]Co"（例如 "1Co"）表示 Compute 結果 Co。

例外與錯誤處理
    - 若輸入格式不符合預期（未知 category/cell_scope、token 格式錯誤、缺少必要字元等），會以 throw std::runtime_error 或 nlohmann::json 的例外傳播錯誤訊息。
    - parse_side 對語法較嚴格（例如 "AND" 後缺少必需元素會拋出），呼叫端建議在高層捕捉並顯示錯誤來源（原始 raw 字串）。

其他設計注意
    - FaultNormalizer 本身無內部狀態（normalize 為純函式風格，僅以輸入 RawFault 產生輸出），因此在多執行緒下若每個線程使用各自的 FaultNormalizer 實例或以 const 調用，應是安全的。
    - 對於「Ci 鏈的一致性」與 cross-shape 的全域合併規則（例如 TP 產生階段需要 enforceCiChain），此類行為屬於 TP 生成 / CrossState 層級，並非 FaultNormalizer 的職責。FaultNormalizer 僅如實解析並提供必要欄位（Ci、ops、last_D 等）。
    - 若需擴充更多 Compute type（非 AND），可在 parse_side 中新增對應 token 的解析並填寫 OpKind 與 C_T/C_M/C_B。
*/

// 一筆操作：
// - Write/Read：用 value 表示 0/1
// - Compute(AND)：用 (C_T, C_M, C_B) 表示 C(T)(M)(B)，M 在 either_read_or_compute 一般固定為 1來傳遞D fault，取代Read
struct Op {
    OpKind kind;
    Val   value{Val::X};             // Write/Read 用；必為 0/1
    Val   C_T{Val::X}, C_M{Val::X}, C_B{Val::X}; // Compute 用
};

// === 五段式資訊 ===
struct SSpec {
    optional<Val> pre_D{Val::X};
    optional<Val> Ci{Val::X};
    vector<Op>    ops;    // 允許多個，像 "1Ci1D, AND0Ci1D"
    optional<Val> last_D{Val::X}; // 規則：有 Op 取最後 Op 的 D；無 Op=pre_D
    bool has_ops() const { return !ops.empty(); }
};

// F：只記「Faulty 的 D 值」
struct FSpec {
    optional<Val> FD{Val::X};
};

// R：只記「期望讀到的 D」（大多為 '-'，但型別預留）
struct RSpec {
    optional<Val> RD{Val::X};
};

// C：Compute 的錯誤結果 Co（0Co / 1Co）
struct CSpec {
    optional<Val> Co{Val::X};
};

// === 一個 FP 的解析結果 ===
struct FPExpr {
    optional<SSpec> Sa; // single-cell 時為 nullopt
    SSpec Sv;
    FSpec  F;
    RSpec  R;
    CSpec  C;
    // 方便下游（TP 生成）用的衍生欄位
    bool s_has_any_op = false; // Sa/Sv 任一側只要有 Op 就為 true（你規則 #2 會用到）
};

struct Fault {
    string fault_id;
    Category category;
    CellScope cell_scope;
    vector<FPExpr> primitives; // 解析後的 primitives
};

class FaultNormalizer {
public:
    // 主函式
    Fault normalize(const RawFault& rf);

    // 主要解析函式
    Category  to_category(const string& s);
    CellScope to_scope(const string& s);
    FPExpr parse_fp(const string& raw, CellScope scope);

    // 五段式子解析（Sa/Sv 封裝呼叫同一套解析核心）
    // - Sa：不允許 AND 計算 token
    // - Sv：允許 AND 計算 token，並在必要時自動插入 Write 以對齊 D
    SSpec parse_sa(const string& s);     // e.g. "1W0D" / "1Ci0D"
    SSpec parse_sv(const string& s);     // e.g. "1Ci1D, AND0Ci1D"
    FSpec  parse_f (const string& s);     // e.g. "0D" 或空
    RSpec  parse_r (const string& s);     // e.g. "-" 或 "1D"
    CSpec  parse_c (const string& s);     // e.g. "1Co" / "0Co" 或空
private:
    // === 共同小工具（僅此類內可用） ===
    // 去除字串內部所有空白（保留 token 粒度）
    string strip_spaces(const string& s);
    // 以逗號分割上層 token，並做 trim
    vector<string> split_by_comma(const string& s);
    // 字元 '0'/'1' → Val
    optional<Val> to_val(char c);

    // 解析 Sa/Sv 的共用核心
    // allow_and=false → Sa 行為；allow_and=true → Sv 行為
    SSpec parse_side(const string& s, bool allow_and);

    string trim_between_angles(const string& raw);  // 去 "< >"
    string trim(const string& s);                   // 去頭尾空白
};

inline Fault FaultNormalizer::normalize(const RawFault& rf) {
    Fault f;
    f.fault_id = rf.fault_id;
    f.category = to_category(rf.category);
    f.cell_scope = to_scope(rf.cell_scope);
    for (const auto& raw : rf.fp_raw) {
        f.primitives.push_back(parse_fp(raw, f.cell_scope));
    }
    return f;
}

inline Category FaultNormalizer::to_category(const string& s) {
    if (s == "either_read_or_compute") return Category::EitherReadOrCompute;
    if (s == "must_read") return Category::MustRead;
    if (s == "must_compute") return Category::MustCompute;
    throw runtime_error("Unknown category: " + s);
}

inline CellScope FaultNormalizer::to_scope(const string& s) {
    if (s == "single cell") return CellScope::SingleCell;
    // 支援兩種命名風格
    if (s == "two-cell row-agnostic" || s == "two cell (row-agnostic)") return CellScope::TwoCellRowAgnostic;
    if (s == "two-cell same-row"     || s == "two cell same row")      return CellScope::TwoCellSameRow;
    if (s == "two-cell cross-row"    || s == "two cell cross row")     return CellScope::TwoCellCrossRow;
    throw runtime_error("Unknown cell_scope: " + s);
}

inline FPExpr FaultNormalizer::parse_fp(const string& raw, CellScope scope) {
    // 先去除 < >
    string s = trim_between_angles(raw);
    // 切五段式
    vector<string> parts;
    size_t start = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '/' || s[i] == ';') {
            parts.push_back(trim(s.substr(start, i - start)));
            start = i + 1;
        }
    }
    parts.push_back(trim(s.substr(start)));
    if (scope == CellScope::SingleCell) {
        if (parts.size() != 4) {
            throw runtime_error("Single-cell FP must have exactly 4 parts: " + raw);
        }
    } else {
        if (parts.size() != 5) {
            throw runtime_error("Two-cell FP must have exactly 5 parts: " + raw);
        }
    }
    // 解析各個部分
    FPExpr expr;
    size_t idx = 0;
    if (scope != CellScope::SingleCell) { // 有 Sa
        expr.Sa = parse_sa(parts[idx++]);
    }
    expr.Sv = parse_sv(parts[idx++]);
    expr.F  = parse_f (parts[idx++]);
    expr.R  = parse_r (parts[idx++]);
    expr.C  = parse_c (parts[idx++]);

    // 衍生欄位
    expr.s_has_any_op = expr.Sa.has_value() && expr.Sa->has_ops();
    expr.s_has_any_op = expr.s_has_any_op || expr.Sv.has_ops();

    return expr;
}

// --- Sa/Sv 封裝：統一走 parse_side ---
inline SSpec FaultNormalizer::parse_sa(const string& s) { return parse_side(s, /*allow_and=*/false); }
inline SSpec FaultNormalizer::parse_sv(const string& s) { return parse_side(s, /*allow_and=*/true ); }

// === 私有小工具 ===
inline string FaultNormalizer::strip_spaces(const string& s) {
    string u; u.reserve(s.size());
    for (char c : s) if (!isspace(static_cast<unsigned char>(c))) u.push_back(c);
    return u;
}
inline vector<string> FaultNormalizer::split_by_comma(const string& s) {
    vector<string> out; string cur; cur.reserve(s.size());
    for (char c : s) {
        if (c == ',') { out.push_back(trim(cur)); cur.clear(); }
        else { cur.push_back(c); }
    }
    if (!cur.empty()) out.push_back(trim(cur));
    return out;
}
inline optional<Val> FaultNormalizer::to_val(char c) {
    if (c == '0') return Val::Zero;
    if (c == '1') return Val::One;
    return std::nullopt;
}

// === 共用核心解析 ===
// 支援：
// - [Ci]? ([01]W)*[01]D 連續寫入鏈
// - R0/R1 讀取
// - allow_and=true 時再支援 ANDxCiyD，且在需要時自動插入 Write(y)
inline SSpec FaultNormalizer::parse_side(const string& s, bool allow_and) {
    SSpec out; // 預設皆為 X
    const string top = trim(s);
    const vector<string> tokens = split_by_comma(top);

    auto parse_pre_ci_and_writes = [&](const string& raw){
        string u = strip_spaces(raw);
        size_t i = 0;
        auto expect = [&](char c){ if (i >= u.size() || u[i] != c) throw runtime_error("parse_side: expect '" + string(1,c) + "' in '" + raw + "'"); ++i; };

        // [Ci]?
        if (i + 3 <= u.size() && (u[i] == '0' || u[i] == '1') && u.substr(i+1,2) == "Ci") { out.Ci = *to_val(u[i]); i += 3; }
        if (i >= u.size()) return; // 僅有 Ci

        // Read R0/R1
        if (u[i] == 'R') { expect('R'); if (i >= u.size() || (u[i] != '0' && u[i] != '1')) throw runtime_error("parse_side: bad Read in '" + raw + "'"); Op rop; rop.kind=OpKind::Read; rop.value=*to_val(u[i]); out.ops.push_back(rop); ++i; return; }

        // 寫入鏈：PreD -> (W v)* -> D
        if (u[i] != '0' && u[i] != '1') return; // 不是此形，可能交由 AND 分支處理
        out.pre_D = *to_val(u[i]); ++i;
        vector<Val> wvals;
        while (i < u.size() && u[i] == 'W') { ++i; if (i>=u.size() || (u[i] != '0' && u[i] != '1')) throw runtime_error("parse_side: malformed 'W' in '"+raw+"'"); wvals.push_back(*to_val(u[i])); ++i; }
        if (i < u.size() && u[i] == 'D') { ++i; for (Val v : wvals) { Op w; w.kind=OpKind::Write; w.value=v; out.ops.push_back(w);} out.last_D = wvals.empty()? out.pre_D : wvals.back(); }
        else if (!wvals.empty()) throw runtime_error("parse_side: write chain must end with 'D' in '"+raw+"'");
    };

    auto ensure_D = [&](Val d){
        auto put_write = [&](Val v){ Op w; w.kind=OpKind::Write; w.value=v; out.ops.push_back(w); out.last_D=v; };
        bool last_unknown = !out.last_D.has_value() || out.last_D.value() == Val::X;
        bool pre_unknown  = !out.pre_D.has_value() || out.pre_D.value()  == Val::X;
        if (last_unknown) {
            if (pre_unknown) { out.pre_D = d; out.last_D = d; }
            else { if (out.pre_D.value() != d) put_write(d); else out.last_D = out.pre_D; }
        } else { if (out.last_D.value() != d) put_write(d); }
    };

    // 支援兩種 AND 形式：
    // 1) AND[01]Ci[01]D  （舊有：先確保 D 後做 ComputeAnd）
    // 2) AND[01]Ci        （新加：不考慮/不保證 D，直接做 ComputeAnd）
    auto parse_and_token = [&](const string& raw){
        if (!allow_and) return false;
        string u = strip_spaces(raw);
        if (u.rfind("AND", 0) != 0) return false;

        size_t i = 3;
        // 解析遮罩 cm
        if (i>=u.size() || (u[i] != '0' && u[i] != '1'))
            throw runtime_error("parse_side: AND needs [01] after 'AND' in '"+raw+"'");
        Val cm = *to_val(u[i]); ++i;

        // 必須跟著 "Ci"
        if (i+1 > u.size() || u.substr(i,2) != "Ci")
            throw runtime_error("parse_side: AND needs 'Ci' in '"+raw+"'");
        i += 2;

        // 如果已到 token 結尾 → 走「不考慮D」的 AND 版本
        if (i == u.size()) {
            Op op; op.kind=OpKind::ComputeAnd; op.C_T=Val::X; op.C_M=cm; op.C_B=Val::X;
            out.ops.push_back(op);
            return true;
        }

        // 否則，必須是 [01]D（舊有語法）
        if (u[i] != '0' && u[i] != '1')
            throw runtime_error("parse_side: AND must end right after 'Ci' or follow with [01]D in '"+raw+"'");
        Val needD = *to_val(u[i]); ++i;

        if (i>=u.size() || u[i] != 'D')
            throw runtime_error("parse_side: AND must end with 'D' in '"+raw+"'");
        ++i;

        // 可接受有無多餘空白，但已 strip_spaces，理論上 i==u.size()
        ensure_D(needD);
        Op op; op.kind=OpKind::ComputeAnd; op.C_T=Val::X; op.C_M=cm; op.C_B=Val::X;
        out.ops.push_back(op);
        return true;
    };

    // 逐 token 處理（依序）：先試 AND，再試一般片段
    for (const auto& tok_raw : tokens) {
        string tok = strip_spaces(tok_raw);
        if (tok.empty()) continue;
        if (parse_and_token(tok)) continue;
        parse_pre_ci_and_writes(tok);
    }

    // 若仍未知 last_D，但 pre_D 已知，沿用 pre_D
    if (out.last_D.has_value() && out.last_D.value() == Val::X && out.pre_D.has_value() && out.pre_D.value() != Val::X) {
        out.last_D = out.pre_D;
    }
    return out;
}


inline FSpec FaultNormalizer::parse_f(const string& s) {
    FSpec out; // default X
    if (s.empty() || s == "-") {
        out.FD = Val::X; // empty or '-' means no R part
    } else if (s.size() >= 2 && (s[0] == '0' || s[0] == '1') && s[1] == 'D') {
        out.FD = s[0] == '0' ? Val::Zero : Val::One;
    } else {
        throw runtime_error("parse_f: malformed F part: '" + s + "'");
    };
    return out; 
}

inline RSpec FaultNormalizer::parse_r(const string& s) {
    RSpec out; // default X
    if (s.empty() || s == "-") {
        out.RD = Val::X; // empty or '-' means no R part
    } else if (s.size() >= 2 && (s[0] == '0' || s[0] == '1') && s[1] == 'D') {
        out.RD = s[0] == '0' ? Val::Zero : Val::One;
    } else {
        throw runtime_error("parse_r: malformed R part: '" + s + "'");
    };
    return out;
}

inline CSpec FaultNormalizer::parse_c(const string& s) {
    CSpec out; // default X
    if (s.empty() || s == "-") {
        out.Co = Val::X; // empty or '-' means no C part
    } else if (s.size() == 3 && (s[0] == '0' || s[0] == '1') && s[1] == 'C' && s[2] == 'o') {
        out.Co = s[0] == '0' ? Val::Zero : Val::One;
    } else {
        throw runtime_error("parse_c: malformed C part: '" + s + "'");
    };
    return out;
}

inline string FaultNormalizer::trim_between_angles(const string& raw) {
    size_t b = 0, e = raw.size();
    while (b < e && isspace(static_cast<unsigned char>(raw[b]))) ++b;
    while (e > b && isspace(static_cast<unsigned char>(raw[e-1]))) --e;
    if (b < e && raw[b] == '<' && raw[e-1] == '>') {
        ++b; --e;
        while (b < e && isspace(static_cast<unsigned char>(raw[b]))) ++b;
        while (e > b && isspace(static_cast<unsigned char>(raw[e-1]))) --e;
    }
    return raw.substr(b, e - b);
}

inline string FaultNormalizer::trim(const string& s) {
    size_t b = 0, e = s.size();
    while (b < e && isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && isspace(static_cast<unsigned char>(s[e-1]))) --e;
    return s.substr(b, e - b);
}



// === TP Generator ===

// 基本型別定義
enum class OrientationGroup { Single, A_LT_V, A_GT_V };
enum class PositionMark { Adjacent, SameElementHead, NextElementHead }; // #, ^, ;
enum class WhoIsPivot { Victim, Aggressor };
enum class DetectKind { Read, ComputeAnd };
enum class Slot { A0, A1, A2, A3, A4 };

// === 五欄 cross-shape：{左, 上, 中(pivot), 下, 右} 的 D/C
struct DC { Val D{Val::X}; Val C{Val::X}; };

struct CrossState {
    DC A0, A1, A2_CAS, A3, A4;
    // 全域規則：C1=C2=C3（同列 Ci 必須一致）
    void enforceDCrule() {
        Val targetDl = mergeD(A0.D, A1.D);
        Val targetDr = mergeD(A3.D, A4.D);
        A0.D = A1.D = targetDl;
        A3.D = A4.D = targetDr;
        Val targetC = mergeC(A1.C, A2_CAS.C, A3.C); // 任一為 One => One；全 X => X；(如遇 0 與 1 衝突可視為錯誤/以 One 為上限收斂)
        A1.C = A2_CAS.C = A3.C = targetC;
    }
    Val mergeD(Val a, Val b) {
        // 0 與 1 衝突
        if ((a == Val::Zero && b == Val::One) || (a == Val::One && b == Val::Zero)) {
            throw runtime_error("Conflicting Di values in mergeD");
        }
        if (a == Val::One || b == Val::One) return Val::One;
        if (a == Val::Zero || b == Val::Zero) return Val::Zero;
        return Val::X;
    }
    Val mergeC(Val a, Val b, Val c) {
        // 0 與 1 衝突
        if ((a == Val::Zero && b == Val::One) || (a == Val::One && b == Val::Zero) ||
            (a == Val::Zero && c == Val::One) || (a == Val::One && c == Val::Zero) ||
            (b == Val::Zero && c == Val::One) || (b == Val::One && c == Val::Zero)) {
            throw runtime_error("Conflicting Ci values in mergeC");
        }
        if (a == Val::One || b == Val::One || c == Val::One) return Val::One;
        if (a == Val::Zero || b == Val::Zero || c == Val::Zero) return Val::Zero;
        return Val::X;
    }
};

inline DC& pick(CrossState& s, Slot k) {
    switch (k) {
        case Slot::A0: return s.A0; 
        case Slot::A1: return s.A1; 
        case Slot::A2: return s.A2_CAS; 
        case Slot::A3: return s.A3; 
        case Slot::A4: return s.A4; 
    }
    throw runtime_error("bad slot");
}
inline const DC& pick(const CrossState& s, Slot k) {
    switch (k) {
        case Slot::A0: return s.A0; 
        case Slot::A1: return s.A1; 
        case Slot::A2: return s.A2_CAS; 
        case Slot::A3: return s.A3; 
        case Slot::A4: return s.A4; 
    }
    throw runtime_error("bad slot");
}

// === 偵測器 ===
struct Detector {
    Op detectOp{OpKind::Read, Val::X}; // Read: value; ComputeAnd: C_T/C_M/C_B
    PositionMark pos{PositionMark::Adjacent};
    bool has_set_Ci{false};
    enum class AddrOrder { None, Assending, Decending };
    AddrOrder order{AddrOrder::None};
};

// === 最終 TP ===
struct TestPrimitive {
    // 溯源
    std::string parent_fault_id;
    std::size_t parent_fp_index{};

    // 方向與 scope 群組
    OrientationGroup group{OrientationGroup::Single};

    // 狀態 + 操作（在偵測之前要做的致敏操作）
    CrossState state;
    std::vector<Op> ops_before_detect; // 直接沿用你 AST 的 OpKind/參數

    // 偵測器
    Detector detector;
    bool F_has_value{false};
    bool R_has_value{false}; // 如果R為錯誤則不需要額外detector
    bool C_has_value{false};
};

struct OrientationPlan {
    OrientationGroup group;
    WhoIsPivot pivot;
    // 非 pivot 的槽位
    std::vector<Slot> nonPivotSlots;
};

class OrientationSelector {
public:
    // 生成所有需要的方向群：Single => 只回 Single；Two-cell => 回 a<v 與 a>v 兩個
    vector<OrientationPlan> plans(const CellScope& scope, const FPExpr& fp) const;
private:
    void plan_single     (vector<OrientationPlan>& plans) const;
    void plan_sameRow    (vector<OrientationPlan>& plans, const FPExpr& fp) const;
    void plan_rowAgnostic(vector<OrientationPlan>& plans, const FPExpr& fp) const;
    void plan_crossRow   (vector<OrientationPlan>& plans, const FPExpr& fp) const;
    WhoIsPivot decidePivot(const FPExpr& fp) const;
};

inline vector<OrientationPlan> OrientationSelector::plans(const CellScope& scope, const FPExpr& fp) const {
    vector<OrientationPlan> out;
    switch (scope) {
        case CellScope::SingleCell:          plan_single      (out); break;
        case CellScope::TwoCellSameRow:      plan_sameRow     (out, fp); break;
        case CellScope::TwoCellRowAgnostic:  plan_rowAgnostic (out, fp); break;
        case CellScope::TwoCellCrossRow:     plan_crossRow    (out, fp); break;
    }
    return out;
}

inline void OrientationSelector::plan_single(vector<OrientationPlan>& plans) const {
    OrientationPlan p;
    p.group = OrientationGroup::Single;
    p.pivot = WhoIsPivot::Victim; // single-cell 時預設 Victim
    p.nonPivotSlots = {}; // 無非 pivot
    plans.push_back(p);
}

inline void OrientationSelector::plan_sameRow(vector<OrientationPlan>& plans, const FPExpr& fp) const {
    WhoIsPivot pivot = decidePivot(fp);
    if (pivot == WhoIsPivot::Victim) {
        plans.push_back(OrientationPlan{OrientationGroup::A_LT_V, WhoIsPivot::Victim, {Slot::A1}});
        plans.push_back(OrientationPlan{OrientationGroup::A_GT_V, WhoIsPivot::Victim, {Slot::A3}});
    } else if (pivot == WhoIsPivot::Aggressor) {
        plans.push_back(OrientationPlan{OrientationGroup::A_LT_V, WhoIsPivot::Aggressor, {Slot::A3}});
        plans.push_back(OrientationPlan{OrientationGroup::A_GT_V, WhoIsPivot::Aggressor, {Slot::A1}});
    }
}

inline void OrientationSelector::plan_rowAgnostic(vector<OrientationPlan>& plans, const FPExpr& fp) const {
    WhoIsPivot pivot = decidePivot(fp);
    if (pivot == WhoIsPivot::Victim) {
        plans.push_back(OrientationPlan{OrientationGroup::A_LT_V, WhoIsPivot::Victim, {Slot::A0, Slot::A1}});
        plans.push_back(OrientationPlan{OrientationGroup::A_GT_V, WhoIsPivot::Victim, {Slot::A3, Slot::A4}});
    } else if (pivot == WhoIsPivot::Aggressor) {
        plans.push_back(OrientationPlan{OrientationGroup::A_LT_V, WhoIsPivot::Aggressor, {Slot::A3, Slot::A4}});
        plans.push_back(OrientationPlan{OrientationGroup::A_GT_V, WhoIsPivot::Aggressor, {Slot::A0, Slot::A1}});
    }
}

inline void OrientationSelector::plan_crossRow(vector<OrientationPlan>& plans, const FPExpr& fp) const {
    WhoIsPivot pivot = decidePivot(fp);
    if (pivot == WhoIsPivot::Victim) {
        plans.push_back(OrientationPlan{OrientationGroup::A_LT_V, WhoIsPivot::Victim, {Slot::A0}});
        plans.push_back(OrientationPlan{OrientationGroup::A_GT_V, WhoIsPivot::Victim, {Slot::A4}});
    } else if (pivot == WhoIsPivot::Aggressor) {
        plans.push_back(OrientationPlan{OrientationGroup::A_LT_V, WhoIsPivot::Aggressor, {Slot::A4}});
        plans.push_back(OrientationPlan{OrientationGroup::A_GT_V, WhoIsPivot::Aggressor, {Slot::A0}});
    }
}

inline WhoIsPivot OrientationSelector::decidePivot(const FPExpr& fp) const {
    // 若 Sa 有 Op，則 Pivot 必須是 Sa
    if (fp.Sa.has_value() && fp.Sa->has_ops()) return WhoIsPivot::Aggressor;
    // 否則預設 Victim
    return WhoIsPivot::Victim;
}

class DetectorPlanner {
public:
    vector<Detector> plan(const Fault& fault, const FPExpr& fp, const OrientationPlan& plan) const;
private:
    Detector makeReadBase(const FPExpr& fp) const;
    Detector makeComputeAsReadBase(CellScope scope, const FPExpr& fp, const OrientationPlan& plan) const;
    Detector makeComputeBase(CellScope scope, const FPExpr& fp, const OrientationPlan& plan) const;
    void setComputeTB(CellScope scope, const FPExpr& fp, const OrientationPlan& plan, Detector& d) const;
    vector<Detector> expandPosVariants(const OrientationPlan& plan, const Detector& base) const;
    bool needDetection(const FPExpr& fp) const;
    bool canComputeSetCi(const FPExpr& fp, CellScope scope) const;
    Val readExpect(const FPExpr& fp) const;
};

inline vector<Detector> DetectorPlanner::plan(const Fault& fault, const FPExpr& fp, const OrientationPlan& plan) const {
    vector<Detector> out;
    // 是否需要偵測
    if (!needDetection(fp)) {
        return out; // 空
    }
    // 根據 category 產生偵測器
    if (fault.category == Category::EitherReadOrCompute) {
        auto r = expandPosVariants(plan, makeReadBase(fp));
        out.insert(out.end(), r.begin(), r.end());
        auto c = expandPosVariants(plan, makeComputeAsReadBase(fault.cell_scope, fp, plan));
        out.insert(out.end(), c.begin(), c.end());
    }
    if (fault.category == Category::MustRead) {
        auto r = expandPosVariants(plan, makeReadBase(fp));
        out.insert(out.end(), r.begin(), r.end());
    }
    if (fault.category == Category::MustCompute) {
        auto c = expandPosVariants(plan, makeComputeBase(fault.cell_scope, fp, plan));
        out.insert(out.end(), c.begin(), c.end());
    }
    return out;
}

inline Detector DetectorPlanner::makeReadBase(const FPExpr& fp) const {
    Detector d; 
    d.detectOp.kind=OpKind::Read; 
    d.detectOp.value=readExpect(fp); 
    return d; 
}

inline Detector DetectorPlanner::makeComputeAsReadBase(CellScope scope, const FPExpr& fp, const OrientationPlan& plan) const {
    Detector d; 
    d.detectOp.kind=OpKind::ComputeAnd; 
    setComputeTB(scope, fp, plan, d); 
    d.detectOp.C_M=Val::One; 
    return d; 
}

inline Detector DetectorPlanner::makeComputeBase(CellScope scope, const FPExpr& fp, const OrientationPlan& plan) const {
    Detector d; 
    d.detectOp.kind=OpKind::ComputeAnd; 
    setComputeTB(scope, fp, plan, d); 
    Val m=Val::X; 
    for(auto it=fp.Sv.ops.rbegin(); it!=fp.Sv.ops.rend(); ++it){ 
        if(it->kind==OpKind::ComputeAnd){ 
            m=it->C_M; 
            break; 
        } 
    } 
    d.detectOp.C_M=m; 
    return d; 
}

inline void DetectorPlanner::setComputeTB(CellScope scope, const FPExpr& fp, const OrientationPlan& plan, Detector& d) const {
    if (canComputeSetCi(fp, scope)) {
            const Val sa_ci = fp.Sa ? fp.Sa->Ci.value_or(Val::X) : Val::X; 
            if (plan.group==OrientationGroup::A_LT_V) d.detectOp.C_T = sa_ci; 
            if (plan.group==OrientationGroup::A_GT_V) d.detectOp.C_B = sa_ci; 
            d.has_set_Ci=true; 
    }
}

inline vector<Detector> DetectorPlanner::expandPosVariants(const OrientationPlan& plan, const Detector& base) const {
    // 若 pivot 非 Aggressor，# 不受影響，僅回傳單一變體
    if (plan.pivot != WhoIsPivot::Aggressor) {
        Detector d = base;
        d.pos = PositionMark::Adjacent; // #
        d.order = Detector::AddrOrder::None;
        return { d };
    }
    // pivot 為 Aggressor：針對 ^ 與 ;，各產生 a_first 與 v_first 兩種，共四種
    vector<Detector> out;
    {
        Detector d = base; d.pos = PositionMark::SameElementHead; 
        d.order = (plan.group == OrientationGroup::A_LT_V) ? Detector::AddrOrder::Assending : Detector::AddrOrder::Decending; 
        out.push_back(d);
    }
    {
        Detector d = base; d.pos = PositionMark::SameElementHead; 
        d.order = (plan.group == OrientationGroup::A_LT_V) ? Detector::AddrOrder::Assending : Detector::AddrOrder::Decending;
        out.push_back(d);
    }
    {
        Detector d = base; d.pos = PositionMark::NextElementHead; 
        d.order = (plan.group == OrientationGroup::A_LT_V) ? Detector::AddrOrder::Assending : Detector::AddrOrder::Decending; 
        out.push_back(d);
    }
    {
        Detector d = base; d.pos = PositionMark::NextElementHead; 
        d.order = (plan.group == OrientationGroup::A_LT_V) ? Detector::AddrOrder::Assending : Detector::AddrOrder::Decending; 
        out.push_back(d);
    }
    return out;
}

// 是否需要detection
inline bool DetectorPlanner::needDetection(const FPExpr& fp) const {
    if (fp.R.RD.has_value() && fp.Sv.last_D.has_value()) {
        if (fp.R.RD.value() != Val::X && fp.Sv.last_D.value() != Val::X && fp.R.RD.value() != fp.Sv.last_D.value()) return false;
    }
    return true;
}

// 是否允許偵測端 Compute 設置 Top Ci和 Bottom Ci
inline bool DetectorPlanner::canComputeSetCi(const FPExpr& fp, CellScope scope) const {
    if (fp.s_has_any_op) {
        // 若 Op 含有 Read/Write，則不可設置 Ci
        for (const auto& op : fp.Sv.ops) {
            if (op.kind == OpKind::Read || op.kind == OpKind::Write) return false;
        }
        if (fp.Sa.has_value()) {
            for (const auto& op : fp.Sa->ops) {
                if (op.kind == OpKind::Read || op.kind == OpKind::Write) return false;
            }
        }
    }
    if (scope == CellScope::TwoCellRowAgnostic) return false; // row-agnostic 不經 compute 傳 Ci
    if (scope == CellScope::TwoCellSameRow) return false; // same row 不經 compute 傳 Ci
    return true; // cross-row 可經 compute 傳 Ci
}

inline Val DetectorPlanner::readExpect(const FPExpr& fp) const {
    if (fp.Sv.last_D.has_value() && fp.Sv.last_D.value() != Val::X) return fp.Sv.last_D.value();
    return Val::X;
}



class StateAssembler {
public:
    // 根據方向與 FPExpr 產生 CrossState
    CrossState assemble(const FPExpr& fp, const OrientationPlan& plan, const Detector& detector) const;
    vector<Op> ops_before_detect(const FPExpr& fp, Category category) const;
private:
    void fill_pivot(CrossState& state, const OrientationPlan& plan, const FPExpr& fp) const;
    void fill_non_pivot(CrossState& state, const OrientationPlan& plan, const FPExpr& fp, const Detector& detector) const;
};

inline CrossState StateAssembler::assemble(const FPExpr& fp, const OrientationPlan& plan, const Detector& detector) const {
    CrossState state; // default all X
    (void)fill_pivot(state, plan, fp);
    fill_non_pivot(state, plan, fp, detector);
    state.enforceDCrule();
    return state;
}

inline vector<Op> StateAssembler::ops_before_detect(const FPExpr& fp, Category category) const {
    vector<Op> out;
    if (fp.Sa.has_value()) {
        out.insert(out.end(), fp.Sa->ops.begin(), fp.Sa->ops.end());
    }
    out.insert(out.end(), fp.Sv.ops.begin(), fp.Sv.ops.end());
    if (category == Category::MustCompute) {
        auto erase_compute = [](vector<Op>& v){
            v.erase(std::remove_if(v.begin(), v.end(),
                    [](const Op& o){ return o.kind==OpKind::ComputeAnd; }), v.end());
        };
        erase_compute(out);
    }
    return out;
}

inline void StateAssembler::fill_pivot(CrossState& state, const OrientationPlan& plan, const FPExpr& fp) const {
    DC* pivotDC = &state.A2_CAS; // pivot slot 固定為 A2
    if (!pivotDC) throw runtime_error("fill_pivot: cannot determine pivot slot");
    if (plan.pivot == WhoIsPivot::Victim) {
        // Victim
        pivotDC->D = fp.Sv.pre_D.value_or(Val::X);
        pivotDC->C = fp.Sv.Ci.value_or(Val::X);
    } else if (plan.pivot == WhoIsPivot::Aggressor) {
        // Aggressor
        if (!fp.Sa.has_value()) throw runtime_error("fill_pivot: Aggressor pivot requires Sa");
        pivotDC->D = fp.Sa->pre_D.value_or(Val::X);
        pivotDC->C = fp.Sa->Ci.value_or(Val::X);
    }
}

inline void StateAssembler::fill_non_pivot(CrossState& state, const OrientationPlan& plan, const FPExpr& fp,
                                           const Detector& detector) const {
    for (Slot slot : plan.nonPivotSlots) {
        DC& ref = pick(state, slot);

        if (plan.pivot == WhoIsPivot::Victim) {
            ref.D = fp.Sa->pre_D.value_or(Val::X);
            if (detector.detectOp.kind == OpKind::ComputeAnd && detector.has_set_Ci) {
                // 若偵測器是 Compute 且有設置 Top/Bottom Ci，則非 pivot Aggressor 的 Ci 為偵測器的 Ci
                ref.C = Val::X;
            } else {
                ref.C = fp.Sa->Ci.value_or(Val::X);
            }
        } else if (plan.pivot == WhoIsPivot::Aggressor) {
            // 非 pivot 為 Victim
            ref.D = fp.Sv.pre_D.value_or(Val::X);
            ref.C = fp.Sv.Ci.value_or(Val::X);
        }
    }
}

class TPGenerator {
public:
    vector<TestPrimitive> generate(const Fault& fault);
private:
    TestPrimitive assemble_tp(const Fault& fault, const size_t fp_index, const OrientationPlan& plan, 
                              const Detector& detector) const;
    OrientationSelector orientation_selector_;
    DetectorPlanner detector_planner_;
    StateAssembler state_assembler_;
};

inline vector<TestPrimitive> TPGenerator::generate(const Fault& fault) {
    vector<TestPrimitive> out;
    for (size_t i = 0; i < fault.primitives.size(); ++i) {
        const auto& fp = fault.primitives[i];
        auto plans = orientation_selector_.plans(fault.cell_scope, fp);
        for (const auto& plan : plans) {
            auto detectors = detector_planner_.plan(fault, fp, plan);
            if (detectors.empty()) {
                // 不需要偵測器，仍然產生一個 TP，但標記為不需要偵測
                TestPrimitive tp = assemble_tp(fault, i, plan, Detector{});
                out.push_back(tp);
            } else {
                for (const auto& detector : detectors) {
                    out.push_back(assemble_tp(fault, i, plan, detector));
                }
            }
        }
    }
    return out;
}

inline TestPrimitive TPGenerator::assemble_tp(const Fault& fault, const size_t fp_index,
                                             const OrientationPlan& plan, const Detector& detector) const {
    TestPrimitive tp;
    tp.parent_fault_id = fault.fault_id;
    tp.parent_fp_index = fp_index;
    tp.group = plan.group;
    tp.state = state_assembler_.assemble(fault.primitives[fp_index], plan, detector);
    tp.ops_before_detect = state_assembler_.ops_before_detect(fault.primitives[fp_index], fault.category);
    tp.detector = detector;
    tp.F_has_value = fault.primitives[fp_index].F.FD.has_value() && fault.primitives[fp_index].F.FD.value() != Val::X;
    tp.R_has_value = fault.primitives[fp_index].R.RD.has_value() && fault.primitives[fp_index].R.RD.value() != Val::X;
    tp.C_has_value = fault.primitives[fp_index].C.Co.has_value() && fault.primitives[fp_index].C.Co.value() != Val::X;
    return tp;
}
