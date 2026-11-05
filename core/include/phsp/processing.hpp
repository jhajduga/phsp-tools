#pragma once
#include "phsp/logging.hpp"
#include <filesystem>
#include <vector>

namespace phsp {
struct CutOptions {
    std::filesystem::path input, output;
    double project_z_cm = 100.0;
    double source_z_offset_cm = 0.0;
    double x_min_cm = -7.0, x_max_cm = 7.0, y_min_cm = -7.0, y_max_cm = 7.0;
    long long max_records = -1; // Input prefix, not accepted-particle count.
    bool drop_extras = false;
    bool allow_incomplete = false;
    bool write_projected = false;
    bool positive_z_only = true;
    LogOptions logging;
};
struct MergeOptions {
    std::vector<std::filesystem::path> inputs;
    std::filesystem::path output;
    long long max_records_per_input = -1;
    bool drop_extras = false;
    bool allow_incomplete = false;
    LogOptions logging;
};
// Return the output IAEA basename. Both operations retain history increments.
std::filesystem::path cut(const CutOptions &options);
std::filesystem::path merge(const MergeOptions &options);
} // namespace phsp
