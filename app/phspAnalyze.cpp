#include "cli.hpp"
#include "phsp/analysis.hpp"
#include <limits>

namespace {
void usage() {
    std::cout << "Usage: phspAnalyze INPUT.root [options]\n"
              << "  --output DIR       Report directory (default: INPUT_parent/INPUT_stem_analysis)\n"
              << "  --threads N        RDataFrame workers (default: 1)\n"
              << "  --max N            First N input entries, before particle selection\n"
              << "  --particle N       IAEA: 1 photon, 2 electron, 3 positron, 4 neutron, 5 proton\n"
              << "  --bins N           Histogram bins (default: 120; range: 8..1000)\n"
              << "  --energy-max MeV   Upper edge (default: data maximum)\n"
              << "  --xy-max cm        Symmetric XY range (default: data maximum)\n"
              << "  --project-z cm     Analyze crossings at this Z (default: 100 cm)\n"
              << "  --source-z-offset cm  Add to source Z to align coordinate origins (default: 0)\n"
              << "  --native-plane     Analyze the original scoring surface without projection\n"
              << "  --no-plots         Skip SVG/PDF; retain ROOT and reports\n"
              << "  --no-histories     Skip the sequential history pass\n";
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
        phsp::AnalysisOptions o;
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
            if (arg == "--output")
                o.output = value();
            else if (arg == "--threads")
                o.threads = cli::integer(value(), 1, 1024);
            else if (arg == "--max")
                o.max_records = cli::integer(value(), 0, std::numeric_limits<long long>::max());
            else if (arg == "--bins")
                o.bins = cli::integer(value(), 8, 1000);
            else if (arg == "--particle")
                o.particle = cli::integer(value(), 1, 5);
            else if (arg == "--energy-max")
                o.energy_max = cli::number(value());
            else if (arg == "--xy-max")
                o.xy_max = cli::number(value());
            else if (arg == "--project-z")
                o.project_z = cli::number(value());
            else if (arg == "--source-z-offset")
                o.source_z_offset_cm = cli::number(value());
            else if (arg == "--native-plane")
                o.project_z.reset();
            else if (arg == "--no-plots")
                o.plots = false;
            else if (arg == "--no-histories")
                o.histories = false;
            else
                throw std::invalid_argument("Unknown option: " + arg);
        }
        const auto result = phsp::analyze(o);
        std::cout << "Report: " << result << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "phspAnalyze: " << e.what() << '\n';
        return 1;
    }
}
