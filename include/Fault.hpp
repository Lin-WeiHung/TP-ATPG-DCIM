#ifndef FAULT_H
#define FAULT_H

#include <deque>
#include <memory>
#include <optional>
#include "FaultConfig.hpp"
#include "MemoryState.hpp"
#include "March.hpp"

// ────────────────────────────────────────────────
// 1. 共用資料結構
// ────────────────────────────────────────────────

struct OperationRecord {
    int beforeValue {0};
    SingleOp op;

    bool operator==(const OperationRecord& other) const {
        return beforeValue == other.beforeValue &&
               op.type_   == other.op.type_     &&
               op.value_  == other.op.value_;
    }
};

// ────────────────────────────────────────────────
// 2. Trigger Strategy 介面
// ────────────────────────────────────────────────
class ITrigger {
public:
    virtual ~ITrigger() = default;

    /// 每次有 read/write 操作時呼叫，蒐集序列
    virtual void feed(int addr, const SingleOp& op, int beforeValue) = 0;

    /// 是否已完全符合觸發條件
    virtual bool matched() const = 0;

    /// 設置觸發條件（可選，視實作而定）
    virtual void setTrigCond() = 0;

    /// 重置內部狀態（換一個March element時呼叫）
    virtual void reset() = 0;
};

// ────────────────────────────────────────────────
// 2‑1. One‑Cell Sequence Trigger
//     (對 victim 連續觀察到特定操作序列時觸發)
// ────────────────────────────────────────────────
class OneCellSequenceTrigger final : public ITrigger {
public:
    OneCellSequenceTrigger(int vicAddr,
                           std::shared_ptr<const FaultConfig> cfg);

    void feed(int addr, const SingleOp& op, int beforeValue) override;

    bool matched() const override { return matched_; }

    void setTrigCond() override;

    void reset() override {
        history_.clear();
        matched_ = false;
    }

private:
    int vicAddr_;
    std::shared_ptr<const FaultConfig> cfg_;
    std::deque<OperationRecord> pattern_;
    std::deque<OperationRecord> history_;
    bool matched_ {false};
};

// ────────────────────────────────────────────────
// 2‑2. Two‑Cell Coupled Trigger
//     (aggressor 對 victim 產生耦合影響的條件)
// ────────────────────────────────────────────────
class TwoCellCoupledTrigger final : public ITrigger {
public:
    TwoCellCoupledTrigger(int aggrAddr, int vicAddr,
                          std::shared_ptr<const FaultConfig> cfg, std::shared_ptr<MemoryState> mem);

    void feed(int addr, const SingleOp& op, int beforeValue) override;

    bool matched() const override { return matched_; }

    void setTrigCond() override;

    void reset() override {
        history_.clear();
        matched_ = false;
    }

private:
    int aggrAddr_;
    int vicAddr_;
    std::shared_ptr<const FaultConfig> cfg_;
    std::shared_ptr<MemoryState> mem_; // 用於讀取耦合 cell 的值
    std::deque<OperationRecord> pattern_;
    std::deque<OperationRecord> history_;
    int coupledTriggerValue_ {-1}; // 用於記錄 coupled cell 的值
    bool matched_ {false};
};

// ────────────────────────────────────────────────
// 3. Fault 基底 (含共通邏輯)
// ────────────────────────────────────────────────
class IFault {
protected:
    std::shared_ptr<MemoryState> mem_;
    std::shared_ptr<const FaultConfig> cfg_;
    std::unique_ptr<ITrigger> trigger_;
    int vicAddr_ {-1}; // 受影響的 victim cell 地址

    IFault(std::shared_ptr<const FaultConfig> cfg,
              std::shared_ptr<MemoryState> mem,
              std::unique_ptr<ITrigger> trig,
              int vicAddr)
        : mem_(std::move(mem)), cfg_(std::move(cfg)), trigger_(std::move(trig)), vicAddr_(vicAddr) {}

    void payload(); // 實際把 fault value 寫入 victim

public:
    // 由外部顯式呼叫，或由 Factory 內部調用
    virtual void writeProcess(int addr, const SingleOp& op) = 0;
    virtual int  readProcess (int addr, const SingleOp& op) = 0;
    void reset() { trigger_->reset(); }
    virtual ~IFault() = default;
};

// --------------------------------------------------
// 3‑1. OneCellFault (final ‑ 不再被繼承)
// --------------------------------------------------

class OneCellFault final : public IFault {
public:
    OneCellFault(std::shared_ptr<const FaultConfig> cfg,
                 std::shared_ptr<MemoryState> mem,
                 std::unique_ptr<ITrigger> trig,
                 int vicAddr)
        : IFault(std::move(cfg), std::move(mem), std::move(trig), vicAddr) {}
    // Factory 方法，創建 OneCellFault 實例
    // 這裡使用 std::unique_ptr 來確保記憶體管理，並且避免不必要的拷貝
    // 這樣可以確保只有一個實例擁有這個記憶體，並且在不需要時自動釋放
    // 這樣的設計符合 RAII 原則，確保資源的自動管理，並且避免記憶體洩漏或重複釋放的問題
    static std::unique_ptr<OneCellFault> create(std::shared_ptr<const FaultConfig> cfg,
                                                std::shared_ptr<MemoryState> mem,
                                                int vicAddr);

    void writeProcess(int addr, const SingleOp& op) override;
    int  readProcess (int addr, const SingleOp& op) override;
};

// ────────────────────────────────────────────────
// 3‑2. TwoCellFault (final ‑ 與 OneCellFault 並列)
// ────────────────────────────────────────────────
class TwoCellFault final : public IFault {
protected:
    int aggrAddr_ {-1}; // Aggressor cell 地址
public:
    TwoCellFault(std::shared_ptr<const FaultConfig> cfg,
                 std::shared_ptr<MemoryState> mem,
                 std::unique_ptr<ITrigger> trig,
                 int aggrAddr,
                 int vicAddr)
        : IFault(std::move(cfg), std::move(mem), std::move(trig), vicAddr) , aggrAddr_(aggrAddr) {}
    // Factory 方法，創建 TwoCellFault 實例
    static std::unique_ptr<TwoCellFault> create(std::shared_ptr<const FaultConfig> cfg,
                                               std::shared_ptr<MemoryState> mem,
                                               int aggrAddr,
                                               int vicAddr);

    void writeProcess(int addr, const SingleOp& op) override;
    int  readProcess (int addr, const SingleOp& op) override;   
};

// ────────────────────────────────────────────────
// 4. FaultFactory (保留漸進遷移介面)
// ────────────────────────────────────────────────
struct FaultFactory {
    static std::unique_ptr<IFault> makeOneCellFault(std::shared_ptr<const FaultConfig> cfg,
                                                   std::shared_ptr<MemoryState> mem,
                                                   int vicAddr) {
        return OneCellFault::create(std::move(cfg), std::move(mem), vicAddr);
    }

    static std::unique_ptr<IFault> makeTwoCellFault(std::shared_ptr<const FaultConfig> cfg,
                                                   std::shared_ptr<MemoryState> mem,
                                                   int aggrAddr,
                                                   int vicAddr) {
        return TwoCellFault::create(std::move(cfg), std::move(mem), aggrAddr, vicAddr);
    }
};

#endif // FAULT_H