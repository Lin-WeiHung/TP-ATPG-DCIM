# include "../include/FaultSimulator.hpp"

OneByOneFaultSimulator::OneByOneFaultSimulator(std::vector<FaultConfig>& faultConfigs,
                                               const std::vector<MarchElement>& marchTest,
                                               int rows, int cols, int seed)
    : cfg_(faultConfigs), marchTest_(marchTest), rows_(rows), cols_(cols) {
    collector_ = std::make_unique<OneByOneResultCollector>();
    addrAllocator_ = std::make_unique<AddressAllocator>(rows, cols, seed);
}

void OneByOneFaultSimulator::run_0() {
    mem_ = std::make_shared<DenseMemoryState>(rows_, cols_, 0);
    for (auto& faultConfig : cfg_) {
        // Reset memory state for each fault configuration
        mem_->reset();
        collector_->reset();
        int aggressorAddr, victimAddr;
        // Allocate addresses for the aggressor and victim cells
        std::tie(aggressorAddr, victimAddr) = addrAllocator_->allocate(faultConfig);

        // Execute the March test sequence
        auto fault = faultConfig.is_twoCell_ ?
            FaultFactory::makeTwoCellFault(std::make_shared<const FaultConfig>(faultConfig), mem_, aggressorAddr, victimAddr) :
            FaultFactory::makeOneCellFault(std::make_shared<const FaultConfig>(faultConfig), mem_, victimAddr);
        SequenceExecutor executor(rows_ * cols_, *collector_);
        executor.execute(marchTest_, *fault);
        faultConfig.init0_healthReport_ = collector_->getReport();
        if (faultConfig.init0_healthReport_.isDetected_) {
            detectedCount_++;
        }
    }
}

void OneByOneFaultSimulator::run_1() {
    mem_ = std::make_shared<DenseMemoryState>(rows_, cols_, 1);
    for (auto& faultConfig : cfg_) {
        // Reset memory state for each fault configuration
        mem_->reset();
        collector_->reset();
        int aggressorAddr, victimAddr;
        // Allocate addresses for the aggressor and victim cells
        std::tie(aggressorAddr, victimAddr) = addrAllocator_->allocate(faultConfig);

        // Execute the March test sequence
        auto fault = faultConfig.is_twoCell_ ?
            FaultFactory::makeTwoCellFault(std::make_shared<const FaultConfig>(faultConfig), mem_, aggressorAddr, victimAddr) :
            FaultFactory::makeOneCellFault(std::make_shared<const FaultConfig>(faultConfig), mem_, victimAddr);
        SequenceExecutor executor(rows_ * cols_, *collector_);
        executor.execute(marchTest_, *fault);
        faultConfig.init1_healthReport_ = collector_->getReport();
        if (faultConfig.init1_healthReport_.isDetected_) {
            detectedCount_++;
        }
    }
}