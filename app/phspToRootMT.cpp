#include "cli.hpp"
#include "phsp/conversion.hpp"
#include <limits>

namespace {
void usage() {
    std::cout << "Usage: phspToRootMT IAEA_BASE [options]\n"
              << "  --output-dir DIR   Output directory (default: results/INPUT_name)\n"
              << "  --threads N        Conversion workers (default: 4)\n"
              << "  --max N            Maximum written records; -1 means all (default)\n"
              << "  --stride N         Keep every Nth record (default: 1)\n"
              << "  --block N          Records per processing block (default: 200000)\n"
              << "  --queue N          Blocks per bounded queue (default: 8)\n"
              << "  --extras           Retain IAEA extra float/integer columns\n"
              << "  --allow-incomplete Convert whole records from an incomplete source\n";
    cli::loggingHelp();
}
} // namespace
int main(int argc, char **argv) {
    try {
        if (argc < 2) {
            usage();
            return 1;
        }
        if (std::string(argv[1]) == "--help") {
            usage();
            return 0;
        }
        phsp::ConversionOptions o;
        o.input = argv[1];
        for (int i = 2; i < argc; ++i) {
            const std::string arg = argv[i];
            auto value = [&]() -> std::string {
                if (++i == argc)
                    throw std::invalid_argument("Missing value for " + arg);
                return argv[i];
            };
            if (cli::logging(arg, value, o.logging))
                continue;
            if (arg == "--output-dir")
                o.output = value();
            else if (arg == "--threads")
                o.threads = cli::integer(value(), 1, 1024);
            else if (arg == "--max")
                o.max_records = cli::integer(value(), -1, std::numeric_limits<long long>::max());
            else if (arg == "--stride")
                o.stride = cli::integer(value(), 1, std::numeric_limits<long long>::max());
            else if (arg == "--block")
                o.block_size = cli::integer(value(), 1, std::numeric_limits<long long>::max());
            else if (arg == "--queue")
                o.queue_capacity = cli::integer(value(), 1, std::numeric_limits<long long>::max());
            else if (arg == "--extras")
                o.extras = true;
            else if (arg == "--allow-incomplete")
                o.allow_incomplete = true;
            else
                throw std::invalid_argument("Unknown option: " + arg);
        }
        const auto result = phsp::convert(o);
        std::cout << "ROOT: " << result << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "phspToRootMT: " << e.what() << '\n';
        return 1;
    }
}
