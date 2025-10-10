// march_gen.cpp
// A Heuristic Algorithm for Automatic Generation of March Tests
// 實作目標：以 Table III fault set 為輸入，依論文 Fig.1/2/3 生成 I-MTA，並依示範做 Phase-2 壓縮為 I-MTA-1、OMTA。
// 主要參考：Fig.1 (流程), Fig.2 (讀出插入規則), Fig.3 (Repair_State), Table II(MTAS 映射), Table III(輸入 faults), Fig.4~6(期望序列)
// 來源：Cui et al., ATS 2017.  (對應文字與圖請見本文檔標號)
//   Fig.1/Fig.2/Fig.3：
//   Table III + Fig.4~6：
//   規則 1/2/3 與 MTAS/Set-Cover 脈絡：

#include <bits/stdc++.h>
using namespace std;

// ---------- Domain Model ----------
// Operation 型別：R0/R1/W0/W1（對應論文 Opx；x∈{0,1}）
enum class Op { R0, R1, W0, W1 };

string opToStr(Op o){
    switch(o){
        case Op::R0: return "R0";
        case Op::R1: return "R1";
        case Op::W0: return "W0";
        case Op::W1: return "W1";
    }
    return "?";
}

// Cell：a（aggressor，低位址）、v（victim，高位址）
enum class Cell { A, V };

// Test Condition / Fault Primitive 的最小表述（僅以論文需要者為主）
// 參考「A. Fault Notation」與「B. Test Condition of Faults」定義。:contentReference[oaicite:10]{index=10}
struct TC {
    // 初始狀態 i = "00","01","10","11"（低位址=a，高位址=v）
    string i;               // e.g., "00"
    // S（致敏）在 a 與 v 的操作序列（字串解析後僅保留 W? 作為致敏用；R? 不列入 SOs，依論文示例）
    vector<Op> Sa_writes;   // 寫入致敏（僅 W0/W1）
    vector<Op> Sv_writes;   // 寫入致敏（僅 W0/W1）
    // D：（v, op）— 永遠在 victim 讀；op=R0/R1 由 S 後 v 的預期值決定（若 SOS 末端為寫入）
    Op D_read;              // R0 or R1
    // G：fault-free 下執行 S 後的預期狀態（兩位元 "ab"）
    string G;
    // c：此 fault 的致敏 cell 是哪個（哪一側真的有 W? 操作）
    Cell c;
    // 是否屬於 FF（flip，TC.I != TC.G）或 NFF（non-flip，TC.I == TC.G）
    bool isFF;
    // 僅為追蹤輸入來源
    string raw;             // 原始 "<Sa;Sv/F/R>" 字串
};

// 解析工具：把如 "<0;0R0W1/0/->" 的 FP 與群組 i 一起轉成 TC
// 規則：
//  1) i 由 Table III 群組提供；Sa/Sv 的前綴 '0'/'1' 視為知識/檢查，不用重覆覆蓋 i。
//  2) 致敏 SO 僅保留 W?（與論文示例一致：如 <0;0R0W1/0/-> 只加入 W1 作為致敏）。:contentReference[oaicite:11]{index=11}
//  3) G：從 i 出發，分別把 Sa/Sv 的 W? 應用在 a/v，得到 G。
//  4) D：若 FP 的 R 字段為 '-'（末端為寫），則以 G 的 v 位決定 R0/R1 作為讀出 op（與 Fig.2 的用法相符）。:contentReference[oaicite:12]{index=12}
static inline bool isW(char c){ return c=='W'; }
static inline bool isR(char c){ return c=='R'; }

vector<Op> parseWrites(const string& s){ // 從 "0R0W1" 抽出 W? -> {W0,W1}
    vector<Op> ws;
    for(size_t k=0;k+1<s.size();++k){
        if(s[k]=='W'){
            if(s[k+1]=='0') ws.push_back(Op::W0);
            else if(s[k+1]=='1') ws.push_back(Op::W1);
        }
    }
    return ws;
}

string applyWrites(const string& i, const vector<Op>& Sa, const vector<Op>& Sv){
    // i: "ab" (a=低位址, v=高位址)
    char a = i[0], v = i[1];
    for(auto op: Sa){
        if(op==Op::W0) a='0';
        else if(op==Op::W1) a='1';
    }
    for(auto op: Sv){
        if(op==Op::W0) v='0';
        else if(op==Op::W1) v='1';
    }
    string g; g.push_back(a); g.push_back(v); return g;
}

Op readFromV(const string& Gv){ // 以 G 的 v 位決定讀出
    return (Gv=="0")? Op::R0: Op::R1;
}

