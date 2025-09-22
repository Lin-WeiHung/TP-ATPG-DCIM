#ifndef ADDRESS_ALLOCATOR_H
#define ADDRESS_ALLOCATOR_H

#include <utility>
#include <random>
#include "FaultConfig.hpp"

class AddressAllocator {
public:
    AddressAllocator(int rows, int cols, unsigned int seed) 
        : row_(rows), col_(cols), rng_(seed) {
    }
    // Determine addresses for a fault based on its configuration.
    // Returns {aggressor, victim}. For single-cell faults, victim is used and aggressor can be -1.
    std::pair<int,int> allocate(const FaultConfig& config);

private:
    int row_, col_; // Memory dimensions (if needed)
    std::mt19937 rng_;
};

#endif // ADDRESS_ALLOCATOR_H