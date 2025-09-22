以下提供一個 **「前瞻＋務實」** 的整併範例。請依實際需要取捨，重點是 **把「共用」與「專案專屬」清楚分離**，並讓日後擴充（第三個 / 第四個系統）也能自然長出來。
我會先給出建議的總體目錄，再說明各層責任與常見做法。

```
my-lab-projects/              <-- 頂層（Monorepo 一次管所有系統）
│
├── cmake/                    <-- 共用的 CMake 腳本(或 GNU Make 片段)
│   └── ...                  
│
├── common/                   <-- **共用函式庫 (shared library / headers)**
│   ├── include/
│   │   ├── fault/
│   │   │   ├── Fault.hpp
│   │   │   └── FaultConfig.hpp
│   │   └── util/
│   │       └── Logger.hpp
│   └── src/
│       ├── Fault.cpp
│       ├── FaultConfig.cpp
│       └── Logger.cpp
│
├── simulator/                <-- **原本的 Simulator 子專案**
│   ├── include/              <-- 只放 Simulator 專用介面
│   │   ├── AddressAllocator.hpp
│   │   ├── DetectionReport.hpp
│   │   └── ...
│   ├── src/                  <-- 實作（#include "common/..."）
│   ├── input/
│   ├── tests/
│   ├── CMakeLists.txt
│   └── README.md
│
├── atpg/                     <-- **新加入的 ATPG 子專案**
│   ├── include/
│   │   └── ...     (ATPG 專屬／或覆寫共用介面)
│   ├── src/
│   ├── input/
│   ├── tests/
│   ├── CMakeLists.txt
│   └── README.md
│
├── third_party/              <-- 第三方函式庫 (submodule／fetch)
│   └── googletest/
│
├── docker/                   <-- 為整體環境提供 image
│   └── Dockerfile
│
├── .gitignore
└── CMakeLists.txt            <-- 頂層：add_subdirectory(common sim­ulator atpg)
```

---

### 1. 頂層 (`my-lab-projects/`)

* **Monorepo**（單一 Git 儲存庫）能讓程式碼共用與版本同步變得簡單。
* 透過 `CMakeLists.txt` 或多目標 `makefile`，一次建置所有子專案，並開啟 **整體 CI/CD**。
* 若日後想拆倉庫，保持子資料夾獨立即可輕易切分。

### 2. `common/` ─ 共用函式庫 (library)

* 將 **Fault 模型、記憶體狀態管理** 等重複使用的核心邏輯集中於此。
* 產出一個 `libcommon.a` (static) 或 `libcommon.so` (shared)，讓子專案 link。
* **單一職責原則（SRP, Single-Responsibility Principle）**：此層只關心「共用邏輯」本身，不含任何具體測試流程或 UI。
* 共用標頭可再依領域分子目錄（如 `fault/`, `util/`），減少 namespace 污染。

### 3. `simulator/` & `atpg/` ─ 子專案

* 兩者**各自擁有 input/、src/、tests/**，互不干涉，唯 **透過依賴 `common`** 共享程式碼與介面。
* 若有不同的執行檔（`Simulator.app`, `ATPG.app`），各自定義 `add_executable()` 並 `target_link_libraries(simulator common)`。
* 測試框架（如 **GoogleTest**）可由 `third_party/` 供應，一次下載、重複使用。

### 4. `cmake/` / 預先片段

* 把常用編譯旗標、第三方尋徑、clang-tidy 規範寫成共用檔，子專案 `include(cmake/xx.cmake)`。
* 例：`cmake/CPM.cmake` 用 **CPM (CMake Package Manager)** 管理外部依賴；或 `Warnings.cmake` 統一 `-Wall -Wextra -Werror`。

### 5. Docker 與開發環境

* 頂層 `docker/` 可建立一個「完整 Toolchain + gtest + clang + lcov」的 image。
* 每個子專案內只需 `docker compose run simulator make test`。

### 6. 測試 (`tests/`)

* **測試覆蓋率 (coverage)**、**靜態分析 (static analysis)**、**格式化檢查 (clang-format)** 可設在頂層 CI。
* 測試檔建議命名 `*_test.cpp`，並以 `CTest` 或 `ctest` 整合跑全部子專案的測試套件。

### 7. 版本控制與 Tag

* 共用程式碼改動時，建議 **先在 `common/` 送 PR**，通過測試後再同步 bump `simulator/`、`atpg/` 版本。
* 發佈可用版本時，以 Git  tag（`v1.2.0-sim`、`v1.0.0-atpg`）標示，確保 build 可重現。

---

## 可能的擴充方向

1. **模組化套件管理**：

   * 若未來想給其他人直接 `#include <fault/Fault.hpp>`，可把 `common` 包成 **頭檔-唯讀 (header-only) 庫** 或 **Conan 套件 (Conan package manager)**。
2. **多語言介面**：

   * 如果 ATPG 需 Python 驅動，可在 `common/bindings/` 放置 **Pybind11** wrapper，產生 `commonpy.so` 供 Jupyter 測試。
3. **文件 (docs) 自動化**：

   * `docs/` 下用 **Doxygen** 產生 API 文件，併入 GitHub Pages。
4. **性能基準 (benchmark)**：

   * `benchmarks/` 子資料夾放 micro-bench、profiling script；可共用 `common` 基礎後再比較 `simulator` 與 `atpg` 的不同演算法效率。

---

### 小結

* **前瞻**：Monorepo + 共用核心 `common/` 讓日後插入第三套系統 (如 ***Emulator***) 依舊乾淨。
* **腳踏實地**：維持既有 `makefile` 亦可，先把 `common` 抽為靜態庫再逐步改用 CMake；不必一次重構到位。
* **專有名詞標註**：Monorepo (單一版本庫), SRP (單一職責原則), CI/CD (持續整合/持續部署), CMake (跨平台建置系統)。

這樣的分層既能保住 **單一職責** 的可維護性，也方便 **共享與擴充**。如有更細的 build 系統或測試腳本問題，隨時再討論！