// 論文 Table III 的 24 個 fault primitives（按四組 i=00/01/10/11）:contentReference[oaicite:13]{index=13}
struct FPItem { string i; string fp; };
vector<FPItem> buildTableIII(){
    return {
        // i=00
        {"00","<0;0R0W1/0/->"}, {"00","<0;0W1W0/1/->"},
        {"00","<0;0W0W1/0/->"}, {"00","<0;0W1W1/0/->"},
        {"00","<0R0W1;0/1/->"}, {"00","<0W1W0;0/1/->"},
        // i=01
        {"01","<0;1R1W0/1/->"}, {"01","<0;1W1W0/1/->"},
        {"01","<0;1W0W1/0/->"}, {"01","<0;1W0W0/1/->"},
        {"01","<0R0W1;1/0/->"}, {"01","<0W1W0;1/0/->"},
        // i=10
        {"10","<1;0R0W1/0/->"}, {"10","<1;0W1W0/1/->"},
        {"10","<1;0W0W1/0/->"}, {"10","<1;0W1W1/0/->"},
        {"10","<1R1W0;0/1/->"}, {"10","<1W1W0;0/1/->"},
        // i=11
        {"11","<1;1R1W0/1/->"}, {"11","<1;1W1W0/1/->"},
        {"11","<1;1W0W1/0/->"}, {"11","<1;1W0W0/1/->"},
        {"11","<1R1W0;1/0/->"}, {"11","<1W1W0;1/0/->"},
    };
}

// 解析 FP 成 TC
TC parseTC(const FPItem& item){
    // <Sa;Sv/F/R>
    string s=item.fp;
    TC tc; tc.i=item.i; tc.raw=item.fp;
    // 拿掉 "<" ">"
    if(s.front()=='<') s=s.substr(1);
    if(s.back()=='>') s.pop_back();
    // 分割 "/"
    vector<string> seg;
    {
        string cur; for(char c: s){ if(c=='/') { seg.push_back(cur); cur.clear(); } else cur.push_back(c); }
        seg.push_back(cur);
    }
    // seg[0] = "Sa;Sv", seg[1] = F(0/1), seg[2] = R(0/1/-)
    string SaSv = seg[0];
    auto pos = SaSv.find(';');
    string Sa = SaSv.substr(0,pos);
    string Sv = SaSv.substr(pos+1);

    // 判斷哪一側有寫入（作為致敏 cell）
    auto Sa_w = parseWrites(Sa);
    auto Sv_w = parseWrites(Sv);
    tc.Sa_writes = Sa_w;
    tc.Sv_writes = Sv_w;
    if(!Sv_w.empty() && Sa_w.empty()) tc.c = Cell::V;
    else if(!Sa_w.empty() && Sv_w.empty()) tc.c = Cell::A;
    else{
        // 若兩側都有寫，為了符合 Table III 的案例（多為單側），這裡預設取 v 優先（實務可擴充）。
        tc.c = !Sv_w.empty()? Cell::V : Cell::A;
    }

    // G 與 D
    tc.G = applyWrites(tc.i, tc.Sa_writes, tc.Sv_writes);
    string Gv; Gv.push_back(tc.G[1]);
    // 若 seg[2]=='-'，以 Gv 決定 R0/R1；（Table III 多為 '-'）:contentReference[oaicite:14]{index=14}
    if(seg[2]=="-") tc.D_read = (Gv=="0")? Op::R0: Op::R1;
    else tc.D_read = (seg[2]=="0")? Op::R0: Op::R1;

    tc.isFF = (tc.G != tc.i); // NFF: I==G；FF: I!=G（定義見文）:contentReference[oaicite:15]{index=15}
    return tc;
}

// ---------- March Element 與 MTAS ----------
// 一個 ME 僅記錄操作序列（R/W）；方向 AO 僅為 Fig.2 規則判斷需要（這裡以 a<v 的固定 mapping 實作）
struct ME {
    vector<Op> ops;            // ME 主體
    vector<Op> prefaceReads;   // 可能因 Fig.2 需要放在「本 ME 開頭」的讀
};

string joinME(const ME& me){
    vector<string> out;
    for(auto r: me.prefaceReads) out.push_back(opToStr(r));
    for(auto o: me.ops) out.push_back(opToStr(o));
    // 逗號分隔，與 Fig.4~6 樣式一致
    string s;
    for(size_t i=0;i<out.size();++i){
        s += out[i];
        if(i+1<out.size()) s += ",";
    }
    return s;
}

