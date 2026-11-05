#include "phsp/logging.hpp"

#include <cmath>
#include <condition_variable>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace phsp {
namespace {
std::mutex console_mutex;
const char *name(LogLevel level) {
    switch (level) {
    case LogLevel::debug:
        return "DEBUG";
    case LogLevel::info:
        return "INFO";
    case LogLevel::warning:
        return "WARN";
    case LogLevel::error:
        return "ERROR";
    }
    return "INFO";
}
std::string duration(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0)
        return "--";
    const auto n = static_cast<std::uint64_t>(seconds);
    return fmt::format("{:02}:{:02}:{:02}", n / 3600, (n / 60) % 60, n % 60);
}
} // namespace
LogLevel parseLogLevel(std::string_view value) {
    if (value == "DEBUG")
        return LogLevel::debug;
    if (value == "INFO")
        return LogLevel::info;
    if (value == "WARNING" || value == "WARN")
        return LogLevel::warning;
    if (value == "ERROR")
        return LogLevel::error;
    throw std::invalid_argument("Log level must be DEBUG, INFO, WARNING or ERROR");
}
Logger::Logger(std::string module, const LogOptions &options, const std::filesystem::path &default_file)
    : module_(std::move(module)), level_(options.level),
      path_(std::filesystem::absolute(options.file.empty() ? default_file : options.file)),
      started_(std::chrono::steady_clock::now()) {
    if (!std::isfinite(options.progress_seconds) || options.progress_seconds < 0)
        throw std::invalid_argument("Progress interval must be finite and nonnegative");
    std::filesystem::create_directories(path_.parent_path());
    file_.open(path_, std::ios::app);
    if (!file_)
        throw std::runtime_error("Cannot open log: " + path_.string());
    info("Log: {}", path_.string());
}
void Logger::write(LogLevel level, const std::string &message) const {
    if (level < level_)
        return;
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
    gmtime_r(&now, &tm);
    std::ostringstream date;
    date << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started_).count();
    const auto line =
        fmt::format("{} +{:8.2f}s [{:5}] [{}] {}\n", date.str(), elapsed, name(level), module_, message);
    std::scoped_lock lock(mutex_, console_mutex);
    std::cerr << line << std::flush;
    file_ << line << std::flush;
    if (!file_)
        throw std::runtime_error("Log write failed: " + path_.string());
}
Progress::Progress(const Logger &logger, double interval_seconds)
    : logger_(logger), interval_(interval_seconds) {}
Progress::~Progress() {
    stop();
    if (active_) {
        // Destructors must not mask the exception which interrupted a stage.
        try {
            report("INTERRUPTED");
        } catch (...) {
        }
    }
}
void Progress::stop() {
    if (heartbeat_.joinable()) {
        heartbeat_.request_stop();
        heartbeat_.join();
    }
}
void Progress::start(std::string stage, std::uint64_t total, unsigned slots) {
    if (active_)
        finish();
    stage_ = std::move(stage);
    total_ = total;
    slots_ = std::max(1u, slots);
    counters_ = std::make_unique<Counter[]>(slots_);
    started_ = std::chrono::steady_clock::now();
    active_ = true;
    logger_.info("START {}{}", stage_, total_ ? fmt::format(" | {} items", total_) : "");
    if (interval_ > 0)
        heartbeat_ = std::jthread([this](std::stop_token token) {
            std::mutex mutex;
            std::condition_variable_any ready;
            std::unique_lock lock(mutex);
            while (!token.stop_requested()) {
                ready.wait_for(lock, token, std::chrono::duration<double>(interval_), [] { return false; });
                if (!token.stop_requested()) {
                    try {
                        report("PROGRESS");
                    } catch (...) {
                        return;
                    }
                }
            }
        });
}
void Progress::advance(unsigned slot, std::uint64_t count) {
    counters_[slot].value.fetch_add(count, std::memory_order_relaxed);
}
void Progress::report(const char *status) const {
    std::uint64_t count = 0;
    for (unsigned i = 0; i < slots_; ++i)
        count += counters_[i].value.load(std::memory_order_relaxed);
    const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started_).count();
    const auto rate = seconds > 0 ? count / seconds : 0;
    if (total_)
        logger_.info("{} {} | {}/{} ({:.1f}%) | {:.1f}/s | elapsed {} | ETA {}", status, stage_, count,
                     total_, 100. * count / total_, rate, duration(seconds),
                     count > 0 ? duration((total_ > count ? total_ - count : 0) / rate) : "--");
    else
        logger_.info("{} {} | {} items | elapsed {}", status, stage_, count, duration(seconds));
}
void Progress::finish() {
    if (!active_)
        return;
    stop();
    report("DONE");
    active_ = false;
}
} // namespace phsp
