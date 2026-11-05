#pragma once

#include "phsp/logging.hpp"
#include <filesystem>

namespace phsp {
struct ComparisonOptions {
    std::filesystem::path reference;
    std::filesystem::path candidate;
    std::filesystem::path output;
    bool plots = true;
    LogOptions logging;
};
std::filesystem::path compare(const ComparisonOptions &options);
} // namespace phsp
