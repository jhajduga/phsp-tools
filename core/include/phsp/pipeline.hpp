#pragma once
#include "phsp/logging.hpp"
#include <filesystem>

namespace phsp {
struct PipelineOptions {
    std::filesystem::path config;
    std::filesystem::path output; // Overrides output_dir; relative to the working directory.
    bool no_plots = false;
    LogOptions logging;
};
std::filesystem::path runPipeline(const PipelineOptions &options);
} // namespace phsp
