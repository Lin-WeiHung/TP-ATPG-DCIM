#include "../include/MemoryState.hpp"

// Write: if address is beyond current size, resize to accommodate.
void DenseMemoryState::write(int address, int value) {
    if (address < 0 || address >= static_cast<int>(data_.size())) return;
    data_[address] = value;
}

// Read: return value at address or default if out-of-range.
int DenseMemoryState::read(int address) const {
    if (address < 0 || address >= static_cast<int>(data_.size())) {
        return defaultValue_;
    }
    return data_[address];
}