#ifndef DETECTION_REPORT_H
#define DETECTION_REPORT_H

#include <vector>
#include <map>
#include <set>
#include "March.hpp"

// Represents the result of the fault simulation for reporting.
class DetectionReport {
public:
    DetectionReport() : isDetected_(false), detected_() {}
    bool isDetected_; // Whether the fault was detected

    // std::set<int> expectedAggrAddrs_; // Set of expected aggressor addresses
    // std::set<int> detectedAggrAddrs_; // Set of detected aggressor addresses

    std::set<int> detectedVicAddrs_; // Set of detected victim addresses

    std::map<MarchIdx, bool> detected_; // Map of MarchIdx to detection status
};

#endif // DETECTION_REPORT_H