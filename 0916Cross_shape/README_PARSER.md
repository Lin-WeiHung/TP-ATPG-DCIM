# Faults Parser (header-only)

本資料夾提供 `Parser.hpp`（header-only）解析 `faults.json` 的工具，以及簡易測試 `ParserTest.cpp`。

## 需求
- C++17 或以上（專案 makefile 已使用 `-std=c++20`）
- [nlohmann/json](https://github.com/nlohmann/json)（Ubuntu 套件：`nlohmann-json3-dev`）

## 檔案
- `Parser.hpp`：介面與實作皆在同一檔（以區塊註解分隔），不使用 `using namespace std;`，僅精確 using。
- `ParserTest.cpp`：讀取 `faults.json`，進行幾項基本驗證。
- `faults.json`：範例輸入（已在此資料夾）。

## 編譯與執行
在此資料夾下執行：

```bash
# 單檔編譯（假設 json.hpp 在系統 include path 中）
g++ -std=c++20 -O2 -Wall -Wextra -pedantic ParserTest.cpp -o ParserTest

# 若 json.hpp 不在系統路徑，請加上 -I 指向 nlohmann/json.hpp 所在目錄
# g++ -std=c++20 -O2 -Wall -Wextra -pedantic -I/path/to/nlohmann ParserTest.cpp -o ParserTest

# 執行
./ParserTest
```

執行成功會輸出：
```
Loaded faults: <數量>
All checks passed.
```