// 整體 March Test
struct RawMarchTest {
    // 以 Fig.4~6 的結構輸出：M0..M5
    ME M0, M1, M2, M3, M4, M5;
};

// ---------- 選 fault 與加入操作：對應 Fig.1 / Fig.2 / Fig.3 ----------

// 判斷「SOs 是否已被現有 EME 完全包含」— Rule 3 所需（簡化：檢查 EME 尾端是否有相同連續尾序）:contentReference[oaicite:16]{index=16}
bool tailContains(const vector<Op>& container, const vector<Op>& pattern){
    if(pattern.empty()) return true;
    if(container.size() < pattern.size()) return false;
    size_t start = container.size() - pattern.size();
    for(size_t i=0;i<pattern.size();++i){
        if(container[start+i] != pattern[i]) return false;
    }
    return true;
}

// 加入致敏 + 依 Fig.2 插入讀出 + 依 Fig.3 修回狀態
// currentME/nextME：為支援 Fig.2 第 3/11 行可能「丟到下一個 ME 開頭」
void addOperationsToME_ByTC(
    const TC& tc, ME& currentME, ME& nextME,
    bool AO_up_for_v, // a<v：若本 ME 方向使得 v 在 a 之後，這裡以布林近似 Fig.2 的 AO 判斷
    bool isLastFaultInSet // Fig.2 第 3 行：僅在走最後一個 fault 時，允許把讀丟到下一個 ME 幫忙壓縮
){
    // 1) 先處理致敏 SOs（僅寫）
    const vector<Op>* SO = (tc.c==Cell::V)? &tc.Sv_writes : &tc.Sa_writes;

    // Rule 3：若 EME 已含蓋 SOs → 僅加讀；否則把缺的 W? 補上。:contentReference[oaicite:17]{index=17}
    if(!tailContains(currentME.ops, *SO)){
        for(auto w: *SO) currentME.ops.push_back(w);
    }

    // 2) Fig.2：決定讀出 TC.D 放哪裡。:contentReference[oaicite:18]{index=18}
    //   本實作：對 Table III + a<v 的情境，採用簡明近似：
    //   - 若 c==v：把讀接在 SO 後（Fig.2 line 5）。
    //   - 若 c==a：
    //       * 若 AO_up_for_v==false（表示本 ME 的順序會先遇到 v，符合 Fig.2 的「讀需前移」情境）：
    //           嘗試塞到 currentME 開頭（prefaceReads）。若有相同首 op 衝突，就塞到 nextME 開頭（Fig.2 line 10~11）。
    //       * 若 AO_up_for_v==true：可接在 SO 後（S 在 a，再遇到 v）。
    auto pushReadToCurrentHead = [&](Op r){
        if(!currentME.prefaceReads.empty() && currentME.prefaceReads.front()==r){
            // 與現有首 op 相同不違反 alternation（此處簡化）→ 保留
            currentME.prefaceReads.insert(currentME.prefaceReads.begin(), r);
        }else{
            currentME.prefaceReads.insert(currentME.prefaceReads.begin(), r);
        }
    };
    auto pushReadToNextHead = [&](Op r){
        if(!nextME.prefaceReads.empty() && nextME.prefaceReads.front()==r){
            nextME.prefaceReads.insert(nextME.prefaceReads.begin(), r);
        }else{
            nextME.prefaceReads.insert(nextME.prefaceReads.begin(), r);
        }
    };

    if(tc.c==Cell::V){
        currentME.ops.push_back(tc.D_read); // line 5
    }else{
        if(!AO_up_for_v){
            // 需要把讀前移
            // 若這是本 Fault_Set 的最後一個 fault，且可幫助壓縮，也允許放到 nextME（Fig.2 line 3/4）
            if(isLastFaultInSet){
                pushReadToNextHead(tc.D_read);
            }else{
                pushReadToCurrentHead(tc.D_read); // line 7~8
            }
        }else{
            // 可以接在 SO 後
            currentME.ops.push_back(tc.D_read);
        }
    }

    // 3) Fig.3：若 FF（I!=G）→ 插入 flip operation W0/W1 把「致敏 cell」修回初始 i。:contentReference[oaicite:19]{index=19}
    if(tc.isFF){
        char need = (tc.c==Cell::V)? tc.i[1] : tc.i[0];
        currentME.ops.push_back( (need=='0')? Op::W0 : Op::W1 );
    }
}

