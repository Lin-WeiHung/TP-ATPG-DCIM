#ifndef MARCH_H
#define MARCH_H

#include <vector>

// Represents a single memory operation (part of a March sequence).
enum class OpType { R, W, CI, CO, UNKNOWN };

enum class Granularity { BIT, WORD };

struct MarchIdx {
    MarchIdx() : marchIdx(-1), opIdx(-1), overallIdx(-1) {}
    MarchIdx( int marchIdx, int opIdx, int overallIdx)
        : marchIdx(marchIdx), opIdx(opIdx), overallIdx(overallIdx) {}

    int marchIdx; // The order of the March element in the sequence
    int opIdx;    // The order of the operation within the March element
    int overallIdx; // Overall order in the March test
    
    bool operator==(const MarchIdx& other) const {
        return marchIdx == other.marchIdx && opIdx == other.opIdx && overallIdx == other.overallIdx;
    }
    bool operator<(const MarchIdx& other) const {
        return overallIdx < other.overallIdx;
    }
};

// A single operation: either a read or write at an address.
class SingleOp {
public:
    // Constructor
    SingleOp() : type_(OpType::R), value_(-1) {}
    SingleOp(OpType type, int value) : type_(type), value_(value) {}
    OpType type_;
    int value_; // For WRITE operations; ignored for READ
    bool operator ==(const SingleOp& other) {
        return type_ == other.type_ && value_ == other.value_;
    }
};

struct PositionedOp {
    PositionedOp() : op_(), idx_() {}
    PositionedOp(const SingleOp& op, const MarchIdx& idx) : op_(op), idx_(idx) {}
    SingleOp op_;
    MarchIdx idx_;
};


// Represents a March operation with multiple SingleOps.
enum class Direction { ASC, DESC, BOTH };

class MarchElement {
public:
    // Constructor
    MarchElement() : addrOrder_(Direction::BOTH), elemIdx_(-1) {}
    Direction addrOrder_; // Address order for this March element (ASC, DESC, BOTH)
    std::vector<PositionedOp> ops_; // Sequence of operations in this March element
    int elemIdx_; // Order in the March sequence
};

#endif // MARCH_H