#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fmt/format.h>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace phsp {
enum class LogLevel { debug, info, warning, error };
LogLevel parseLogLevel(std::string_view value);

struct LogOptions {
    LogLevel level = LogLevel::info;
    std::filesystem::path file;    // Empty selects a log beside the output.
    double progress_seconds = 5.0; // Zero disables periodic updates, not stage boundaries.
};

// A single, synchronized sink per run. INFO/DEBUG filtering affects both the
// terminal and the file; ERROR is always preserved at the default INFO level.
class Logger {
  public:
    Logger(std::string module, const LogOptions &options, const std::filesystem::path &default_file);
    void write(LogLevel level, const std::string &message) const;
    template <class... A> void info(fmt::format_string<A...> f, A &&...a) const {
        write(LogLevel::info, fmt::format(f, std::forward<A>(a)...));
    }
    template <class... A> void debug(fmt::format_string<A...> f, A &&...a) const {
        write(LogLevel::debug, fmt::format(f, std::forward<A>(a)...));
    }
    template <class... A> void warning(fmt::format_string<A...> f, A &&...a) const {
        write(LogLevel::warning, fmt::format(f, std::forward<A>(a)...));
    }
    template <class... A> void error(fmt::format_string<A...> f, A &&...a) const {
        write(LogLevel::error, fmt::format(f, std::forward<A>(a)...));
    }
    const std::filesystem::path &file() const { return path_; }

  private:
    std::string module_;
    LogLevel level_;
    std::filesystem::path path_;
    std::chrono::steady_clock::time_point started_;
    mutable std::mutex mutex_;
    mutable std::ofstream file_;
};

// Counts are isolated per RDF slot to avoid contention between workers. A
// heartbeat reports real work every N seconds, including 0% during ROOT setup/JIT.
class Progress {
  public:
    Progress(const Logger &logger, double interval_seconds);
    ~Progress();
    Progress(const Progress &) = delete;
    Progress &operator=(const Progress &) = delete;
    void start(std::string stage, std::uint64_t total = 0, unsigned slots = 1);
    void advance(unsigned slot = 0, std::uint64_t count = 1);
    void finish();

  private:
    struct alignas(64) Counter {
        std::atomic<std::uint64_t> value{0};
    };
    const Logger &logger_;
    double interval_;
    std::string stage_;
    std::uint64_t total_ = 0;
    unsigned slots_ = 0;
    std::unique_ptr<Counter[]> counters_;
    std::chrono::steady_clock::time_point started_;
    std::jthread heartbeat_;
    bool active_ = false;
    void stop();
    void report(const char *status) const;
};
} // namespace phsp
