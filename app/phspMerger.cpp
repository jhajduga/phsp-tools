#include "cli.hpp"
#include "phsp/processing.hpp"
#include <limits>

int main(int argc, char **argv) {
    try {
        if (argc < 3 || std::string(argv[1]) == "--help") {
            std::cout
                << "Usage: phspMerger INPUT... --output OUTPUT_BASE [options]\n"
                << "Legacy syntax: phspMerger INPUT1 INPUT2 OUTPUT_BASE\n"
                << "  --drop-extras      Retain history increments only; allow differing opaque extras\n"
                << "  --allow-incomplete Read complete records from partial sources\n"
                << "  --max-per-input N  Limit each source to an input prefix\n";
            cli::loggingHelp();
            return argc >= 2 && std::string(argv[1]) == "--help" ? 0 : 1;
        }
        phsp::MergeOptions o;
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            auto value = [&]() -> std::string {
                if (++i == argc)
                    throw std::invalid_argument("Missing value for " + arg);
                return argv[i];
            };
            if (cli::logging(arg, value, o.logging))
                continue;
            if (arg == "--output")
                o.output = value();
            else if (arg == "--max-per-input")
                o.max_records_per_input = cli::integer(value(), 0, std::numeric_limits<long long>::max());
            else if (arg == "--drop-extras")
                o.drop_extras = true;
            else if (arg == "--allow-incomplete")
                o.allow_incomplete = true;
            else if (arg.starts_with("--"))
                throw std::invalid_argument("Unknown option: " + arg);
            else
                o.inputs.emplace_back(arg);
        }
        if (o.output.empty() && o.inputs.size() >= 2) {
            o.output = o.inputs.back();
            o.inputs.pop_back();
        }
        if (o.output.empty())
            throw std::invalid_argument("Output basename is required");
        const auto result = phsp::merge(o);
        std::cout << "IAEA: " << result << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "phspMerger: " << e.what() << '\n';
        return 1;
    }
}
