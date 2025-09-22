#include <cassert>
#include <iostream>
#include <sstream>
#define private   public
#define protected public
#include "../include/Parser.hpp"        // Parser / explodeOpToken
#include "../src/Parser.cpp"

// 方便重複驗證字串 → int 轉換
void test_toInt() {
    Parser p;
    assert(p.toInt("0") == 0);
    assert(p.toInt("1") == 1);
    assert(p.toInt("-") == -1);
    bool threw = false;
    try { p.toInt("x"); }          // 非法輸入應該丟例外:contentReference[oaicite:0]{index=0}
    catch (const std::runtime_error&) { threw = true; }
    assert(threw);
}

// 驗證 R0W1CI0CO1 拆解成 4 個 SingleOp
void test_explodeOpToken() {
    Parser p;
    auto v = p.explodeOpToken("R0W1Ci0Co1");      // ⬅︎ 大小寫交錯也要吃得下:contentReference[oaicite:1]{index=1}
    assert(v.size() == 4);
    assert(v[0].type_ == OpType::R && v[0].value_ == 0);
    assert(v[2].type_ == OpType::CI && v[2].value_ == 0);
}

// 讀 fault.json：檢查第一條 SAF
void test_parseFaults() {
    Parser p;
    auto list = p.parseFaults("fault.json");
    assert(!list.empty());
    const FaultConfig& f = list.front();
    assert(f.id_.faultName_ == "Stuck-at Fault (SAF)");
    assert(!f.is_twoCell_);
    assert(f.VI_ == 0);
    assert(f.trigger_.size() == 1);           // W1
    assert(f.faultValue_ == 0);
    assert(f.finalReadValue_ == -1);
}

// 為 parseMarchTest 建一個 cin mock
std::vector<MarchElement> parse_march_with_choice(std::size_t idx) {
    Parser p;
    std::istringstream fakeIn(std::to_string(idx) + "\n");
    std::streambuf* orig = std::cin.rdbuf(fakeIn.rdbuf()); // ↱ 取代 cin
    auto elems = p.parseMarchTest("marchTest.json");       // 內部會讀 cin:contentReference[oaicite:2]{index=2}
    std::cin.rdbuf(orig);                                  // ↳ 還原
    return elems;
}

void test_parseMarchTest_ok() {
    auto elems = parse_march_with_choice(1);   // 選 「MATS++」 (第 1 筆)
    assert(elems.size() == 3);                 // 3 個 MarchElement
    assert(elems[0].addrOrder_ == Direction::BOTH);
    assert(elems[0].ops_.size() == 1);         // (w0)
}

void test_parseMarchTest_invalid_index() {
    bool threw = false;
    Parser p;
    std::istringstream wrong("999\n");
    std::streambuf* orig = std::cin.rdbuf(wrong.rdbuf());
    try { p.parseMarchTest("marchTest.json"); }            // 應該丟 "Invalid selection":contentReference[oaicite:3]{index=3}
    catch (const std::runtime_error&) { threw = true; }
    std::cin.rdbuf(orig);
    assert(threw);
}

// 驗證能正確產生報告檔 (僅測試「未偵測」路徑，涵蓋 I/O、格式基本正確性)
void test_writeDetectionReport() {
    Parser p;
    auto faults = p.parseFaults("fault.json"); // 尚未執行模擬 → 預設皆未偵測
    const std::string fname = "t_detection_report.txt";

    // 呼叫待測函式
    p.writeDetectionReport(fname, faults);

    // 檔案應該被成功建立並且可讀
    std::ifstream ifs(fname);
    assert(ifs && "❌ 產生 report 失敗");

    std::stringstream buffer; buffer << ifs.rdbuf();
    std::string txt = buffer.str();
    assert(!txt.empty() && "❌ Report 內容為空");

    // (1) 至少包含一次 "No detection" 字串
    assert(txt.find("No detection") != std::string::npos && "❌ 缺少 \"No detection\" 標記");

    // (2) 每條 fault 都應該對應一行結果 (用出現次數粗略驗證)
    size_t subcase_cnt = 0;
    for (size_t pos = txt.find(" Subcase "); pos != std::string::npos; pos = txt.find(" Subcase ", pos + 8))
        ++subcase_cnt;
    assert(subcase_cnt == faults.size() && "❌ Subcase 行數與 fault 數量不符");
}

int main() {
    test_toInt();
    test_explodeOpToken();
    test_parseFaults();
    test_parseMarchTest_ok();
    test_parseMarchTest_invalid_index();
    test_writeDetectionReport();
    std::cout << "✅  All cassert tests passed!\n";
}