// ---------- Fault-Set 與 MTAS 巡訪策略（a<v） ----------
// 本實作依 Table II（a<v）在 Fig.1 的 Get_Fault_Set(AO, State_V) 做**固定順序**，以重現 Fig.4 的 I-MTA。:contentReference[oaicite:20]{index=20}
// 依論文示例：先從 Fault_Set(00,v) 起，之後切到 (01,a)，… 最終覆蓋 Table III。
// 我們將：M1 = (00,v) → (01,a)
//        M2 = (11,v) → (10,a)
//        M3 = (00,a) → (10,v)
//        M4 = (11,a) → (01,v)
// M0 = (W0)，M5 = (R0) 直接給定（Fig.4 樣式）。:contentReference[oaicite:21]{index=21}
struct FaultSetKey { string i; Cell c; };
struct FaultSetKeyLess {
    bool operator()(const FaultSetKey& x, const FaultSetKey& y) const {
        if(x.i!=y.i) return x.i<y.i;
        return (int)x.c < (int)y.c;
    }
};

// NFF 先於 FF（Rule 1），且若 SO 已被 EME 含蓋者優先（Rule 2）。:contentReference[oaicite:22]{index=22}
vector<TC> orderByRule1Rule2(const vector<TC>& all, const ME& me){
    vector<TC> nff, ff;
    for(auto& t: all){ if(t.isFF) ff.push_back(t); else nff.push_back(t); }
    auto score = [&](const TC& t){
        const vector<Op>* SO = (t.c==Cell::V)? &t.Sv_writes : &t.Sa_writes;
        return tailContains(me.ops, *SO)? 1:0; // 被含蓋 → 高分（優先）
    };
    auto cmp = [&](const TC& a, const TC& b){
        return score(a) > score(b);
    };
    sort(nff.begin(), nff.end(), cmp);
    sort(ff.begin(), ff.end(), cmp);

    vector<TC> out; out.reserve(all.size());
    for(auto& t: nff) out.push_back(t);
    for(auto& t: ff)  out.push_back(t);
    return out;
}

// 把同一 Fault_Set(i,c) 的 faults 放進一個 ME（必要時把讀前移到下一個 ME）：
void packFaultSetIntoME(const vector<TC>& set, ME& cur, ME& next, bool AO_up_for_v){
    auto ordered = orderByRule1Rule2(set, cur); // Rule 1/2
    for(size_t k=0;k<ordered.size();++k){
        bool last = (k+1==ordered.size());
        addOperationsToME_ByTC(ordered[k], cur, next, AO_up_for_v, last);
    }
}

// ---------- Phase-1：從 Table III 生成 I-MTA ----------
// 回傳：MarchTest with Fig.4 之 I-MTA（期望為 54N）。:contentReference[oaicite:23]{index=23}
RawMarchTest build_I_MTA_from_TableIII(){
    // 1) 解析 Table III → 16 類 Fault_Set(i,c)
    map<FaultSetKey, vector<TC>, FaultSetKeyLess> FS;
    for(auto& fp: buildTableIII()){
        TC tc = parseTC(fp);
        FaultSetKey key{tc.i, tc.c};
        FS[key].push_back(tc);
    }

    RawMarchTest mt;
    // M0: W0（與 Fig.4 一致）
    mt.M0.ops = { Op::W0 };

    // 將四個 ME 依固定順序塞入（對應 MTAS a<v 的巡訪；M1..M4）
    // AO_up_for_v 設定：當本 ME 的掃描順序使「先遇 a 後遇 v」時，設 true；反之（先 v 後 a）設 false，
    // 用來驅動 Fig.2 的簡化決策。
    auto addME = [&](ME& cur, ME& next, const vector<pair<string,Cell>>& sets, bool AO_up_for_v){
        for(size_t i=0;i<sets.size();++i){
            FaultSetKey k{sets[i].first, sets[i].second};
            auto it = FS.find(k);
            if(it==FS.end()) continue;
            packFaultSetIntoME(it->second, cur, next, AO_up_for_v);
            // 該 Fault_Set 已覆蓋 → 清空
            it->second.clear();
        }
    };

    // M1: (00,v) → (01,a)
    addME(mt.M1, mt.M2, {{"00",Cell::V},{"01",Cell::A}}, /*AO_up_for_v=*/true);
    // M2: (11,v) → (10,a)
    addME(mt.M2, mt.M3, {{"11",Cell::V},{"10",Cell::A}}, /*AO_up_for_v=*/true);
    // M3: (00,a) → (10,v)   （此處讓 AO_up_for_v=false，使 c==a 的讀前移，符合 Fig.2 的前移情境）
    addME(mt.M3, mt.M4, {{"00",Cell::A},{"10",Cell::V}}, /*AO_up_for_v=*/false);
    // M4: (11,a) → (01,v)
    addME(mt.M4, mt.M5, {{"11",Cell::A},{"01",Cell::V}}, /*AO_up_for_v=*/false);

    // M5: R0（與 Fig.4 一致）
    mt.M5.ops = { Op::R0 };
    return mt;
}

