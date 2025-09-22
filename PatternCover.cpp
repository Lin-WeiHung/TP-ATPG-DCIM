// MinimalHitting.cpp
// 以最樸素的「子集合窮舉」尋找 4-bit pattern 的最小 hitting set。
// 1. 允許 pattern 中出現 'x' (don't-care)。
// 2. 讀入一行字串，例如 x0110011  →  切成 x011 與 0011 兩個 4-bit pattern。
// 3. 求出能同時覆蓋所有 pattern 的最小實際向量(只含 0/1)。
// 4. 將結果逐行輸出。
// 編譯：g++ -std=c++17 MinimalHitting.cpp -o hit
// 執行：echo x0110011 | ./hit
#include <iostream>
#include <vector>
#include <string>
#include <cstdint>

// 可自行調整 bit 寬度 (<= 6 建議)。
const int BIT = 4;                // 每一組 pattern 的位元數
const int VEC_CNT = 1 << BIT;     // 實際向量數量 (2^BIT)

// --------------------------
//    最小 hitting set 類別
// --------------------------
class MinimalCover {
public:
    // 加入一條 pattern (長度需等於 BIT)
    void add(const std::string &pat) {
        patterns.push_back(pat);
    }

    // 計算並回傳最小覆蓋集合 (向量以 0/1 字串表示)
    std::vector<std::string> solve() const {
        int P = patterns.size();
        if (P == 0) return {};

        // 預先計算每個實向量能覆蓋哪些 pattern (以 bitmask 表示)
        std::vector<uint64_t> mask(VEC_CNT, 0);
        for (int v = 0; v < VEC_CNT; ++v) {
            for (int i = 0; i < P; ++i)
                if (match(v, patterns[i])) mask[v] |= (1ULL << i);
        }
        uint64_t FULL = (P >= 64) ? ~0ULL : ((1ULL << P) - 1);

        // 最小子集合搜尋
        int bestSize = BIT * 2;          // 上界 (足夠小即可)
        uint32_t bestSet = 0;
        for (uint32_t subset = 1; subset < (1u << VEC_CNT); ++subset) {
            int sz = popcount(subset);
            if (sz >= bestSize) continue;
            uint64_t cov = 0;
            for (int v = 0; v < VEC_CNT && cov != FULL; ++v)
                if (subset & (1u << v)) cov |= mask[v];
            if (cov == FULL) { bestSize = sz; bestSet = subset; }
        }
        // 轉成字串結果
        std::vector<std::string> ans;
        for (int v = 0; v < VEC_CNT; ++v)
            if (bestSet & (1u << v)) ans.push_back(vecToStr(v));
        return ans;
    }

private:
    std::vector<std::string> patterns;

    static bool match(int val, const std::string &pat) {
        for (int i = 0; i < BIT; ++i) {
            char pc = pat[BIT - 1 - i];  // 右邊最低位
            if (pc == 'x') continue;
            int bit = (val >> i) & 1;
            if (bit != pc - '0') return false;
        }
        return true;
    }

    static std::string vecToStr(int v) {
        std::string s(BIT, '0');
        for (int i = 0; i < BIT; ++i)
            s[BIT - 1 - i] = ((v >> i) & 1) ? '1' : '0';
        return s;
    }

    static int popcount(uint32_t x) {
        int c = 0; while (x) { x &= x - 1; ++c; } return c;
    }
};

// ---------------
//  輔助 I/O 函式
// ---------------
MinimalCover readPatterns() {
    MinimalCover mc;
    std::string input;
    if (!(std::cin >> input)) return mc;
    for (size_t i = 0; i + BIT <= input.size(); i += BIT)
        mc.add(input.substr(i, BIT));
    return mc;
}

void printCover(const std::vector<std::string> &cover) {
    for (const auto &v : cover) std::cout << v << '\n';
}

// ---- main ----
int main() {
    MinimalCover mc = readPatterns();
    std::vector<std::string> cover = mc.solve();
    printCover(cover);
    return 0;
}
