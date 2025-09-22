// FaultUnitTests.cpp — self‑contained unit checks for Fault/Trigger logic
// 使用 <cassert> 而非外部測試框架
// 每一組 assert 前面都會標註所對應的 Fault model 方便對照論文 <S/F/R> 表記

#include <cassert>
#include <iostream>
#include <memory>
#include "../include/Fault.hpp"        // Fault / Trigger 主要邏輯
#include "../src/Fault.cpp"
#include "../include/FaultConfig.hpp"  // 使用者定義的配置結構
#include "../include/MemoryState.hpp"  // DenseMemoryState
#include "../src/MemoryState.cpp"

using Op   = OpType;                           // 簡寫
using MI   = MarchIdx;                         // March 位置索引
using SO   = SingleOp;                         // 單一操作
using Dir  = OpType;                           // READ / WRITE

//---------------------------------------------------------------------
// 測試參數 (避免魔術數字)
//---------------------------------------------------------------------
constexpr int VIC_ADDR      = 0;    // victim  cell address
constexpr int AGGR_ADDR     = 1;    // aggressor cell address
constexpr int OTHER_ADDR   = 2;    // 其他 cell address
constexpr int INIT_0        = 0;    // 預設初值 0
constexpr int INIT_1        = 1;    // 預設初值 1
constexpr int DEF_VAL       = -1;   // DenseMemoryState 預設值
constexpr int ROWS          = 4;
constexpr int COLS          = 4;

//---------------------------------------------------------------------
// 便利函式：建立 DenseMemoryState 與 FaultConfig
//---------------------------------------------------------------------
std::shared_ptr<DenseMemoryState> makeMemory(int initVal = DEF_VAL)
{
    return std::make_shared<DenseMemoryState>(ROWS, COLS, initVal);
}

std::shared_ptr<FaultConfig> makeBaseCfg()
{
    return std::make_shared<FaultConfig>();
}

//---------------------------------------------------------------------
// 一些建構 trigger 序列的小工具
//---------------------------------------------------------------------
inline SO W(int value)
{ return SO{Op::W, value}; }

inline SO R(int value)
{ return SO{Op::R,  value}; }

//---------------------------------------------------------------------
// One‑Cell Sequence Trigger 測試
//---------------------------------------------------------------------
void test_OCSeqTrigger_basic()
{
    auto cfg = makeBaseCfg();
    cfg->VI_      = INIT_0;
    cfg->trigger_ = { W(0), R(0) };   // <W0 R0/ ‑/‑>  動態一 cell fault 序列

    OneCellSequenceTrigger trig(VIC_ADDR, cfg);

    // 餵入非 victim 地址 — 不應觸發
    trig.feed(OTHER_ADDR, W(0), INIT_0);
    assert(!trig.matched());
    trig.feed(OTHER_ADDR, R(0), 0);
    assert(!trig.matched());
    
    // 餵入正確序列
    trig.feed(VIC_ADDR, W(0), INIT_0);
    assert(!trig.matched()); // 部分比對 — 尚未觸發
    trig.feed(VIC_ADDR, R(0), 0);
    assert(trig.matched()); // 完整比對 — 應觸發

    // 再次餵入同樣的序列，確保依然能再次觸發
    trig.feed(VIC_ADDR, W(0), INIT_0);
    assert(!trig.matched()); 
    trig.feed(VIC_ADDR, R(0), 0);
    assert(trig.matched()); // 再次觸發成功

    // 先寫入1再餵入同樣的序列
    trig.feed(VIC_ADDR, W(1), INIT_1);
    assert(!trig.matched());
    trig.feed(VIC_ADDR, R(1), 0);
    assert(!trig.matched()); // 未觸發，因為初始值不符合預期
}

