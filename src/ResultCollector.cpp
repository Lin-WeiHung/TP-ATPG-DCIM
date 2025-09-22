# include "../include/ResultCollector.hpp"
    
void OneByOneResultCollector::opRecord(const MarchIdx& idx, int addr, bool isDetected) {
    report_.detected_[idx] = report_.detected_[idx] || isDetected;
    report_.isDetected_ = report_.isDetected_ || isDetected; // At least one detection occurred
    if (isDetected) {
        report_.detectedVicAddrs_.insert(addr); // Add the detected victim address
    }
}

