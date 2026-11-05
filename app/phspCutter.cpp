#include "cli.hpp"
#include "phsp/processing.hpp"
#include <limits>

int main(int argc, char **argv) {
    try {
        if (argc < 3 || std::string(argv[1]) == "--help") {
            std::cout
                << "Usage: phspCutter INPUT_BASE OUTPUT_BASE [options]\n"
                << "  --project-z cm     Aperture plane (default: 100)\n"
                << "  --x-min cm --x-max cm --y-min cm --y-max cm  Bounds (default: -7..7)\n"
                << "  --source-z-offset cm  Align source Z origin (default: 0)\n"
                << "  --write-projected  Write projected coordinates; default retains original positions\n"
                << "  --both-directions  Accept any forward-time ray reaching the aperture\n"
                << "  --max N            Input-record prefix, not accepted count\n"
                << "  --drop-extras      Retain only the history-increment extra\n"
                << "  --allow-incomplete Read complete records from a partial input\n";
            cli::loggingHelp();
            return argc >= 2 && std::string(argv[1]) == "--help" ? 0 : 1;
        }
        phsp::CutOptions o;
        o.input = argv[1];
        o.output = argv[2];
        for (int i = 3; i < argc; ++i) {
            std::string arg = argv[i];
            auto value = [&]() -> std::string {
                if (++i == argc)
                    throw std::invalid_argument("Missing value for " + arg);
                return argv[i];
            };
            if (cli::logging(arg, value, o.logging))
                continue;
            if (arg == "--project-z")
                o.project_z_cm = cli::number(value());
            else if (arg == "--x-min")
                o.x_min_cm = cli::number(value());
            else if (arg == "--x-max")
                o.x_max_cm = cli::number(value());
            else if (arg == "--y-min")
                o.y_min_cm = cli::number(value());
            else if (arg == "--y-max")
                o.y_max_cm = cli::number(value());
            else if (arg == "--source-z-offset")
                o.source_z_offset_cm = cli::number(value());
            else if (arg == "--max")
                o.max_records = cli::integer(value(), 0, std::numeric_limits<long long>::max());
            else if (arg == "--write-projected")
                o.write_projected = true;
            else if (arg == "--both-directions")
                o.positive_z_only = false;
            else if (arg == "--drop-extras")
                o.drop_extras = true;
            else if (arg == "--allow-incomplete")
                o.allow_incomplete = true;
            else
                throw std::invalid_argument("Unknown option: " + arg);
        }
        const auto result = phsp::cut(o);
        std::cout << "IAEA: " << result << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "phspCutter: " << e.what() << '\n';
        return 1;
    }
}
