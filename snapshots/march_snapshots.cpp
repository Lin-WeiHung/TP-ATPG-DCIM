// march_print_ctriple_v2.cpp
// C(l)(m)(r) 三段式；C 在 before/at/after 每個實際被處理的位址都會依序執行並覆寫全域 C 狀態。
// 編譯：g++ -std=c++17 -O2 -Wall -Wextra march_print_ctriple_v2.cpp -o march_print
// 使用：echo "b{W0 C(0)(0)(0)} a{R0 W1 C(0)(1)(1)}" | ./march_print --cells 3 --r 1

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <iostream>
#include <optional>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
using namespace std;

enum class Dir { Up, Down };

// ---- C 三段式 ----
struct CSpec {
    int l = 0, m = 0, r = 0;  // ∈ {0,1}
    static string toString(const CSpec& c){
        return "C("s + char('0'+c.l) + ")(" + char('0'+c.m) + ")(" + char('0'+c.r) + ")";
    }
};

// ---- 操作 ----
struct Op {
    enum class Type { Read, Write, Compute } type;
    int val = -1;                  // R/W 用
    optional<CSpec> c;             // C 用
    string printable;              // 原字串（輸出用）
};

// ---- Element ----
struct Element {
    char dirChar;                  // 'a','b','d'（b 視為 Up）
    Dir  dir;
    vector<Op> ops;
};

// ---- 快照 ----
struct Snap { int id; vector<optional<int>> D; vector<optional<int>> C; };

// ============ Parser ============
class Parser {
public:
    vector<Element> parseLine(const string& s) const {
        vector<Element> elems;
        regex block(R"(([badBAD])\s*\{([^}]*)\})");
        for (auto it = sregex_iterator(s.begin(), s.end(), block);
             it != sregex_iterator(); ++it) {
            smatch m = *it;
            char dch = (char)tolower(m[1].str()[0]);
            Dir dir = (dch=='d') ? Dir::Down : Dir::Up;
            string inside = m[2].str();
            auto ops = parseOps(inside);
            if (ops.empty()) throw runtime_error("Element has no operations: " + m.str());
            elems.push_back(Element{dch, dir, std::move(ops)});
        }
        if (elems.empty()) throw runtime_error("No element parsed.");
        return elems;
    }

private:
    static vector<Op> parseOps(const string& inside) {
        vector<Op> res;
        // 支援：R0/W1 與 C(l)(m)(r)，l,m,r ∈ {0,1}；空白容忍
        regex tokRe(R"(([RW][01])|(C\s*\(\s*[01]\s*\)\s*\(\s*[01]\s*\)\s*\(\s*[01]\s*\)))",
                    regex::icase);
        for (auto it = sregex_iterator(inside.begin(), inside.end(), tokRe);
             it != sregex_iterator(); ++it) {
            string tok = it->str();

            // R/W
            if (tok.size()>=2 && (tok[0]=='R'||tok[0]=='r'||tok[0]=='W'||tok[0]=='w')
                && (tok[1]=='0'||tok[1]=='1')) {
                Op op;
                op.type = (toupper(tok[0])=='R') ? Op::Type::Read : Op::Type::Write;
                op.val  = tok[1]-'0';
                op.printable = string(1,(char)toupper(tok[0])) + (op.val?'1':'0');
                res.push_back(std::move(op));
                continue;
            }

            // C(l)(m)(r)
            if (!tok.empty() && (tok[0]=='C' || tok[0]=='c')) {
                string t; t.reserve(tok.size());
                for (char ch: tok) if (!isspace((unsigned char)ch)) t.push_back(ch);
                regex cre(R"(C\(([01])\)\(([01])\)\(([01])\))", regex::icase);
                smatch cm;
                if (!regex_match(t, cm, cre)) throw runtime_error("Bad C token: " + tok);

                CSpec c; c.l = cm[1].str()[0]-'0'; c.m = cm[2].str()[0]-'0'; c.r = cm[3].str()[0]-'0';
                Op op; op.type = Op::Type::Compute; op.c = c; op.printable = CSpec::toString(c);
                res.push_back(std::move(op));
                continue;
            }
        }
        return res;
    }
};

// ============ C Evaluator ============
static vector<int> buildC_by_k(const CSpec& c, int cells, int k){
    vector<int> out(cells, 0);
    if (cells<=0) return out;
    k = max(0, min(cells-1, k));
    for (int i=0;i<cells;++i){
        if (i < k)      out[i] = c.l;
        else if (i==k)  out[i] = c.m;
        else            out[i] = c.r;
    }
    return out;
}

