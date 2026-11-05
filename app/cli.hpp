#pragma once
#include "phsp/logging.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace cli {
inline long long integer(const std::string &text, long long min, long long max) {
    size_t end = 0;
    const auto value = std::stoll(text, &end);
    if (end != text.size() || value < min || value > max)
        throw std::invalid_argument("Integer outside allowed range: " + text);
    return value;
}
inline double number(const std::string &text) {
    size_t end = 0;
    const auto value = std::stod(text, &end);
    if (end != text.size() || !std::isfinite(value))
        throw std::invalid_argument("Invalid number: " + text);
    return value;
}
template <class Value> bool logging(const std::string &arg, Value &&value, phsp::LogOptions &options) {
    if (arg == "--log-level")
        options.level = phsp::parseLogLevel(value());
    else if (arg == "--log-file")
        options.file = value();
    else if (arg == "--progress-seconds") {
        options.progress_seconds = number(value());
        if (options.progress_seconds < 0)
            throw std::invalid_argument("--progress-seconds must be nonnegative");
    } else
        return false;
    return true;
}
inline void loggingHelp() {
    std::cout << "  --log-level LEVEL   DEBUG, INFO (default), WARNING, ERROR\n"
              << "  --log-file FILE     Override the log beside the output\n"
              << "  --progress-seconds N  Update every N seconds (default: 5; 0: stage boundaries only)\n";
}
} // namespace cli
