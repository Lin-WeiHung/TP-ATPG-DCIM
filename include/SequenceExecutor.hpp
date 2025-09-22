#ifndef SEQUENCE_EXECUTOR_H
#define SEQUENCE_EXECUTOR_H

#include <vector>
#include <memory>
#include "March.hpp"
#include "MemoryState.hpp"
#include "Fault.hpp"
#include "ResultCollector.hpp"

// Executes a sequence of memory operations (March pattern),
// coordinating fault injection and detection.
class SequenceExecutor {
public:
    SequenceExecutor(int memorySize, IResultCollector& collector)
        : memSize_(memorySize), collector_(collector) {}

    // Execute a sequence of single operations with a set of faults.
    // faults: list of fault objects to inject/check during simulation.
    void execute(const std::vector<MarchElement>& marchTest, IFault& fault);

private:
    void execBit(const MarchElement&, IFault&, int addr);
    void execWord(const MarchElement&, IFault&, int row);
    int memSize_; // Size of the memory to simulate
    IResultCollector& collector_;
};

#endif // SEQUENCE_EXECUTOR_H