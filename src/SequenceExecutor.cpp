#include "../include/SequenceExecutor.hpp"

void SequenceExecutor::execute( const std::vector<MarchElement>& marchTest, IFault& fault) {
    if (memSize_ <= 0 || marchTest.empty()) {
        // No memory to simulate or no operations to execute
        return;
    }
    for (const auto& elem : marchTest) {
        fault.reset(); // Reset fault state for each March element
        if (elem.addrOrder_ == Direction::ASC || elem.addrOrder_ == Direction::BOTH) {
            // Process operations in ascending order
            for (int addr = 0; addr < memSize_; ++addr) {
                execBit(elem, fault, addr);
            }
        } else if (elem.addrOrder_ == Direction::DESC) {
            // Process operations in descending order
            for (int addr = memSize_ - 1; addr >= 0; --addr) {
                execBit(elem, fault, addr);
            }
        }
    }
}

void SequenceExecutor::execBit(const MarchElement& elem, IFault& fault, int mem_idx) {
    for (const auto& op : elem.ops_) {
        if (op.op_.type_ == OpType::R) {
            // Read operation
            int value = fault.readProcess(mem_idx, op.op_);
            if (value != op.op_.value_) {
                collector_.opRecord(op.idx_, mem_idx, true); // Record detection
            } else {
                collector_.opRecord(op.idx_, mem_idx, false); // Record no detection
            }
        } else if (op.op_.type_ == OpType::W) {
            // Write operation
            fault.writeProcess(mem_idx, op.op_);
        }
    }
}