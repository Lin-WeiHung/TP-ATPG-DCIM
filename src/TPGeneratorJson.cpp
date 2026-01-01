// TPGeneratorJson.cpp
// 將 S_C_faults.json 透過 TPGenerator 產生 TP 展開資訊並輸出為 JSON
// 編譯：g++ -std=c++20 -Wall -Wextra -Iinclude src/TPGeneratorJson.cpp -o build/tp_generator_json
// 執行：./build/tp_generator_json

#include <iostream>
#include <fstream>
#include <string>
#include <vector>

#include "FpParserAndTpGen.hpp"
#include "nlohmann/json.hpp"

using json = nlohmann::json;

// === 輔助函式：將 enum 轉為字串 ===
std::string val_to_string(Val v) {
    switch (v) {
        case Val::Zero: return "0";
        case Val::One:  return "1";
        case Val::X:    return "X";
    }
    return "?";
}

std::string val_to_string(std::optional<Val> v) {
    if (!v.has_value()) return "X";
    return val_to_string(v.value());
}

std::string opkind_to_string(OpKind k) {
    switch (k) {
        case OpKind::Write:      return "Write";
        case OpKind::Read:       return "Read";
        case OpKind::ComputeAnd: return "ComputeAnd";
    }
    return "?";
}

std::string orientation_to_string(OrientationGroup g) {
    switch (g) {
        case OrientationGroup::Single: return "Single";
        case OrientationGroup::A_LT_V: return "A<V";
        case OrientationGroup::A_GT_V: return "A>V";
    }
    return "?";
}

std::string position_to_string(PositionMark p) {
    switch (p) {
        case PositionMark::Adjacent:        return "#";
        case PositionMark::SameElementHead: return "^";
        case PositionMark::NextElementHead: return ";";
    }
    return "?";
}

std::string addr_order_to_string(Detector::AddrOrder o) {
    switch (o) {
        case Detector::AddrOrder::None:      return "None";
        case Detector::AddrOrder::Assending: return "Ascending";
        case Detector::AddrOrder::Decending: return "Descending";
    }
    return "?";
}

std::string category_to_string(Category c) {
    switch (c) {
        case Category::EitherReadOrCompute: return "either_read_or_compute";
        case Category::MustRead:            return "must_read";
        case Category::MustCompute:         return "must_compute";
    }
    return "?";
}

std::string cell_scope_to_string(CellScope s) {
    switch (s) {
        case CellScope::SingleCell:         return "single_cell";
        case CellScope::TwoCellRowAgnostic: return "two_cell_row_agnostic";
        case CellScope::TwoCellSameRow:     return "two_cell_same_row";
        case CellScope::TwoCellCrossRow:    return "two_cell_cross_row";
    }
    return "?";
}

// === 將 Op 轉為 JSON ===
json op_to_json(const Op& op) {
    json j;
    j["kind"] = opkind_to_string(op.kind);
    if (op.kind == OpKind::Write || op.kind == OpKind::Read) {
        j["value"] = val_to_string(op.value);
    }
    if (op.kind == OpKind::ComputeAnd) {
        j["C_T"] = val_to_string(op.C_T);
        j["C_M"] = val_to_string(op.C_M);
        j["C_B"] = val_to_string(op.C_B);
    }
    return j;
}

// === 將 DC 轉為 JSON ===
json dc_to_json(const DC& dc) {
    return json{
        {"D", val_to_string(dc.D)},
        {"C", val_to_string(dc.C)}
    };
}

// === 將 CrossState 轉為 JSON ===
json cross_state_to_json(const CrossState& state) {
    return json{
        {"A0", dc_to_json(state.A0)},
        {"A1", dc_to_json(state.A1)},
        {"A2_CAS", dc_to_json(state.A2_CAS)},
        {"A3", dc_to_json(state.A3)},
        {"A4", dc_to_json(state.A4)}
    };
}

// === 將 Detector 轉為 JSON ===
json detector_to_json(const Detector& d) {
    json j;
    j["detect_op"] = op_to_json(d.detectOp);
    j["position"] = position_to_string(d.pos);
    j["has_set_Ci"] = d.has_set_Ci;
    j["addr_order"] = addr_order_to_string(d.order);
    return j;
}

// === 將 TestPrimitive 轉為 JSON ===
json tp_to_json(const TestPrimitive& tp) {
    json j;
    j["orientation_group"] = orientation_to_string(tp.group);
    j["cross_state"] = cross_state_to_json(tp.state);
    
    json ops_json = json::array();
    for (const auto& op : tp.ops_before_detect) {
        ops_json.push_back(op_to_json(op));
    }
    j["ops_before_detect"] = ops_json;
    
    j["detector"] = detector_to_json(tp.detector);
    
    return j;
}

// === 將 Fault 與其產生的 TPs 轉為 JSON ===
json fault_with_tps_to_json(const Fault& fault, const std::vector<TestPrimitive>& tps) {
    json j;
    j["fault_id"] = fault.fault_id;
    j["category"] = category_to_string(fault.category);
    j["cell_scope"] = cell_scope_to_string(fault.cell_scope);
    
    // 原始 primitives 數量
    j["primitives_count"] = fault.primitives.size();
    
    // 展開後的 TPs
    json tps_json = json::array();
    for (const auto& tp : tps) {
        tps_json.push_back(tp_to_json(tp));
    }
    j["generated_tps"] = tps_json;
    j["generated_tps_count"] = tps.size();
    
    return j;
}

int main(int argc, char* argv[]) {
    // 預設輸入輸出路徑
    std::string input_path = "input/S_C_faults.json";
    std::string output_path = "output/TP_Generated.json";
    
    // 可選：從命令列參數讀取
    if (argc >= 2) {
        input_path = argv[1];
    }
    if (argc >= 3) {
        output_path = argv[2];
    }
    
    std::cout << "=== TP Generator JSON ===" << std::endl;
    std::cout << "Input:  " << input_path << std::endl;
    std::cout << "Output: " << output_path << std::endl;
    
    try {
        // 1. 解析 JSON 檔案
        FaultsJsonParser parser;
        std::vector<RawFault> raw_faults = parser.parse_file(input_path);
        std::cout << "Parsed " << raw_faults.size() << " faults from JSON." << std::endl;
        
        // 2. 正規化並產生 TPs
        FaultNormalizer normalizer;
        TPGenerator generator;
        
        json output_json = json::array();
        size_t total_tps = 0;
        
        for (const RawFault& rf : raw_faults) {
            // 正規化
            Fault fault = normalizer.normalize(rf);
            
            // 產生 TPs
            std::vector<TestPrimitive> tps = generator.generate(fault);
            total_tps += tps.size();
            
            // 轉為 JSON
            output_json.push_back(fault_with_tps_to_json(fault, tps));
            
            std::cout << "  " << fault.fault_id 
                      << ": " << fault.primitives.size() << " primitives -> " 
                      << tps.size() << " TPs" << std::endl;
        }
        
        std::cout << "Total: " << total_tps << " TPs generated." << std::endl;
        
        // 3. 輸出 JSON
        std::ofstream ofs(output_path);
        if (!ofs) {
            throw std::runtime_error("Cannot open output file: " + output_path);
        }
        ofs << output_json.dump(2); // pretty print with 2-space indent
        ofs.close();
        
        std::cout << "Output written to: " << output_path << std::endl;
        std::cout << "=== Done ===" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
