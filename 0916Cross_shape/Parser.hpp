// Parser.hpp — header-only faults.json 解析器
// 說明:
// - 介面（宣告）與實作（定義）皆置於本檔，並以區塊註解區隔
// - 僅使用精確 using；不使用 `using namespace std;`
// - 需要 nlohmann/json.hpp（系統或專案提供），Ubuntu 可安裝套件：nlohmann-json3-dev

#pragma once

#include <cstdint>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "nlohmann/json.hpp"

// ---- 精確 using（避免 using namespace） ----
using nlohmann::json;
using std::nullopt;
using std::optional;
using std::runtime_error;
using std::size_t;
using std::string;
using std::vector;

/* ==========================================================
 * 介面區（宣告）
 * ========================================================== */

// 與 JSON 的 "cell_scope" 對應
enum class CellScope {
	Single,         // "single cell"
	TwoRowAgnostic, // "two cell (row-agnostic)"
	TwoCrossRow     // "two cell cross row"
};

// S.init 的單一欄位（Ci 或 D），用 optional<int> 表示 {0,1,未指定}
struct InitVal {
	optional<int> Ci; // 0/1/未指定
	optional<int> D;  // 0/1/未指定
};

// 寫操作或計算操作：'W' 或 'C' + 0/1
struct Operation {
	char op; // 'W' 或 'C'
	int  val; // 0 或 1
};

// S 區塊（本階段關注 aggressor/victim 的 init 與 ops）
struct SSpec {
	InitVal aggressor;
	InitVal victim;
	vector<Operation> aggressor_ops;
	vector<Operation> victim_ops;
};

// FaultPrimitive：保留原字串、S 與偵測欄位
struct FaultPrimitive {
	string original; // 例如 "< 1𝐷/0𝐷/− >"
	SSpec  S;
	optional<int> FD; // 0/1/-
	optional<int> FR; // 0/1/-
	optional<int> FC; // 0/1/-
};

// Fault：單一 fault 的結構化表示
struct Fault {
	string fault_id;
	string category;
	CellScope cell_scope;
	vector<FaultPrimitive> primitives;
};

// 解析器
class FaultsParser {
public:
	vector<Fault> parse_file(const string& path) const; // 讀檔並解析
	vector<Fault> parse_json(const json& root) const;    // 直接解析 json 物件

private:
	// 輔助：
	static string read_all(const string& path);
	static CellScope parse_cell_scope(const string& s);
	static optional<int> parse_opt_bit(const json& j);    // 接受 null/"-"/"0"/"1"/0/1/bool
	static optional<int> parse_opt_bit_str(const string& s);
	static Operation parse_op_token(const string& tok);   // 解析 "W1" / "W0" / "C1" / "C0"
};

/* ==========================================================
 * 實作區（定義）
 * ========================================================== */

inline string FaultsParser::read_all(const string& path) {
	std::ifstream ifs(path, std::ios::binary);
	if (!ifs) throw runtime_error("Cannot open file: " + path);
	std::string content;
	ifs.seekg(0, std::ios::end);
	content.resize(static_cast<size_t>(ifs.tellg()));
	ifs.seekg(0, std::ios::beg);
	ifs.read(&content[0], static_cast<std::streamsize>(content.size()));
	return content;
}

inline CellScope FaultsParser::parse_cell_scope(const string& s) {
	if (s == "single cell")               return CellScope::Single;
	if (s == "two cell (row-agnostic)")   return CellScope::TwoRowAgnostic;
	if (s == "two cell cross row")        return CellScope::TwoCrossRow;
	throw runtime_error("Unknown cell_scope: " + s);
}

inline optional<int> FaultsParser::parse_opt_bit_str(const string& s0) {
	// 支援 "-" 表示未指定
	string s = s0;
	// 修剪空白
	while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
	size_t p = 0; while (p < s.size() && (s[p] == ' ' || s[p] == '\t')) ++p;
	if (p > 0) s = s.substr(p);

	if (s == "-" || s == "" ) return nullopt;
	if (s == "0") return 0;
	if (s == "1") return 1;
	throw runtime_error("Bit string must be '-'/'0'/'1', got: " + s);
}

inline optional<int> FaultsParser::parse_opt_bit(const json& j) {
	if (j.is_null()) return nullopt;
	if (j.is_boolean()) return j.get<bool>() ? 1 : 0;
	if (j.is_number_integer()) {
		const int v = j.get<int>();
		if (v == 0 || v == 1) return v;
		throw runtime_error("Bit integer must be 0/1, got: " + std::to_string(v));
	}
	if (j.is_string()) {
		return parse_opt_bit_str(j.get<string>());
	}
	throw runtime_error("Unsupported JSON type for bit");
}

inline Operation FaultsParser::parse_op_token(const string& tok0) {
    // 接受大小寫，例如 "W1"、"w0"、"C1"、"c0"、"R0"、"r1"
    if (tok0.size() < 2) throw runtime_error("Invalid op token: " + tok0);
    char op = tok0[0];
    if (op >= 'a' && op <= 'z') op = static_cast<char>(op - 'a' + 'A');
    const char c = tok0.back();
    if (op != 'W' && op != 'C' && op != 'R') throw runtime_error("Unknown op type (expect W/C/R): " + tok0);
    if (c != '0' && c != '1')   throw runtime_error("Op value must be 0/1: " + tok0);
    return Operation{op, c - '0'};
}

