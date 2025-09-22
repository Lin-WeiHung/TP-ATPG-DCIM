// g++ -std=c++20 -O2 -Wall -Wextra -pedantic CrossStateCoverTest.cpp -o CrossStateCoverTest
// ./CrossStateCoverTest

#include <iostream>
#include <vector>
#include <string>
#include <iomanip>
#include <sstream>
#include "CrossStateCoverSolver.hpp"

using std::cout; using std::cerr; using std::endl; using std::vector; using std::string;

static CrossCell mk(int d, int c) { return CrossCell{d,c}; }
static CrossState make_state(const string& name,
                             int D0,int C0,int D1,int C1,int D2,int C2,int D3,int C3,int D4,int C4) {
    CrossState st; st.case_name = name;
    st.cells[0]=mk(D0,C0); st.cells[1]=mk(D1,C1); st.cells[2]=mk(D2,C2); st.cells[3]=mk(D3,C3); st.cells[4]=mk(D4,C4);
    return st;
}

// 將 CrossState 顯示成 [D C] 序列
static string state_str(const CrossState& st) {
    auto enc=[](int v){ return v==-1? 'X':(v==0?'0':'1'); };
    std::ostringstream oss;
    for (int i=0;i<5;++i) {
        oss << "[ " << enc(st.cells[i].D) << ' ' << enc(st.cells[i].C) << " ]";
        if (i+1<5) oss << ' ';
    }
    return oss.str();
}

int main(){
    // Universe 狀態 (五個)
    CrossState SA0          = make_state("SA0",          -1,-1, -1,-1, 1,-1, -1,-1, -1,-1); // [ X X ][ X X ][ 1 X ][ X X ][ X X ]
    CrossState CFidL        = make_state("CFidL",        1,-1,  1,-1,  1,-1, -1,-1, -1,-1);  // [ 1 X ][ 1 X ][ 1 X ][ X X ][ X X ]
    CrossState CFidR        = make_state("CFidR",        -1,-1, -1,-1, 1,-1,  1,-1,  1,-1);  // [ X X ][ X X ][ 1 X ][ 1 X ][ 1 X ]
    CrossState CIDDBTop     = make_state("CIDDBTop",     -1,0,  -1,-1, 1,-1, -1,-1, -1,-1);  // [ X 0 ][ X X ][ 1 X ][ X X ][ X X ]
    CrossState CIDDBBottom  = make_state("CIDDBBottom",  -1,-1, -1,-1, 1,-1, -1,-1, -1,0);   // [ X X ][ X X ][ 1 X ][ X X ][ X 0 ]

    vector<CrossState> universe = {SA0, CFidL, CFidR, CIDDBTop, CIDDBBottom};

    // 使用者需求的兩個集合 (稍作調整以避免互相全覆蓋)
    // Set1 目標覆蓋: SA0, CFidL, CIDDBTop  (避免覆蓋 CFidR, CIDDBBottom)
    //   調整：將最後一格 C4 設為 1 以排除 CIDDBBottom (其 C4=0)
    CrossState Set1 = make_state("Set1", 1,0, 1,-1, 1,-1, 0,-1, 0,1); // [ 1 0 ][ 1 X ][ 1 X ][ 0 X ][ 0 1 ]
    // Set2 目標覆蓋: SA0, CFidR, CIDDBBottom  (避免覆蓋 CFidL (D0=1) 與 CIDDBTop (C0=0))
    //   調整：D0=D1=0, C0=1
    CrossState Set2 = make_state("Set2", 0,1, 0,-1, 1,-1, 1,-1, 1,0); // [ 0 1 ][ 0 X ][ 1 X ][ 1 X ][ 1 0 ]

    CrossStateCoverSolver solver;
    // 直接合成最小 general pattern 集合
    auto generalized = solver.synthesize_generalized_patterns(universe);

    cout << "Generalized pattern count: " << generalized.size() << '\n';
    for (size_t i=0;i<generalized.size();++i) {
        cout << "  GEN" << i << ": " << state_str(generalized[i]) << '\n';
    }
    cout << '\n';

    // 仍保留原本兩個 Set 測試：用 solver 驗證兩集合覆蓋 (示範舊流程)
    vector<vector<CrossState>> candidates = { {Set1}, {Set2} };
    auto result = solver.solve(universe, candidates);

    cout << "Universe (index -> pattern):\n";
    for (size_t i=0;i<universe.size();++i) {
        cout << "  U" << i << " (" << universe[i].case_name << "): " << state_str(universe[i]) << '\n';
    }
    cout << '\n';

    cout << "Candidates:\n";
    for (size_t si=0; si<candidates.size(); ++si) {
        cout << "  Set" << si << ": " << state_str(candidates[si][0]) << '\n';
    }
    cout << '\n';

    cout << "Solver chosen set count: " << result.chosen_sets.size() << '\n';
    cout << "Chosen sets: "; for (auto s: result.chosen_sets) cout << s << ' '; cout << "\n\n";
    for (size_t i=0;i<result.chosen_sets.size();++i) {
        cout << "Set" << result.chosen_sets[i] << " covers universe indices: ";
        for (size_t j=0;j<result.cover_report[i].size();++j) {
            cout << result.cover_report[i][j]; if (j+1<result.cover_report[i].size()) cout << ',';
        }
        cout << '\n';
    }
    if (!result.uncovered_indices.empty()) {
        cerr << "ERROR: uncovered universe indices: ";
        for (auto u: result.uncovered_indices) cerr << u << ' '; cerr << '\n';
        return 3;
    }

    // 驗證 generalized 是否能單獨覆蓋全部 (理論上 size=1)
    bool gen_ok = true;
    if (generalized.empty()) gen_ok = false; else {
        // 簡單覆蓋檢查：用 solver 的 unify 規則需要重用 normalize -> 快速重跑 solve
        // 將 generalized 作為 candidate sets
        vector<vector<CrossState>> genCand;
        for (auto &g: generalized) genCand.push_back({g});
        auto genRes = solver.solve(universe, genCand);
        if (genRes.chosen_sets.size() != generalized.size() || !genRes.uncovered_indices.empty()) gen_ok = false;
    }
    if (!gen_ok) {
        cerr << "Generalized pattern failed to cover all universe." << endl;
        return 5;
    }
    cout << "Generalized single pattern covers all universe (expected)." << endl;

    // 验證舊 set cover 還是兩集合（演示原設計）
    if (result.chosen_sets.size() == 2 && result.chosen_sets[0]==0 && result.chosen_sets[1]==1)
        cout << "Original two-set cover also valid." << endl;
    else
        cout << "Original handcrafted sets not minimal (generalization found 1)." << endl;

    return 0;
}
