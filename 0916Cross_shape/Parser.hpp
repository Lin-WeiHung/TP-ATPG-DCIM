// Parser.hpp - Minimal fault list parser
// 單一函式 parse_file 回傳 vector<FaultEntry>
// 假設一定存在並可使用 nlohmann/json.hpp

#pragma once

#include <string>
#include <vector>
#include <fstream>

#include "nlohmann/json.hpp"

// ---- 精確 using ----
using std::string;
using std::vector;
using std::ifstream;

struct FaultEntry {
    string fault_id;                 // "SA0"
    string category;                 // "either_read_or_compute" | ...
    string cell_scope;               // e.g. "single cell"
    vector<string> fault_primitives;               // 原始 primitive 字串陣列
};

// 直接解析檔案；失敗時丟出 std::runtime_error
inline vector<FaultEntry> parse_file(const string& path) {
    ifstream ifs(path);
    if (!ifs) {
        throw std::runtime_error("Cannot open file: " + path);
    }
    nlohmann::json j; 
    ifs >> j;
    if (!j.is_array()) {
        throw std::runtime_error("Top-level JSON is not an array");
    }
    vector<FaultEntry> out;
    out.reserve(j.size());
    for (const auto& item : j) {
        FaultEntry fe;
        fe.fault_id = item.at("fault_id").get<string>();
        fe.category = item.at("category").get<string>();
        fe.cell_scope = item.at("cell_scope").get<string>();
        for (const auto& p : item.at("fault_primitives")) {
            fe.fault_primitives.push_back(p.get<string>());
        }
        out.push_back(std::move(fe));
    }
    return out;
}


