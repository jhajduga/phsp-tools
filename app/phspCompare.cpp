#include "cli.hpp"
#include "phsp/comparison.hpp"

namespace {
void usage() {
    std::cout << "Usage: phspCompare REFERENCE/analysis.root CANDIDATE/analysis.root --output DIR [options]\n"
              << "  --no-plots         Skip SVG rendering\n"
              << "Both analyses must use identical --bins, --energy-max and --xy-max.\n";
    cli::loggingHelp();
}
} // namespace
int main(int argc, char **argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--help") {
            usage();
            return 0;
        }
        if (argc < 5) {
            usage();
            return 1;
        }
        phsp::ComparisonOptions o;
        o.reference = argv[1];
        o.candidate = argv[2];
        for (int i = 3; i < argc; ++i) {
            const std::string arg = argv[i];
            auto value = [&]() -> std::string {
                if (++i == argc)
                    throw std::invalid_argument("Missing value for " + arg);
                return argv[i];
            };
            if (cli::logging(arg, value, o.logging))
                continue;
            if (arg == "--output")
                o.output = value();
            else if (arg == "--no-plots")
                o.plots = false;
            else
                throw std::invalid_argument("Unknown option: " + arg);
        }
        const auto result = phsp::compare(o);
        std::cout << "Comparison: " << result << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "phspCompare: " << e.what() << '\n';
        return 1;
    }
}
