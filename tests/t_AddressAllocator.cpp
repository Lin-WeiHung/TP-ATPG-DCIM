#include <iostream>
#include <cassert>
#include "../include/AddressAllocator.hpp"
#include "../src/AddressAllocator.cpp"

void testSingleCellFault() {
    AddressAllocator allocator(10, 10, 12345); // 10x10 memory grid, seed = 12345

    FaultConfig singleCellFault;
    singleCellFault.is_twoCell_ = false; // Single-cell fault

    auto result = allocator.allocate(singleCellFault);
    std::cout << "Single-cell fault: Aggressor = " << result.first << ", Victim = " << result.second << "\n";

    // Assert that aggressor is -1 for single-cell faults
    assert(result.first == -1);
    assert(result.second >= 0 && result.second < 100); // Victim address should be within bounds
}

void testTwoCellFaultAggressorLessThanVictim() {
    AddressAllocator allocator(10, 10, 12345); // 10x10 memory grid, seed = 12345

    FaultConfig twoCellFault;
    twoCellFault.is_twoCell_ = true; // Two-cell fault
    twoCellFault.is_A_less_than_V_ = true; // Aggressor < Victim

    auto result = allocator.allocate(twoCellFault);
    std::cout << "Two-cell fault (Aggressor < Victim): Aggressor = " << result.first << ", Victim = " << result.second << "\n";

    // Assert that aggressor < victim
    assert(result.first < result.second);
    assert(result.first >= 0 && result.first < 100); // Aggressor address should be within bounds
    assert(result.second >= 0 && result.second < 100); // Victim address should be within bounds
}

void testTwoCellFaultAggressorGreaterThanVictim() {
    AddressAllocator allocator(10, 10, 12345); // 10x10 memory grid, seed = 12345

    FaultConfig twoCellFault;
    twoCellFault.is_twoCell_ = true; // Two-cell fault
    twoCellFault.is_A_less_than_V_ = false; // Aggressor > Victim

    auto result = allocator.allocate(twoCellFault);
    std::cout << "Two-cell fault (Aggressor > Victim): Aggressor = " << result.first << ", Victim = " << result.second << "\n";

    // Assert that aggressor > victim
    assert(result.first > result.second);
    assert(result.first >= 0 && result.first < 100); // Aggressor address should be within bounds
    assert(result.second >= 0 && result.second < 100); // Victim address should be within bounds
}

int main() {
    std::cout << "Running AddressAllocator tests...\n";

    testSingleCellFault();
    testTwoCellFaultAggressorLessThanVictim();
    testTwoCellFaultAggressorGreaterThanVictim();

    std::cout << "All AddressAllocator tests passed!\n";
    return 0;
}