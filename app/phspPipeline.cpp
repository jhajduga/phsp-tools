#include "cli.hpp"
#include "phsp/pipeline.hpp"

int main(int argc, char **argv) {
    try {
        if (argc < 2 || std::string(argv[1]) == "--help") {
            std::cout << "Usage: phspPipeline CONFIG.json [--output DIR] [--no-plots] [logging options]\n"
                      << "Convert IAEA inputs, analyze every dataset, then compare selected pairs.\n"
                      << "Paths in JSON are relative to the configuration file.\n";
            cli::loggingHelp();
            return argc < 2 ? 1 : 0;
        }
        phsp::PipelineOptions o;
        o.config = argv[1];
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
            else if (arg == "--no-plots")
                o.no_plots = true;
            else
                throw std::invalid_argument("Unknown option: " + arg);
        }
        const auto result = phsp::runPipeline(o);
        std::cout << "Pipeline: " << result << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "phspPipeline: " << e.what() << '\n';
        return 1;
    }
}
