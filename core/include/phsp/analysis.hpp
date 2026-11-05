#pragma once

#include "phsp/logging.hpp"

#include <filesystem>
#include <optional>
#include <string>

namespace phsp {
struct AnalysisOptions {
    std::filesystem::path input;
    std::filesystem::path output;
    unsigned threads = 1;
    long long max_records = -1;
    int bins = 120;
    std::optional<double> energy_max;
    std::optional<double> xy_max;
    // Shared target-centred +Z reference frame. Null selects the native surface.
    std::optional<double> project_z = 100.0;
    double source_z_offset_cm = 0.0;
    int particle = 0; // 0 = all, IAEA codes 1..5
    bool plots = true;
    bool histories = true;
    LogOptions logging;
};

// Creates analysis.root, report.json, report.md, report.html and plots/.
// Returns a report directory; malformed inputs raise std::runtime_error.
std::filesystem::path analyze(const AnalysisOptions &options);
} // namespace phsp
