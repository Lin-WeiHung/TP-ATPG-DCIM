#include <iostream>
#include <string>
#include <vector>
#include "Parser.hpp"

using std::cerr;
using std::cin;
using std::cout;
using std::endl;
using std::string;
using std::vector;

int main() {
    try {
        FaultsParser p;
        const string path = "faults.json"; // 與執行目錄同一層的 0916Cross_shape/faults.json
        auto faults = p.parse_file(path);
        cout << "Loaded faults: " << faults.size() << endl;
        if (faults.empty()) {
            cerr << "No faults parsed" << endl;
            return 2;
        }

        // 簡單驗證：找 SA0 與第一個 primitive 的欄位
        const Fault* sa0 = nullptr;
        for (const auto& f : faults) {
            if (f.fault_id == "SA0") { sa0 = &f; break; }
        }
        if (!sa0) {
            cerr << "SA0 not found" << endl; return 3;
        }
        if (sa0->category != "either_read_or_compute") {
            cerr << "SA0 category mismatch: " << sa0->category << endl; return 4;
        }
        if (sa0->cell_scope != CellScope::Single) {
            cerr << "SA0 cell_scope mismatch" << endl; return 5;
        }
        if (sa0->primitives.empty()) {
            cerr << "SA0 primitives empty" << endl; return 6;
        }
        const auto& prim = sa0->primitives.front();
        if (!prim.S.victim.D.has_value() || prim.S.victim.D.value() != 1) {
            cerr << "SA0 victim.D expected 1" << endl; return 7;
        }
        if (prim.S.victim_ops.size() != 0) {
            cerr << "SA0 victim_ops expected 0" << endl; return 8;
        }

        // 再檢查一個含 ops 的：TFu（victim W1）
        const Fault* tfu = nullptr;
        for (const auto& f : faults) if (f.fault_id == "TFu") { tfu = &f; break; }
        if (!tfu) { cerr << "TFu not found" << endl; return 9; }
        if (tfu->primitives.empty()) { cerr << "TFu primitives empty" << endl; return 10; }
        const auto& p2 = tfu->primitives.front();
        if (p2.S.victim_ops.size() != 1 || p2.S.victim_ops[0].op != 'W' || p2.S.victim_ops[0].val != 1) {
            cerr << "TFu victim_ops expected [W1]" << endl; return 11;
        }

        cout << "All checks passed." << endl;
        // 印出每種 cell_scope 與 category 各一個 fault的所有資訊
        // 先印出編號和名字
        cout << "Faults list:" << endl;
        for (size_t i = 0; i < faults.size(); ++i) {
            cout << i << ": " << faults[i].fault_id << endl;
        }

        cout << "Enter fault index to display: ";
        size_t idx;
        cin >> idx;
        if (idx >= faults.size()) {
            cerr << "Invalid index." << endl;
        } else {
            const auto& f = faults[idx];
            cout << "Fault ID: " << f.fault_id << endl;
            cout << "  Category: " << f.category << endl;
            cout << "  Cell Scope: ";
            switch (f.cell_scope) {
            case CellScope::Single: cout << "Single"; break;
            case CellScope::TwoRowAgnostic: cout << "TwoRowAgnostic"; break;
            case CellScope::TwoCrossRow: cout << "TwoCrossRow"; break;
            }
            cout << endl;
            cout << "  Primitives: " << f.primitives.size() << endl;
            for (const auto& prim : f.primitives) {
            cout << "    Original: " << prim.original << endl;
            cout << "    S:" << endl;
            cout << "      Aggressor Init: Ci=";
            if (prim.S.aggressor.Ci.has_value()) cout << prim.S.aggressor.Ci.value(); else cout << "N/A";
            cout << ", D=";
            if (prim.S.aggressor.D.has_value()) cout << prim.S.aggressor.D.value(); else cout << "N/A";
            cout << endl;
            cout << "      Aggressor Ops: ";
            if (prim.S.aggressor_ops.empty()) { cout << "None"; }
            else {
                for (const auto& op : prim.S.aggressor_ops) {
                cout << op.op << op.val << " ";
                }
            }
            cout << endl;
            cout << "      Victim Init: Ci=";
            if (prim.S.victim.Ci.has_value()) cout << prim.S.victim.Ci.value(); else cout << "N/A";
            cout << ", D=";
            if (prim.S.victim.D.has_value()) cout << prim.S.victim.D.value(); else cout << "N/A";
            cout << endl;
            cout << "      Victim Ops: ";
            if (prim.S.victim_ops.empty()) { cout << "None"; }
            else {
                for (const auto& op : prim.S.victim_ops) {
                cout << op.op << op.val << " ";
                }
            }
            cout << endl;
            cout << "    FD: ";
            if (prim.FD.has_value()) cout << prim.FD.value(); else cout << "N/A";
            cout << ", FR: ";
            if (prim.FR.has_value()) cout << prim.FR.value(); else cout << "N/A";
            cout << ", FC: ";
            if (prim.FC.has_value()) cout << prim.FC.value(); else cout << "N/A";
            cout << endl;
            }
        }

        return 0;
    } catch (const std::exception& e) {
        cerr << "Exception: " << e.what() << endl;
        return 1;
    }
}
