// g++ -std=c++20 -Wall -Wextra -Iinclude FaultBrowser.cpp -o fb
// ./fb
#include <iostream>
#include <string>
#include <vector>
#include <optional>
#include <stdexcept>

#include "FpParserAndTpGen.hpp"

using std::cout;
using std::cin;
using std::endl;
using std::string;
using std::vector;

static const char* val2s(Val v){
    switch(v){
        case Val::Zero: return "0";
        case Val::One:  return "1";
        case Val::X:    return "-";
    }
    return "?";
}

static void print_op(const Op& op, size_t idx){
    cout << "    op[" << idx << "] : ";
    switch(op.kind){
        case OpKind::Write:
            cout << "Write(" << val2s(op.value) << ")";
            break;
        case OpKind::Read:
            cout << "Read(" << val2s(op.value) << ")";
            break;
        case OpKind::ComputeAnd:
            cout << "ComputeAND(T=" << val2s(op.C_T) << ", M=" << val2s(op.C_M) << ", B=" << val2s(op.C_B) << ")";
            break;
    }
    cout << endl;
}

static void print_sspec(const char* name, const SSpec& s){
    cout << "  " << name << ":\n";
    cout << "    pre_D: " << (s.pre_D.has_value()? val2s(s.pre_D.value()) : "<none>") << "\n";
    cout << "    Ci   : " << (s.Ci.has_value()?    val2s(s.Ci.value())    : "<none>") << "\n";
    cout << "    last_D: "<< (s.last_D.has_value()? val2s(s.last_D.value()) : "<none>") << "\n";
    for(size_t i=0;i<s.ops.size();++i) print_op(s.ops[i], i);
}

static const char* scope2s(CellScope sc){
    switch(sc){
        case CellScope::SingleCell: return "single cell";
        case CellScope::TwoCellRowAgnostic: return "two cell (row-agnostic)";
        case CellScope::TwoCellSameRow: return "two cell same row";
        case CellScope::TwoCellCrossRow: return "two cell cross row";
    }
    return "?";
}
static const char* cat2s(Category c){
    switch(c){
        case Category::EitherReadOrCompute: return "either_read_or_compute";
        case Category::MustRead: return "must_read";
        case Category::MustCompute: return "must_compute";
    }
    return "?";
}

static void print_fault(const Fault& f, const vector<string>& rawFP){
    cout << "==== Fault ====" << endl;
    cout << "id      : " << f.fault_id << endl;
    cout << "category: " << cat2s(f.category) << endl;
    cout << "scope   : " << scope2s(f.cell_scope) << endl;
    cout << "primitives: " << f.primitives.size() << endl;
    for(size_t i=0;i<f.primitives.size();++i){
        const auto& p = f.primitives[i];
        cout << "- primitive[" << i << "]\n";
        cout << "  raw: " << (i < rawFP.size()? rawFP[i] : "<no raw>") << endl;
        if (p.Sa) print_sspec("Sa", *p.Sa);
        print_sspec("Sv", p.Sv);
        cout << "  F: " << (p.F.FD.has_value()? val2s(p.F.FD.value()) : "-") << endl;
        cout << "  R: " << (p.R.RD.has_value()? val2s(p.R.RD.value()) : "-") << endl;
        cout << "  C: " << (p.C.Co.has_value()? val2s(p.C.Co.value()) : "-") << endl;
        cout << "  s_has_any_op: " << (p.s_has_any_op? "true":"false") << endl;
    }
}

int main(){
    try{
        FaultsJsonParser parser;
        // 讀 0916Cross_shape/faults.json（四段式已升級為五段式）
        const string path = "../0916Cross_shape/faults.json";
        auto raw_list = parser.parse_file(path);

        if (raw_list.empty()){
            cout << "沒有 faults 可選" << endl;
            return 0;
        }

        while(true){
            // 列單
            cout << "\n===== Fault 選單 (輸入編號；exit 退出) =====\n";
            for (size_t i=0;i<raw_list.size();++i){
                cout << "  [" << i << "] " << raw_list[i].fault_id
                     << "  (" << raw_list[i].category << ", " << raw_list[i].cell_scope << ")" << endl;
            }
            cout << "> 請輸入編號或 'exit': ";
            string in; if(!std::getline(cin, in)) break;
            if (in == "exit" || in == "quit") break;
            if (in.empty()) continue;
            // 轉 index
            size_t idx = 0; bool ok = false;
            try{
                size_t pos=0; long long v = std::stoll(in, &pos); ok = (pos == in.size() && v >= 0);
                idx = static_cast<size_t>(v);
            }catch(...){ ok = false; }
            if (!ok || idx >= raw_list.size()){
                cout << "輸入不合法或超出範圍" << endl; continue;
            }

            // normalize 與列印
            FaultNormalizer norm;
            Fault nf = norm.normalize(raw_list[idx]);
            print_fault(nf, raw_list[idx].fp_raw);
        }

    }catch(const std::exception& ex){
        cout << "Exception: " << ex.what() << endl;
        return 1;
    }
    return 0;
}
