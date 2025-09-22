# include "../include/Fault.hpp"

// === OneCellSequenceTrigger ===
OneCellSequenceTrigger::OneCellSequenceTrigger(int vicAddr,
                                               std::shared_ptr<const FaultConfig> cfg)
    : vicAddr_(vicAddr), cfg_(std::move(cfg)) {
    setTrigCond();
}

void OneCellSequenceTrigger::feed(int addr, const SingleOp& op, int beforeValue) {
    if (addr != vicAddr_) {
        matched_ = false; // 只要餵入非 victim cell 的操作，就重置 matched 狀態
        return;
    }
    history_.push_back({beforeValue, op});
    if (history_.size() > pattern_.size()) history_.pop_front();
    matched_ = (history_ == pattern_);
}

void OneCellSequenceTrigger::setTrigCond() {
    pattern_.clear();
    for (size_t i = 0; i < cfg_->trigger_.size(); ++i) {
        if (i == 0) {
            pattern_.emplace_back(OperationRecord{cfg_->VI_, cfg_->trigger_[i]});
            continue;
        }
        pattern_.emplace_back(OperationRecord{cfg_->trigger_[i-1].value_, cfg_->trigger_[i]});
    }
}

// === TwoCellCoupledTrigger ===
TwoCellCoupledTrigger::TwoCellCoupledTrigger(int aggrAddr, int vicAddr,
                                             std::shared_ptr<const FaultConfig> cfg,
                                             std::shared_ptr<MemoryState> mem)
    : aggrAddr_(aggrAddr), vicAddr_(vicAddr), cfg_(std::move(cfg)), 
      mem_(std::move(mem)) {
    setTrigCond();
}

void TwoCellCoupledTrigger::feed(int addr, const SingleOp& op, int beforeValue) {
    if ((cfg_->twoCellFaultType_ == TwoCellFaultType::Sa && addr == aggrAddr_) ||
            (cfg_->twoCellFaultType_ == TwoCellFaultType::Sv && addr == vicAddr_)) {
        history_.push_back({beforeValue, op});
        if (history_.size() > pattern_.size()) history_.pop_front();
        if (history_ == pattern_) {
            if (cfg_->twoCellFaultType_ == TwoCellFaultType::Sa) {
                // 如果是 Sa，則需要讀取 coupled cell 的值
                int coupledValue = mem_->read(vicAddr_);
                if (coupledValue == coupledTriggerValue_) {
                    matched_ = true;
                    return;
                }
            } else if (cfg_->twoCellFaultType_ == TwoCellFaultType::Sv) {
                // 如果是 Sv，則需要讀取 coupled cell 的值
                int coupledValue = mem_->read(aggrAddr_);
                if (coupledValue == coupledTriggerValue_) {
                    matched_ = true;
                    return;
                }
            }
        }
        matched_ = false; // 只要有餵入就重置 matched 狀態
    } 
}

void TwoCellCoupledTrigger::setTrigCond() {
    pattern_.clear();
    for (size_t i = 0; i < cfg_->trigger_.size(); ++i) {
        if (i == 0) {
            if (cfg_->twoCellFaultType_ == TwoCellFaultType::Sa) {
                pattern_.emplace_back(OperationRecord{cfg_->AI_, cfg_->trigger_[i]});
            } else {
                pattern_.emplace_back(OperationRecord{cfg_->VI_, cfg_->trigger_[i]});
            }
            continue;
        }
        pattern_.emplace_back(OperationRecord{cfg_->trigger_[i-1].value_, cfg_->trigger_[i]});
    }
    // 如果是耦合觸發，還需要記錄 coupled cell 的值
    if (cfg_->twoCellFaultType_ == TwoCellFaultType::Sa) {
        coupledTriggerValue_ = cfg_->VI_;
    } else if (cfg_->twoCellFaultType_ == TwoCellFaultType::Sv) {
        coupledTriggerValue_ = cfg_->AI_;
    }
}

// === IFault::injectFault ===
void IFault::payload() {
    // 假設 FaultConfig 帶有 victim address & fault value
    mem_->write(vicAddr_, cfg_->faultValue_);
}

// === OneCellFault ===
std::unique_ptr<OneCellFault> OneCellFault::create(std::shared_ptr<const FaultConfig> cfg,
                                                   std::shared_ptr<MemoryState> mem,
                                                   int vicAddr) {
    auto trig = std::make_unique<OneCellSequenceTrigger>(vicAddr, cfg);
    return std::unique_ptr<OneCellFault>(new OneCellFault(
        std::move(cfg), std::move(mem), std::move(trig), vicAddr));
}

void OneCellFault::writeProcess(int addr, const SingleOp& op) {
    int before = mem_->read(addr);
    mem_->write(addr, op.value_);
    trigger_->feed(addr, op, before);
    if (trigger_->matched()) {
        payload();
        return; 
    }
}

int OneCellFault::readProcess(int addr, const SingleOp& op) {
    int before = mem_->read(addr);
    trigger_->feed(addr, op, before);
    if (trigger_->matched()) {
        payload();
        return cfg_->finalReadValue_; // 返回故障後的值
    }
    return mem_->read(addr);
}

// === TwoCellFault ===
std::unique_ptr<TwoCellFault> TwoCellFault::create(std::shared_ptr<const FaultConfig> cfg,
                                                   std::shared_ptr<MemoryState> mem,
                                                   int aggrAddr,
                                                   int vicAddr) {
    auto trig = std::make_unique<TwoCellCoupledTrigger>(aggrAddr, vicAddr, cfg, mem);
    return std::unique_ptr<TwoCellFault>(new TwoCellFault(std::move(cfg), std::move(mem), std::move(trig), 
                                                         aggrAddr, vicAddr));
}

void TwoCellFault::writeProcess(int addr, const SingleOp& op) {
    int before = mem_->read(addr);
    trigger_->feed(addr, op, before);
    if (trigger_->matched()) {
        payload();
        return; 
    }
    mem_->write(addr, op.value_);
}

int TwoCellFault::readProcess(int addr, const SingleOp& op) {
    int before = mem_->read(addr);
    trigger_->feed(addr, op, before);
    if (trigger_->matched()) {
        payload();
        return cfg_->finalReadValue_; // 返回故障後的值
    }
    return mem_->read(addr);
}