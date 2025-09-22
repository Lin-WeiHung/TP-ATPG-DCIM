#include "../include/AddressAllocator.hpp"

std::pair<int, int> AddressAllocator::allocate(const FaultConfig& config) {
    // For single-cell faults, aggressor is not used
    if (!config.is_twoCell_) {
        // Allocate victim address
        std::uniform_int_distribution<int> dist(0, col_ * row_ - 1);
        int victimAddr = dist(rng_);
        return { -1, victimAddr }; // Aggressor is -1 for single-cell faults
    }
    // For two-cell faults, allocate both aggressor and victim addresses

    std::uniform_int_distribution<int> dist(1, col_ * row_ - 1);
    int highAddr = dist(rng_);
    int lowAddr;
    if (highAddr / col_ == 0) {
        lowAddr = highAddr - 1;
    } else if (highAddr % col_ == 0) {
        lowAddr = highAddr - col_;
    } else {
        // Randomly choose above or left
        std::uniform_int_distribution<int> choice(0, 1);
        lowAddr = (choice(rng_) == 0) ? highAddr - col_ : highAddr - 1;
    }
    
    if (config.is_A_less_than_V_) {
        // If aggressor < victim, return aggressor first
        return { lowAddr, highAddr };
    } else if (!config.is_A_less_than_V_) {
        // If aggressor > victim, return victim first
        return { highAddr, lowAddr };
    }
    return { -1, -1 }; // Fallback case, should not happen
}