//---------------------------------------------------------------------
// Two‑Cell Coupled Trigger 測試 (Sa)
//---------------------------------------------------------------------
void test_TCCoupledTrigger_Sa()
{
    auto mem = makeMemory();
    mem->write(VIC_ADDR, INIT_1);           // Victim 預設為 1

    auto cfg = makeBaseCfg();
    cfg->AI_              = INIT_0;    // aggressor initial value
    cfg->VI_              = INIT_1;    // victim initial value
    cfg->twoCellFaultType_= TwoCellFaultType::Sa; // Sa: 序列作用於 aggressor
    cfg->trigger_         = { W(0), R(0) };       // <0W0R0; 1/‑/‑>

    TwoCellCoupledTrigger trig(AGGR_ADDR, VIC_ADDR, cfg, mem);

    // 正確餵入序列
    trig.feed(AGGR_ADDR, W(0), INIT_0);
    assert(!trig.matched()); // 餵第一個 W0
    trig.feed(AGGR_ADDR, R(0), 0);
    assert(trig.matched()); // 餵第二個 R0 —— 由於 Victim 仍為 1，符合 coupledTriggerValue

    // 再次餵入同樣的序列，應該仍然能觸發
    trig.feed(AGGR_ADDR, W(0), INIT_0);
    assert(!trig.matched()); // 此時應該是R0W0，未觸發
    trig.feed(AGGR_ADDR, R(0), 0);
    assert(trig.matched()); // 再次觸發成功

    // 餵入非 aggressor和victim 地址，應該不影響觸發狀態
    trig.feed(OTHER_ADDR, W(0), INIT_0);
    assert(trig.matched()); // 仍然觸發

    // 餵入錯誤初始值，應該不觸發
    trig.feed(AGGR_ADDR, W(0), INIT_1); // 初始值改為 1
    assert(!trig.matched()); // 不應觸發
    trig.feed(AGGR_ADDR, R(0), 0);
    assert(!trig.matched()); // 仍然不應觸發

    // 更改 Victim 的值，應不觸發
    mem->write(VIC_ADDR, INIT_0); // Victim 改為 0

    trig.feed(AGGR_ADDR, W(0), INIT_0); // 再餵入 W0
    assert(!trig.matched()); // 不應觸發
    trig.feed(AGGR_ADDR, R(0), 0); // 餵入 R0
    assert(!trig.matched()); // 仍然不應觸發
}

//---------------------------------------------------------------------
// Static One‑Cell Fault — W 結尾 (Stuck‑at‑0) [SSF‑W]
//---------------------------------------------------------------------
void test_OneCellFault_SSF_W()
{
    auto mem  = makeMemory(0);
    auto cfg  = makeBaseCfg();
    cfg->VI_            = INIT_0;
    cfg->faultValue_    = INIT_0;      // 故障後 cell 強制 0
    cfg->finalReadValue_= -1;
    cfg->trigger_       = { W(1) };    // S = W1 —— 單操作 (static)

    auto fault = FaultFactory::makeOneCellFault(cfg, mem, VIC_ADDR);

    // 在 fault 之前，嘗試將 cell 寫為 1
    fault->writeProcess(VIC_ADDR, W(1));    // 觸發後 payload 把它寫回 0
    assert(mem->read(VIC_ADDR) == cfg->faultValue_);  // 應 stuck 在 0
    int val = fault->readProcess(VIC_ADDR, R(1)); // 讀取
    assert(val == cfg->faultValue_);
    fault->writeProcess(VIC_ADDR, W(0));    // 觸發後 payload 把它寫回 0
    assert(mem->read(VIC_ADDR) == 0);  // 寫0成功
    val = fault->readProcess(VIC_ADDR, R(0)); // 再次讀取
    assert(val == 0); // 讀取仍然是 0
}

//---------------------------------------------------------------------
// Static One‑Cell Fault — R 結尾 (Stuck‑at‑0 read) [SSF‑R]
//---------------------------------------------------------------------
void test_OneCellFault_SSF_R()
{
    auto mem  = makeMemory(0);
    auto cfg  = makeBaseCfg();
    cfg->VI_            = INIT_0;
    cfg->faultValue_    = INIT_1;
    cfg->finalReadValue_= INIT_0;      // 讀取永遠得到 0
    cfg->trigger_       = { R(0) };     // S = R0 —— 單 READ 操作

    auto fault = FaultFactory::makeOneCellFault(cfg, mem, VIC_ADDR);

    int readVal = fault->readProcess(VIC_ADDR, R(0)); // 立即觸發
    assert(readVal == INIT_0);
    int cellVal = mem->read(VIC_ADDR);
    assert(cellVal == INIT_1); // 實際值仍為 1，但讀取時返回 0
}

