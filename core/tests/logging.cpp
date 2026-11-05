#include "phsp/logging.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

int main(int argc, char **argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("Expected log path");
        const std::filesystem::path path = argv[1];
        std::filesystem::remove(path);
        {
            phsp::LogOptions options;
            phsp::Logger logger("test", options, path);
            logger.debug("filtered debug message");
            phsp::Progress progress(logger, .02);
            progress.start("waiting for work", 1);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            progress.advance();
            progress.finish();
            progress.start("parallel slots", 100, 4);
            std::vector<std::jthread> workers;
            for (unsigned slot = 0; slot < 4; ++slot)
                workers.emplace_back([&progress, slot] {
                    for (int i = 0; i < 25; ++i)
                        progress.advance(slot);
                });
            for (auto &thread : workers)
                thread.join();
            progress.finish();
        }
        std::ifstream in(path);
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (text.find("PROGRESS waiting for work") == std::string::npos ||
            text.find("100/100 (100.0%)") == std::string::npos ||
            text.find("filtered debug message") != std::string::npos)
            throw std::runtime_error("Heartbeat, slot counts or log level filtering failed");
        bool rejected = false;
        try {
            phsp::parseLogLevel("invalid");
        } catch (const std::invalid_argument &) {
            rejected = true;
        }
        if (!rejected)
            throw std::runtime_error("Invalid log level accepted");
        std::cout << "Logging and progress checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
