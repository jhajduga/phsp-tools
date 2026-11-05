#pragma once

#include "phsp/logging.hpp"
#include <cstddef>
#include <filesystem>

namespace phsp {
struct ConversionOptions {
    std::filesystem::path input;
    std::filesystem::path output;
    unsigned threads = 4;
    std::size_t block_size = 200000;
    std::size_t queue_capacity = 8;
    long long max_records = -1;
    long long stride = 1;
    bool extras = false;
    bool allow_incomplete = false;
    LogOptions logging;
};
std::filesystem::path convert(const ConversionOptions &options);
} // namespace phsp
