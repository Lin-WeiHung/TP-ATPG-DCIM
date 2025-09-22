#ifndef FAULT_CONFIG_H
#define FAULT_CONFIG_H

#include <string>
#include <vector>
#include <functional>
#include "March.hpp"
#include "DetectionReport.hpp"

enum class TwoCellFaultType { Sa, Sv };
// Represents configuration for a fault from input.

struct FaultID {
    std::string faultName_; // Name of the fault (e.g., "StuckAt", "Transition", etc.)
    int subcaseIdx_;        // Index of the subcase for this fault

    bool operator==(const FaultID& other) const {
        return faultName_ == other.faultName_ && subcaseIdx_ == other.subcaseIdx_;
    }
};

// Hash function for FaultID to use in unordered containers
namespace std {
    template <>
    struct hash<FaultID> {
        std::size_t operator()(const FaultID& fid) const {
            std::size_t h1 = std::hash<std::string>{}(fid.faultName_);
            std::size_t h2 = std::hash<int>{}(fid.subcaseIdx_);
            return h1 ^ (h2 << 1);
        }
    };
}

class FaultConfig {
public:
    // Constructor to initialize with basic parameters
    FaultConfig()
        : VI_(-1), faultValue_(-1), finalReadValue_(-1),
          is_twoCell_(false), twoCellFaultType_(TwoCellFaultType::Sa), AI_(-1) {}

    // Basic information about the fault
    FaultID id_; // Unique identifier for the fault
    int VI_;            // Initial victim value
    int CI_;
    std::vector<SingleOp> trigger_;  // Trigger sequence for the fault on the aggressor or victim cell
    int faultValue_;      // The value to inject for the fault
    int finalReadValue_; // Expected value after the fault is injected (if applicable)

    // Additional parameters for the two-cell fault
    bool is_twoCell_;     // Indicates if this is a two-cell fault
    bool is_A_less_than_V_; // Indicates if aggressor < victim (for two-cell faults)
    TwoCellFaultType twoCellFaultType_; // Type of fault (e.g., "Sa", "Sv", etc.)
    int AI_;            // Initial aggressor value (if applicable)
    int CAI_;

    DetectionReport init0_healthReport_; // Report on the health of the fault
    DetectionReport init1_healthReport_; // Report on the health of the fault
};

#endif // FAULT_CONFIG_H