// ============ Simulator ============
class Simulator {
public:
    struct Config { int cells=3; int r=1; };
    explicit Simulator(Config cfg): cfg_(cfg), R_(cfg.cells-1){
        if (cfg_.cells<=0) throw invalid_argument("cells>0");
        if (cfg_.r<0 || cfg_.r>R_) throw invalid_argument("r out of range");
        Dstate_.assign(cfg_.cells, nullopt);
        Cstate_.assign(cfg_.cells, nullopt); // 全域 C（跨 element 保留）
    }

    struct RunResult {
        vector<Element> elems;
        vector<Snap> snaps;
        vector<vector<int>> idsPerElem; // 每 element 的 [PRE, after op1, ...] -> snap id
    };

    RunResult run(vector<Element> elems){
        RunResult rr; rr.elems = std::move(elems);
        int id=1;
        for (size_t ei=0; ei<rr.elems.size(); ++ei) {
            rr.idsPerElem.push_back(simElem(rr.elems[ei], rr.snaps, id));
        }
        return rr;
    }

private:
    Config cfg_; int R_;
    vector<optional<int>> Dstate_;
    vector<optional<int>> Cstate_;

    static vector<int> beforeAddrs(Dir dir, int r, int R){
        vector<int> v;
        if (dir==Dir::Up) { for (int i=0;i<r;++i) v.push_back(i); }
        else              { for (int i=R;i>r;--i) v.push_back(i); }
        return v;
    }
    static vector<int> afterAddrs(Dir dir, int r, int R){
        vector<int> v;
        if (dir==Dir::Up) { for (int i=r+1;i<=R;++i) v.push_back(i); }
        else              { for (int i=r-1;i>=0;--i) v.push_back(i); }
        return v;
    }

    static void applyRWAtAddr(vector<optional<int>>& D, int addr, const vector<Op>& ops){
        for (const auto& op: ops) if (op.type==Op::Type::Write) D[addr]=op.val;
    }

    // 新增：在「某個位址 k」依序執行所有 C（若有），覆寫全域 Cstate_
    void applyCAtAddrIfAny(int k, const vector<Op>& ops){
        bool seen = false;
        vector<int> temp;
        for (const auto& op : ops){
            if (op.type==Op::Type::Compute){
                temp = buildC_by_k(*op.c, (int)Cstate_.size(), k);
                seen = true;
            }
        }
        if (seen){
            for (size_t i=0;i<Cstate_.size(); ++i) Cstate_[i] = temp[i];
        }
    }

    vector<int> simElem(const Element& e, vector<Snap>& out, int& nextId){
        auto D = Dstate_;

        // 1) before：逐地址做 R/W，並在該位址 k 依序執行本 element 的 C（若有）
        for (int k : beforeAddrs(e.dir, cfg_.r, R_)) {
            applyRWAtAddr(D, k, e.ops);
            applyCAtAddrIfAny(k, e.ops);
        }

        vector<int> ids; ids.reserve(e.ops.size()+1);

        // 2) PRE：顯示當前 D 與全域 C
        out.push_back(Snap{nextId, D, Cstate_}); ids.push_back(nextId++);

        // 3) 在 r：逐 op；R/W 立即修改 D；遇到 C 則以 k=r 覆寫全域 C
        for (const auto& op : e.ops){
            if (op.type==Op::Type::Write) {
                D[cfg_.r] = op.val;
            } else if (op.type==Op::Type::Compute) {
                auto vec = buildC_by_k(*op.c, (int)Cstate_.size(), cfg_.r);
                for (size_t i=0;i<Cstate_.size(); ++i) Cstate_[i] = vec[i];
            }
            out.push_back(Snap{nextId, D, Cstate_}); ids.push_back(nextId++);
        }

        // 4) after：逐地址做 R/W，並在該位址 k 依序執行本 element 的 C（若有）
        for (int k : afterAddrs(e.dir, cfg_.r, R_)) {
            applyRWAtAddr(D, k, e.ops);
            applyCAtAddrIfAny(k, e.ops);
        }

        Dstate_ = std::move(D);
        return ids;
    }
};