inline vector<Fault> FaultsParser::parse_file(const string& path) const {
	const string text = read_all(path);
	json root = json::parse(text);
	return parse_json(root);
}

inline vector<Fault> FaultsParser::parse_json(const json& root) const {
	if (!root.is_array()) {
		throw runtime_error("Top-level JSON must be an array");
	}

	vector<Fault> faults;
	faults.reserve(root.size());

	for (size_t i = 0; i < root.size(); ++i) {
		const json& jf = root[i];
		if (!jf.is_object()) throw runtime_error("Fault item is not an object at index " + std::to_string(i));

		Fault f;
		// fault_id
		if (!jf.contains("fault_id") || !jf["fault_id"].is_string())
			throw runtime_error("fault_id missing or not a string at index " + std::to_string(i));
		f.fault_id = jf["fault_id"].get<string>();

		// category
		if (!jf.contains("category") || !jf["category"].is_string())
			throw runtime_error("category missing or not a string for fault: " + f.fault_id);
		f.category = jf["category"].get<string>();

		// cell_scope
		if (!jf.contains("cell_scope") || !jf["cell_scope"].is_string())
			throw runtime_error("cell_scope missing or not a string for fault: " + f.fault_id);
		f.cell_scope = parse_cell_scope(jf["cell_scope"].get<string>());

		// fault_primitives
		if (!jf.contains("fault_primitives") || !jf["fault_primitives"].is_array())
			throw runtime_error("fault_primitives missing or not an array for fault: " + f.fault_id);

		const json& jprims = jf["fault_primitives"];
		f.primitives.reserve(jprims.size());

		for (size_t k = 0; k < jprims.size(); ++k) {
			const json& jp = jprims[k];
			if (!jp.is_object()) throw runtime_error("primitive is not an object in fault: " + f.fault_id);

			FaultPrimitive fp{};

			// original
			if (!jp.contains("original") || !jp["original"].is_string())
				throw runtime_error("primitive.original missing or not string in fault: " + f.fault_id);
			fp.original = jp["original"].get<string>();

			// S
			if (!jp.contains("S") || !jp["S"].is_object())
				throw runtime_error("primitive.S missing or not object in fault: " + f.fault_id);
			const json& jS = jp["S"];

			// aggressor
			if (!jS.contains("aggressor") || !jS["aggressor"].is_object())
				throw runtime_error("S.aggressor missing or not object in fault: " + f.fault_id);
			const json& jagg = jS["aggressor"];
			if (!jagg.contains("init") || !jagg["init"].is_object())
				throw runtime_error("S.aggressor.init missing or not object in fault: " + f.fault_id);
			const json& jaggi = jagg["init"];
			fp.S.aggressor.Ci = jaggi.contains("Ci") ? parse_opt_bit(jaggi["Ci"]) : nullopt;
			fp.S.aggressor.D  = jaggi.contains("D")  ? parse_opt_bit(jaggi["D"])  : nullopt;
			if (jagg.contains("ops")) {
				if (!jagg["ops"].is_array()) throw runtime_error("S.aggressor.ops must be array in fault: " + f.fault_id);
				for (const auto& jt : jagg["ops"]) {
					if (!jt.is_string()) throw runtime_error("S.aggressor.ops item must be string in fault: " + f.fault_id);
					fp.S.aggressor_ops.push_back(parse_op_token(jt.get<string>()));
				}
			}

			// victim
			if (!jS.contains("victim") || !jS["victim"].is_object())
				throw runtime_error("S.victim missing or not object in fault: " + f.fault_id);
			const json& jvic = jS["victim"];
			if (!jvic.contains("init") || !jvic["init"].is_object())
				throw runtime_error("S.victim.init missing or not object in fault: " + f.fault_id);
			const json& jvici = jvic["init"];
			fp.S.victim.Ci = jvici.contains("Ci") ? parse_opt_bit(jvici["Ci"]) : nullopt;
			fp.S.victim.D  = jvici.contains("D")  ? parse_opt_bit(jvici["D"])  : nullopt;
			if (jvic.contains("ops")) {
				if (!jvic["ops"].is_array()) throw runtime_error("S.victim.ops must be array in fault: " + f.fault_id);
				for (const auto& jt : jvic["ops"]) {
					if (!jt.is_string()) throw runtime_error("S.victim.ops item must be string in fault: " + f.fault_id);
					fp.S.victim_ops.push_back(parse_op_token(jt.get<string>()));
				}
			}

			// FD/FR/FC （可為 "-" / "0" / "1"）
			auto parse_opt_attr = [&](const char* key) -> optional<int> {
				if (!jp.contains(key)) return nullopt; // 若缺，視為未指定
				const json& jv = jp.at(key);
				if (jv.is_null()) return nullopt;
				if (jv.is_string()) return parse_opt_bit_str(jv.get<string>());
				if (jv.is_boolean()) return jv.get<bool>() ? 1 : 0;
				if (jv.is_number_integer()) {
					const int vi = jv.get<int>();
					if (vi == 0 || vi == 1) return vi;
					throw runtime_error(string("Attribute ") + key + " integer must be 0/1");
				}
				throw runtime_error(string("Unsupported JSON type for attribute ") + key);
			};

			fp.FD = parse_opt_attr("FD");
			fp.FR = parse_opt_attr("FR");
			fp.FC = parse_opt_attr("FC");

			f.primitives.push_back(std::move(fp));
		}

		faults.push_back(std::move(f));
	}

	return faults;
}

