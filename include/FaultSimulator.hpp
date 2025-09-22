#include "AddressAllocator.hpp"
#include "DetectionReport.hpp"
#include "Fault.hpp"
#include "FaultConfig.hpp"
#include "March.hpp"
#include "MemoryState.hpp"
#include "ResultCollector.hpp"
#include "SequenceExecutor.hpp"
#include <unordered_map>

class IFaultSimulator {
public:
    virtual ~IFaultSimulator() = default;
    // Execute a March test with the given fault configuration and memory state.
    virtual void run() = 0;
    virtual double getDetectedRate() = 0;
};

class OneByOneFaultSimulator final: public IFaultSimulator {
public:
    OneByOneFaultSimulator(std::vector<FaultConfig>& faultConfigs,
              const std::vector<MarchElement>& marchTest,
              int rows, int cols, int seed);
    ~OneByOneFaultSimulator() = default;
    void run() override {
        run_0();
        run_1();
    }
    void run_0();
    void run_1();
    double getDetectedRate() override {
        return static_cast<double>(detectedCount_) / (cfg_.size() * 2);
    }
protected:
    std::vector<FaultConfig>& cfg_; // Fault configurations
    const std::vector<MarchElement>& marchTest_; // March test sequence
    int rows_;
    int cols_;
    int detectedCount_{0}; // Count of detections
    std::shared_ptr<MemoryState> mem_;// Memory state
    std::unique_ptr<IResultCollector> collector_; // Result collector
    std::unique_ptr<AddressAllocator> addrAllocator_; // Address allocator
};