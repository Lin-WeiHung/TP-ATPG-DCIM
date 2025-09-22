#include <iostream>
#include <memory>
#include <cassert>
#include "../include/SequenceExecutor.hpp"
#include "../src/SequenceExecutor.cpp"
#include "../include/Fault.hpp"
#include "../src/Fault.cpp"
#include "../include/ResultCollector.hpp"
#include "../src/ResultCollector.cpp"


class StubFault : public IFault {
public:
    StubFault(bool invert = false) : IFault(nullptr, nullptr, nullptr, -1), invert_(invert) {}
    int readProcess(int addr, const SingleOp& op) override {
        return invert_ == 1 ? op.value_ ^ 1 : op.value_;
    }
    void writeProcess(int addr, const SingleOp& op) override {return;}
    bool invert_{0};
};
class StubCollector : public IResultCollector {
public:
    void opRecord(const MarchIdx& idx, int addr, bool isDetected) override { hits_.push_back({idx, addr, isDetected}); }
    DetectionReport getReport() const override { return DetectionReport(); }
    void reset() override { hits_.clear(); } // Reset the collector for a new simulation
    std::vector<std::tuple<const MarchIdx&, int, bool>> hits_;
};

// ---------------------------------
// 工具：產生「寫 0 → 讀 0」的 MarchElement
// ---------------------------------
static MarchElement genWriteRead0(Direction dir, int elemIdx = 0) {
    MarchElement elem;
    elem.addrOrder_ = dir;
    elem.elemIdx_   = elemIdx;

    // op0 : W0
    PositionedOp w0;
    w0.op_  = SingleOp(OpType::W, 0);
    w0.idx_ = MarchIdx(elemIdx, 0, 0);

    // op1 : R0
    PositionedOp r0;
    r0.op_  = SingleOp(OpType::R, 0);
    r0.idx_ = MarchIdx(elemIdx, 1, 1);

    elem.ops_ = { w0, r0 };
    return elem;
}

void testNormalASC() {
    const int memSize = 4;
    std::vector<MarchElement> march { genWriteRead0(Direction::ASC) };
    StubFault fault(false);
    StubCollector col;
    SequenceExecutor exe(memSize, col);
    exe.execute(march, fault);
    assert(col.hits_.empty());  // 不應偵測到任何故障
}

void testInvertASC() {
    const int memSize = 4;
    std::vector<MarchElement> march { genWriteRead0(Direction::ASC) };
    StubFault fault(true);
    StubCollector col;
    SequenceExecutor exe(memSize, col);
    exe.execute(march, fault);
    assert(col.hits_.size() == memSize);  // 會有一個Read偵測到故障
}

void testNormalBOTH() {
    const int memSize = 4;
    std::vector<MarchElement> march { genWriteRead0(Direction::BOTH) };
    StubFault fault(false);
    StubCollector col;
    SequenceExecutor exe(memSize, col);
    exe.execute(march, fault);
    assert(col.hits_.empty());  // 不應偵測到任何故障
}

void testInvertBOTH() {
    const int memSize = 4;
    std::vector<MarchElement> march { genWriteRead0(Direction::BOTH) };
    StubFault fault(true);
    StubCollector col;
    SequenceExecutor exe(memSize, col);
    exe.execute(march, fault);
    assert(col.hits_.size() == memSize);  // 會有一個Read偵測到故障
}

void testNormalDESC() {
    const int memSize = 4;
    std::vector<MarchElement> march { genWriteRead0(Direction::DESC) };
    StubFault fault(false);
    StubCollector col;
    SequenceExecutor exe(memSize, col);
    exe.execute(march, fault);
    assert(col.hits_.empty());  // 不應偵測到任何故障
}

void testInvertDESC() {
    const int memSize = 4;
    std::vector<MarchElement> march { genWriteRead0(Direction::DESC) };
    StubFault fault(true);
    StubCollector col;
    SequenceExecutor exe(memSize, col);
    exe.execute(march, fault);
    assert(col.hits_.size() == memSize);  // 會有一個Read偵測到故障
}

void testEmptyMarch() {
    const int memSize = 4;
    std::vector<MarchElement> march;  // 空的 March
    StubFault fault(false);
    StubCollector col;
    SequenceExecutor exe(memSize, col);
    exe.execute(march, fault);
    assert(col.hits_.empty());  // 不應偵測到任何故障
}



// ---------------------------------
// main() 測試入口
// ---------------------------------
int main() {
    testNormalASC();
    testInvertASC();
    testNormalBOTH();
    testInvertBOTH();
    testNormalDESC();
    testInvertDESC();
    testEmptyMarch();
    std::cout << "All tests passed!" << std::endl;

    return 0;    // 全部通過
}
