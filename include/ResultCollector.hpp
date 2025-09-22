#ifndef RESULT_COLLECTOR_H
#define RESULT_COLLECTOR_H

#include "DetectionReport.hpp"

// Collects fault detection results during simulation.
class IResultCollector {
public:
    virtual void opRecord(const MarchIdx& idx, int addr, bool isDetected) = 0;
    virtual DetectionReport getReport() const = 0;
    virtual void reset() = 0; // Reset the collector for a new simulation
    virtual ~IResultCollector() = default;
};

class OneByOneResultCollector : public IResultCollector {
public:
    void opRecord(const MarchIdx& idx, int addr, bool isDetected) override;
    DetectionReport getReport() const override { return report_; }
    void reset() override { report_ = DetectionReport(); } // Reset the report
private:
    // Map of fault IDs to their detection reports
    DetectionReport report_;
};

#endif // RESULT_COLLECTOR_H