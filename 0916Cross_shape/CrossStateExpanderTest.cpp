// g++ -std=c++20 -O2 -Wall -Wextra -pedantic CrossStateExpanderTest.cpp -o CrossStateTest
// ./CrossStateTest

#include <iostream>
#include <string>
#include <vector>
#include "Parser.hpp"
#include "CrossStateExpander.hpp"

using std::cerr;
using std::cout;
using std::endl;
using std::string;
using std::vector;

static void dump(const Fault* fault, const CrossState& st) {
    cout << fault->fault_id << "(" << fault->primitives.front().original << ") " << st.case_name << ": ";
    for (size_t i = 0; i < st.cells.size(); ++i) {
        auto &c = st.cells[i];
        auto toCh = [](int v){ return v==-1?'X':(v==0?'0':'1'); };
        cout << "[ " << toCh(c.D) << " " << toCh(c.C) << " ]";
    }
    cout << '\n';
}

static void dump_choose(const vector<Fault>& faults, const vector<CrossState>& states, CrossStateExpander& exp) {
// 印出所有 faults 及其 primitive
    while (true) {
        cout << "\nSelect a fault to view details:\n";
        for (size_t i = 0; i < faults.size(); ++i) {
        cout << i + 1 << ". " << faults[i].fault_id
                 << " (" << faults[i].primitives.front().original << ")\n";
        }
        cout << faults.size() + 1 << ". Exit\n";
        cout << "Enter choice (1-" << faults.size() + 1 << "): ";
        int choice = 0;
        std::cin >> choice;

        if (choice == faults.size() + 1) {
        cout << "Exiting.\n";
        break;
        }
        if (choice < 1 || choice > faults.size()) {
        cout << "Invalid choice.\n";
        continue;
        }

        const Fault* selected_fault = &faults[choice - 1];
        auto selected_states = exp.expand(selected_fault->primitives.front(), selected_fault->cell_scope);

        cout << "\nDumping selected fault states:\n";
        for (const auto& st : selected_states) {
        dump(selected_fault, st);
        }
    }
}

int main() {
    try {
        FaultsParser parser;
        auto faults = parser.parse_file("faults.json");
        if (faults.empty()) { cerr << "No faults.json data" << endl; return 2; }

        CrossStateExpander exp;

        // 取一個 single cell fault，例如 SA0
        const Fault* single = nullptr; for (auto &f: faults) if (f.cell_scope==CellScope::Single) { single=&f; break; }
        if (!single) { cerr << "No single cell fault" << endl; return 3; }
        auto s_states = exp.expand(single->primitives.front(), single->cell_scope);
        if (s_states.size()!=1) { cerr << "Single cell expected 1 state" << endl; return 4; }

        // 取一個 row-agnostic fault，例如 CFid(↓,0)  (期望 aggressor 有 ops → pivot=aggressor)
        const Fault* rowAg = nullptr; for (auto &f: faults) if (f.cell_scope==CellScope::TwoRowAgnostic) { rowAg=&f; break; }
        if (!rowAg) { cerr << "No row-agnostic fault" << endl; return 5; }
        auto r_states = exp.expand(rowAg->primitives.front(), rowAg->cell_scope);
        if (r_states.size()!=2) { cerr << "Row-agnostic expected 2 states (L,R)" << endl; return 6; }
        // Pivot 驗證：若 aggressor 有 ops，pivot cell( index2 ) 應映射 aggressor init D/C
        bool aggressor_has_ops = !rowAg->primitives.front().S.aggressor_ops.empty();
        if (aggressor_has_ops) {
            int agD = rowAg->primitives.front().S.aggressor.D ? (*rowAg->primitives.front().S.aggressor.D) : -1;
            int gotD = r_states[0].cells[2].D; // 任一 case pivot 相同
            if (agD==-1) agD = -1; // normalize
            if ( (agD==-1 && gotD!=-1) || (agD!= -1 && gotD!=agD) ) {
                cerr << "Pivot D mismatch: expected=" << (agD==-1?'X':('0'+agD)) << " got=" << (gotD==-1?'X':('0'+gotD)) << endl;
                return 61;
            }
        }

        // 取一個 cross-row fault，例如 CIDDB(0,0)
        const Fault* cross = nullptr; for (auto &f: faults) if (f.cell_scope==CellScope::TwoCrossRow) { cross=&f; break; }
        if (!cross) { cerr << "No cross-row fault" << endl; return 7; }
        auto c_states = exp.expand(cross->primitives.front(), cross->cell_scope);
        if (c_states.size()!=2) { cerr << "Cross-row expected 2 states (Top,Bottom) after refactor" << endl; return 8; }
        bool cross_ag_ops = !cross->primitives.front().S.aggressor_ops.empty();
        if (cross_ag_ops) {
            int agD = cross->primitives.front().S.aggressor.D ? (*cross->primitives.front().S.aggressor.D) : -1;
            if ( (agD!=-1) && (c_states[0].cells[2].D!=agD || c_states[1].cells[2].D!=agD) ) {
                cerr << "Cross-row pivot D mismatch" << endl; return 81;
            }
        }

        // 簡單 dump（人工檢視）
        for (auto &st: s_states) dump(single, st);
        for (auto &st: r_states) dump(rowAg, st);
        for (auto &st: c_states) dump(cross, st);

        cout << "All CrossState tests passed basic checks." << endl;
        dump_choose(faults, c_states, exp);
        return 0;
    } catch (const std::exception& e) {
        cerr << "Exception: " << e.what() << endl;
        return 1;
    }
}