// ============ 輸出（元素標頭 + 每 element 兩列、欄寬對齊） ============
static void printHeaders(const vector<Element>& elems, const vector<vector<int>>& idsPerElem){
    for (size_t ei=0; ei<elems.size(); ++ei){
        const auto& e = elems[ei];
        const auto& ids = idsPerElem[ei];
        ostringstream oss;
        oss << e.dirChar << "{ ";
        if (!ids.empty()) oss << "(" << ids[0] << ") ";
        for (size_t k=0;k<e.ops.size();++k){
            if (k) oss << " ";
            oss << e.ops[k].printable << " (" << ids[k+1] << ")";
        }
        oss << " }";
        cout << oss.str() << "\n";
    }
}

static string fmtVec(const vector<optional<int>>& v){
    ostringstream o;
    o << "{";
    for (size_t i=0;i<v.size(); ++i){
        o << (v[i].has_value()? char('0'+*v[i]) : 'X');
        if (i+1!=v.size()) o << ", ";
    }
    o << "}";
    return o.str();
}

static void printSnapsGrouped(const vector<Element>& elems,
                              const vector<vector<int>>& idsPerElem,
                              const vector<Snap>& snaps){
    unordered_map<int, const Snap*> byId;
    byId.reserve(snaps.size());
    for (const auto& s: snaps) byId[s.id] = &s;

    auto padRight = [](const string& s, size_t w){ return (s.size()>=w)? s : s + string(w - s.size(), ' '); };

    cout << "\n";
    for (size_t ei=0; ei<elems.size(); ++ei){
        const auto& ids = idsPerElem[ei];
        if (ids.empty()) { cout << "\n"; continue; }

        vector<string> ID(ids.size()), D(ids.size()), C(ids.size());
        for (size_t k=0;k<ids.size(); ++k){
            const Snap* s = byId.at(ids[k]);
            ID[k] = "(" + to_string(ids[k]) + ")";
            D[k]  = fmtVec(s->D);
            C[k]  = fmtVec(s->C);
        }

        // 欄寬 = max( len("(id) " + D), len("(id) " + C) )
        vector<size_t> idw(ids.size()), colw(ids.size());
        for (size_t k=0;k<ids.size(); ++k){
            idw[k]  = ID[k].size() + 1;
            colw[k] = max(idw[k] + D[k].size(), idw[k] + C[k].size());
        }

        // 第一列： (id) + D
        {
            ostringstream line;
            for (size_t k=0;k<ids.size(); ++k){
                if (k) line << "   ";
                line << padRight(ID[k] + " " + D[k], colw[k]);
            }
            cout << line.str() << "\n";
        }
        // 第二列：同欄寬印 C
        {
            ostringstream line;
            for (size_t k=0;k<ids.size(); ++k){
                if (k) line << "   ";
                line << padRight(string(idw[k],' ') + C[k], colw[k]);
            }
            cout << line.str() << "\n";
        }
        cout << "\n";
    }
}

// ============ CLI ============
static void parseCLI(int argc, char** argv, int& cells, int& r){
    cells=3; r=1;
    for (int i=1;i<argc;++i){
        string a=argv[i];
        if (a=="--cells" && i+1<argc) cells = stoi(argv[++i]);
        else if (a=="--r" && i+1<argc) r = stoi(argv[++i]);
    }
}

// ============ main ============
int main(int argc, char** argv){
    try{
        int cells, r; parseCLI(argc, argv, cells, r);

        string line; getline(cin, line);
        if (line.empty()){
            cerr << "請輸入一行 March test，例如：\n";
            cerr << "b{W0 C(0)(0)(0)} a{R0 W1 C(0)(1)(1)} d{R0 W1 C(1)(1)(0)} b{R0}\n";
            cerr << "參數：--cells N --r K（預設 N=3, K=1）\n";
            return 1;
        }

        Parser p; auto elems = p.parseLine(line);
        // 套用固定 r：本程式的快照只在 r 位址（例如 r=1）取
        // 但 before/after 仍會把各位址的 C 依序執行，作為全域 C 狀態
        class Simulator sim(Simulator::Config{cells, r});
        auto rr = sim.run(elems);

        printHeaders(rr.elems, rr.idsPerElem);
        printSnapsGrouped(rr.elems, rr.idsPerElem, rr.snaps);
    }catch(const std::exception& ex){
        cerr << "Error: " << ex.what() << endl;
        return 1;
    }
    return 0;
}