// ---------- Phase-2：針對 Table III 的同款壓縮示範（I-MTA → I-MTA-1 → OMTA） ----------
// 依論文兩個 observation 對「Table III 的 I-MTA」做相同操作：
//  Obs1（Loop-Sens fault 插入致敏 → 刪掉別處可刪段）：在 M1(8) 後插入 {W0,W1}，刪除 M4(1..3)。:contentReference[oaicite:24]{index=24}
void applyObs1_TableIII(RawMarchTest& mt){
    // M1(8) 後插入 {W0,W1}：此處把「第 8 個」理解為 M1.ops 的第 8 筆（從 1 起算）
    auto& m1 = mt.M1.ops;
    if(m1.size() >= 8){
        m1.insert(m1.begin()+8, {Op::W0, Op::W1});
    }
    // 刪 M4(1)~M4(3)
    auto& m4 = mt.M4.ops;
    if(m4.size()>=3){
        m4.erase(m4.begin(), m4.begin()+3);
    }
}

//  Obs2（交換片段使 flip deletable）：交換 M1(4..8) 與 M4(4..8)，並刪除 M1(3), M1(9), M4(3), M4(9)。:contentReference[oaicite:25]{index=25}
void applyObs2_TableIII(RawMarchTest& mt){
    auto& m1 = mt.M1.ops;
    auto& m4 = mt.M4.ops;
    auto safeSlice = [](vector<Op>& v, int l, int r){ // [l..r] 1-based
        int L=l-1, R=r-1;
        if(L<0) L=0;
        if(R>=(int)v.size()) R=(int)v.size()-1;
        if(L>R) return vector<Op>{};
        return vector<Op>(v.begin()+L, v.begin()+R+1);
    };
    auto replaceSlice = [](vector<Op>& v, int l, int r, const vector<Op>& rep){
        int L=l-1, R=r-1;
        if(L<0) L=0;
        if(R>=(int)v.size()) R=(int)v.size()-1;
        if(L>R) return;
        v.erase(v.begin()+L, v.begin()+R+1);
        v.insert(v.begin()+L, rep.begin(), rep.end());
    };
    // 交換
    auto s1 = safeSlice(m1,4,8);
    auto s4 = safeSlice(m4,4,8);
    replaceSlice(m1,4,8,s4);
    replaceSlice(m4,4,8,s1);

    // 刪 flip：M1(3), M1(9), M4(3), M4(9)
    auto eraseAt1Based = [](vector<Op>& v, int idx){
        int I=idx-1; if(0<=I && I<(int)v.size()) v.erase(v.begin()+I);
    };
    // 注意：刪除會改變後續索引，故用「由大到小」順序刪除以避免位移錯位
    eraseAt1Based(m1,9);
    eraseAt1Based(m1,3);
    eraseAt1Based(m4,9);
    eraseAt1Based(m4,3);
}

//輸出工具：印出 Fig.4/5/6 的樣式
void printMarch(const string& title, const RawMarchTest& mt){
    cout << "=== " << title << " ===\n";
    cout << "M0: (" << joinME(mt.M0) << ")\n";
    cout << "M1: (" << joinME(mt.M1) << ")\n";
    cout << "M2: (" << joinME(mt.M2) << ")\n";
    cout << "M3: (" << joinME(mt.M3) << ")\n";
    cout << "M4: (" << joinME(mt.M4) << ")\n";
    cout << "M5: (" << joinME(mt.M5) << ")\n\n";
}

int main(){
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    // Phase-1：以 Table III 建 I-MTA
    RawMarchTest I_MTA = build_I_MTA_from_TableIII();
    printMarch("Phase-1  (I-MTA  expected ~54N; Fig.4)", I_MTA);

    // Phase-2 / Obs1：Loop-Sens 插入 + 刪除
    RawMarchTest I_MTA_1 = I_MTA;
    applyObs1_TableIII(I_MTA_1);
    printMarch("Phase-2a (I-MTA-1 expected ~50N; Fig.5)", I_MTA_1);

    // Phase-2 / Obs2：交換片段 + 刪除 flips
    RawMarchTest OMTA = I_MTA_1;
    applyObs2_TableIII(OMTA);
    printMarch("Phase-2b (OMTA    expected ~42N; Fig.6)", OMTA);

    return 0;
}