//---------------------------------------------------------------------
// Dynamic One‑Cell Fault — W 結尾 (dWDF) [DSF‑W]
//---------------------------------------------------------------------
void test_OneCellFault_DSF_W()
{
    auto mem  = makeMemory(0);
    auto cfg  = makeBaseCfg();
    cfg->VI_            = INIT_0;
    cfg->faultValue_    = INIT_1;      // 寫後 cell 變 1
    cfg->finalReadValue_= -1;     // 無讀取
    cfg->trigger_       = { W(0), W(1) }; // <0W0W1/1/‑>

    auto fault = FaultFactory::makeOneCellFault(cfg, mem, VIC_ADDR);

    fault->writeProcess(VIC_ADDR, W(0));    // 第一操作，不觸發
    fault->writeProcess(VIC_ADDR, W(1));    // 第二操作，觸發
    assert(mem->read(VIC_ADDR) == INIT_1);
}

//---------------------------------------------------------------------
// Dynamic One‑Cell Fault — R 結尾 (dRDF) [DSF‑R]
//---------------------------------------------------------------------
void test_OneCellFault_DSF_R()
{
    auto mem  = makeMemory(0);
    auto cfg  = makeBaseCfg();
    cfg->VI_            = INIT_0;
    cfg->faultValue_    = INIT_0;     
    cfg->finalReadValue_= INIT_1;
    cfg->trigger_       = { W(1), R(1) };   // <0W0R1/0/1>

    auto fault = FaultFactory::makeOneCellFault(cfg, mem, VIC_ADDR);

    fault->writeProcess(VIC_ADDR, W(1));    // 第一操作
    int v = fault->readProcess(VIC_ADDR, R(1)); // 第二操作 => fault
    assert(v == INIT_1);
    assert(mem->read(VIC_ADDR) == INIT_0); // 實際值仍為 0，但讀取時返回 1
}

//---------------------------------------------------------------------
// Static Two‑Cell Fault — Sa, W 結尾 (dCFdsww) [SCF‑Sa‑W]
//---------------------------------------------------------------------
void test_TwoCellFault_SCF_Sa_W()
{
    auto mem = makeMemory(0);
    mem->write(VIC_ADDR, INIT_1);           // Victim 為 1

    auto cfg = makeBaseCfg();
    cfg->AI_              = INIT_0;
    cfg->VI_              = INIT_1;
    cfg->twoCellFaultType_= TwoCellFaultType::Sa;
    cfg->faultValue_      = INIT_0;    // Victim 被破壞為 0
    cfg->finalReadValue_  = -1;
    cfg->trigger_         = { W(0) };  // 靜態、單 W 操作於 aggressor

    auto fault = FaultFactory::makeTwoCellFault(cfg, mem, AGGR_ADDR, VIC_ADDR);

    fault->writeProcess(AGGR_ADDR, W(0));   // 觸發
    assert(mem->read(VIC_ADDR) == INIT_0);
}

//---------------------------------------------------------------------
// Dynamic Two‑Cell Fault — Sv, R 結尾 (dCFdsrr) [DCF‑Sv‑R]
//---------------------------------------------------------------------
void test_TwoCellFault_DCF_Sv_R()
{
    auto mem = makeMemory(0);
    mem->write(AGGR_ADDR, INIT_0);          // Aggressor 狀態 0
    mem->write(VIC_ADDR,  INIT_1);          // Victim 狀態 1

    auto cfg = makeBaseCfg();
    cfg->AI_              = INIT_0;
    cfg->VI_              = INIT_1;
    cfg->twoCellFaultType_= TwoCellFaultType::Sv; // 序列作用於 victim
    cfg->faultValue_      = INIT_0;
    cfg->finalReadValue_  = INIT_0;
    cfg->trigger_         = { R(1), R(1) }; // victim 連續兩次讀取

    auto fault = FaultFactory::makeTwoCellFault(cfg, mem, AGGR_ADDR, VIC_ADDR);

    int tmp = fault->readProcess(VIC_ADDR, R(1)); // 第一 R
    assert(tmp == INIT_1); // 正確讀取1
    int val = fault->readProcess(VIC_ADDR, R(1)); // 第二 R => 觸發
    assert(val == cfg->finalReadValue_);
    assert(mem->read(VIC_ADDR) == cfg->faultValue_); // Victim 現在應為 0
}

//---------------------------------------------------------------------
int main()
{
    test_OCSeqTrigger_basic();
    test_TCCoupledTrigger_Sa();
    test_OneCellFault_SSF_W();
    test_OneCellFault_SSF_R();
    test_OneCellFault_DSF_W();
    test_OneCellFault_DSF_R();
    test_TwoCellFault_SCF_Sa_W();
    test_TwoCellFault_DCF_Sv_R();

    std::cout << "[All Fault & Trigger asserts passed]" << std::endl;
    return 0;
}
