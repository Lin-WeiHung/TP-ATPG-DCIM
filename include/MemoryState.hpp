#ifndef MEMORY_STATE_H
#define MEMORY_STATE_H

#include <unordered_map>
#include <vector>

// Base class representing memory with read/write operations.
class MemoryState {
public:
    // Initialize memory with a default value for uninitialized cells.
    MemoryState(int defaultValue) : defaultValue_(defaultValue) {}
    virtual ~MemoryState() = default;
    
    // Write a value to the given address.
    virtual void write(int address, int value) = 0;
    
    // Read a value from the given address. Returns defaultValue_ if not initialized.
    virtual int read(int address) const = 0;

    virtual void reset() = 0;
    
protected:
    int defaultValue_;
};

// Dense memory implementation using a contiguous vector.
// Uninitialized cells are set to default value.
class DenseMemoryState final: public MemoryState {
public:
    // Construct with given memory size and default value.
    DenseMemoryState(int row, int col, int defaultValue)
        : MemoryState(defaultValue), data_(row * col, defaultValue) {}

    // Write a value to the given address.
    void write(int address, int value) override;

    // Read a value from the given address.
    int read(int address) const override;

    // Reset the memory to the default value.
    void reset() override {
        std::fill(data_.begin(), data_.end(), defaultValue_);
    }

private:
    std::vector<int> data_;
};

#endif // MEMORY_STATE_H