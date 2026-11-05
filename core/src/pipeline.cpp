#include "phsp/pipeline.hpp"
#include "phsp/analysis.hpp"
#include "phsp/comparison.hpp"
#include "phsp/conversion.hpp"
#include "phsp/processing.hpp"
#include <cmath>
#include <fstream>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <vector>

namespace phsp {
namespace {
using json = nlohmann::json;
namespace fs = std::filesystem;
long long integer(const json &j, const char *key, long long fallback, long long lo, long long hi) {
    if (!j.contains(key))
        return fallback;
    if (!j[key].is_number_integer() || j[key].get<long double>() < lo || j[key].get<long double>() > hi)
        throw std::invalid_argument(std::string("Invalid integer field: ") + key);
    return j[key].get<long long>();
}
std::optional<double> optionalNumber(const json &j, const char *key, bool positive = true) {
    if (!j.contains(key))
        return {};
    if (!j[key].is_number())
        throw std::invalid_argument(std::string("Expected number: ") + key);
    const auto value = j[key].get<double>();
    if (!std::isfinite(value) || (positive && value <= 0))
        throw std::invalid_argument(std::string("Invalid range: ") + key);
    return value;
}
void objectKeys(const json &j, std::initializer_list<const char *> allowed, const char *section) {
    if (!j.is_object())
        throw std::invalid_argument(std::string(section) + " must be an object");
    for (auto it = j.begin(); it != j.end(); ++it) {
        bool known = false;
        for (const auto *key : allowed)
            known |= it.key() == key;
        if (!known)
            throw std::invalid_argument(std::string("Unknown ") + section + " field: " + it.key());
    }
}
void validName(const std::string &name) {
    if (name.empty() || name == "." || name == ".." ||
        name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-") !=
            std::string::npos)
        throw std::invalid_argument(
            "Dataset/comparison names must use letters, digits, dot, underscore or hyphen");
}
fs::path resolve(const fs::path &base, const fs::path &path) {
    return fs::absolute(path.is_absolute() ? path : base / path).lexically_normal();
}
void save(const fs::path &path, const json &manifest) {
    const auto temporary = path.string() + ".partial";
    {
        std::ofstream out(temporary);
        if (!out || !(out << manifest.dump(2) << '\n'))
            throw std::runtime_error("Cannot write pipeline manifest");
    }
    fs::rename(temporary, path);
}
std::string escape(const std::string &text) {
    std::string out;
    for (char c : text) {
        if (c == '&')
            out += "&amp;";
        else if (c == '<')
            out += "&lt;";
        else if (c == '>')
            out += "&gt;";
        else if (c == '"')
            out += "&quot;";
        else
            out += c;
    }
    return out;
}
void index(const fs::path &output, const json &manifest) {
    std::string html =
        "<!doctype html><html lang=\"en\"><meta charset=\"utf-8\"><meta name=\"viewport\" "
        "content=\"width=device-width,initial-scale=1\"><title>PHSP pipeline</title><style>body{font:16px "
        "system-ui;max-width:1000px;margin:32px auto;padding:0 "
        "20px;background:#f5f7fa;color:#17243a}section{padding:20px;background:white;border-radius:12px;"
        "margin:20px "
        "0}table{width:100%;border-collapse:collapse}td,th{text-align:left;padding:10px;border-bottom:1px "
        "solid #ddd}a{color:#145bbc}</style><h1>PHSP pipeline</h1><p>Status: <strong>";
    html += escape(manifest.at("status").get<std::string>()) +
            "</strong> · <a href=\"pipeline_manifest.json\">Manifest JSON</a> · <a "
            "href=\"pipeline.log\">Log</a></p>";
    if (manifest.contains("error"))
        html += "<section>" + escape(manifest["error"].get<std::string>()) + "</section>";
    for (const std::string section : {"preprocessing", "datasets", "comparisons"}) {
        html += "<section><h2>" +
                std::string(section == "preprocessing" ? "IAEA preprocessing"
                            : section == "datasets"    ? "Analyses"
                                                       : "Comparisons") +
                "</h2><table><tr><th>Name</th><th>Status</th><th>Report</th></tr>";
        for (auto it = manifest[section].begin(); it != manifest[section].end(); ++it) {
            const auto status = it.value().value("status", std::string("pending"));
            html += "<tr><td>" + escape(it.key()) + "</td><td>" + escape(status) + "</td><td>";
            if (status == "complete") {
                const auto path = section == "preprocessing"
                                      ? "preprocessing/" + it.key() + "/" + it.key() + ".processing.json"
                                  : section == "datasets" ? "datasets/" + it.key() + "/analysis/report.html"
                                                          : "comparisons/" + it.key() + "/comparison.html";
                html += "<a href=\"" + path + "\">Open report</a>";
            }
            html += "</td></tr>";
        }
        html += "</table></section>";
    }
    html += "</html>";
    std::ofstream out(output / "index.html");
    if (!out || !(out << html))
        throw std::runtime_error("Cannot write pipeline index");
}
struct Dataset {
    std::string name;
    fs::path input;
    bool root = false;
    ConversionOptions conversion;
    AnalysisOptions analysis;
};
struct Preparation {
    std::string name, operation;
    CutOptions cut;
    MergeOptions merge;
};
struct Pair {
    std::string name, reference, candidate;
    bool plots = true;
};
} // namespace

fs::path runPipeline(const PipelineOptions &opt) {
    const auto configPath = fs::absolute(opt.config);
    std::ifstream in(configPath);
    if (!in)
        throw std::runtime_error("Cannot open pipeline config: " + configPath.string());
    json config;
    in >> config;
    objectKeys(
        config,
        {"output_dir", "threads", "conversion", "analysis", "preprocessing", "datasets", "comparisons"},
        "pipeline");
    const auto configDir = configPath.parent_path();
    const auto output = opt.output.empty()
                            ? resolve(configDir, config.value("output_dir", std::string("results")))
                            : fs::absolute(opt.output);
    if (fs::weakly_canonical(configPath) == fs::weakly_canonical(output / "pipeline_manifest.json"))
        throw std::invalid_argument("Pipeline manifest would overwrite its config");
    Logger log("pipeline", opt.logging, output / "pipeline.log");
    json manifest = {{"version", PHSP_VERSION},        {"config", configPath.string()},
                     {"output", output.string()},      {"status", "validating"},
                     {"datasets", json::object()},     {"comparisons", json::object()},
                     {"preprocessing", json::object()}};
    try {
        log.info("Config: {}", configPath.string());
        const auto threads = integer(config, "threads", 4, 1, 1024);
        const auto globalConversion = config.value("conversion", json::object());
        const auto globalAnalysis = config.value("analysis", json::object());
        if (!config.contains("datasets") || !config["datasets"].is_array() || config["datasets"].empty())
            throw std::invalid_argument("Pipeline needs a nonempty datasets array");
        std::vector<Dataset> datasets;
        std::map<std::string, size_t> lookup;
        LogOptions childLog = opt.logging;
        childLog.file.clear(); // A custom --log-file belongs to the pipeline, not every child run.
        std::map<std::string, fs::path> products;
        auto inputPath = [&](const std::string &text) {
            if (text.starts_with("@")) {
                const auto key = text.substr(1);
                if (!products.contains(key))
                    throw std::invalid_argument("Unknown or forward preprocessing reference: " + text);
                return products.at(key);
            }
            return resolve(configDir, text);
        };
        auto checkInput = [&](const fs::path &path) {
            for (const auto &product : products)
                if (product.second == path)
                    return;
            if (path.extension() == ".root") {
                if (!fs::is_regular_file(path))
                    throw std::invalid_argument("Missing ROOT input: " + path.string());
            } else {
                auto base = path;
                if (base.extension() == ".IAEAheader" || base.extension() == ".IAEAphsp")
                    base.replace_extension();
                if (!fs::is_regular_file(base.string() + ".IAEAheader") ||
                    !fs::is_regular_file(base.string() + ".IAEAphsp"))
                    throw std::invalid_argument("Missing IAEA header/data pair: " + base.string());
            }
        };
        std::vector<Preparation> preparations;
        const auto stages = config.value("preprocessing", json::array());
        if (!stages.is_array())
            throw std::invalid_argument("preprocessing must be an array");
        for (const auto &item : stages) {
            Preparation p;
            p.name = item.at("name").get<std::string>();
            validName(p.name);
            if (products.contains(p.name))
                throw std::invalid_argument("Duplicate preprocessing name: " + p.name);
            p.operation = item.at("operation").get<std::string>();
            const auto base = output / "preprocessing" / p.name / p.name;
            if (p.operation == "merge") {
                objectKeys(item,
                           {"name", "operation", "inputs", "drop_extras", "allow_incomplete",
                            "max_records_per_input"},
                           "merge stage");
                if (!item.contains("inputs") || !item["inputs"].is_array() || item["inputs"].empty())
                    throw std::invalid_argument("Merge stage needs input paths");
                for (const auto &path : item["inputs"]) {
                    auto in = inputPath(path.get<std::string>());
                    checkInput(in);
                    p.merge.inputs.push_back(in);
                }
                p.merge.output = base;
                p.merge.drop_extras = item.value("drop_extras", false);
                p.merge.allow_incomplete = item.value("allow_incomplete", false);
                p.merge.max_records_per_input = integer(item, "max_records_per_input", -1, -1, INT64_MAX);
                p.merge.logging = childLog;
            } else if (p.operation == "cut") {
                objectKeys(item,
                           {"name", "operation", "input", "drop_extras", "allow_incomplete", "max_records",
                            "project_z_cm", "source_z_offset_cm", "x_min_cm", "x_max_cm", "y_min_cm",
                            "y_max_cm", "write_projected", "positive_z_only"},
                           "cut stage");
                p.cut.input = inputPath(item.at("input").get<std::string>());
                checkInput(p.cut.input);
                p.cut.output = base;
                p.cut.drop_extras = item.value("drop_extras", false);
                p.cut.allow_incomplete = item.value("allow_incomplete", false);
                p.cut.max_records = integer(item, "max_records", -1, -1, INT64_MAX);
                p.cut.project_z_cm = optionalNumber(item, "project_z_cm", false).value_or(100.);
                p.cut.source_z_offset_cm = optionalNumber(item, "source_z_offset_cm", false).value_or(0.);
                p.cut.x_min_cm = optionalNumber(item, "x_min_cm", false).value_or(-7.);
                p.cut.x_max_cm = optionalNumber(item, "x_max_cm", false).value_or(7.);
                p.cut.y_min_cm = optionalNumber(item, "y_min_cm", false).value_or(-7.);
                p.cut.y_max_cm = optionalNumber(item, "y_max_cm", false).value_or(7.);
                p.cut.write_projected = item.value("write_projected", false);
                p.cut.positive_z_only = item.value("positive_z_only", true);
                if (p.cut.x_min_cm > p.cut.x_max_cm || p.cut.y_min_cm > p.cut.y_max_cm)
                    throw std::invalid_argument("Invalid preprocessing aperture bounds");
                p.cut.logging = childLog;
            } else
                throw std::invalid_argument("Preprocessing operation must be cut or merge");
            products[p.name] = base;
            preparations.push_back(std::move(p));
        }
        for (const auto &item : config["datasets"]) {
            objectKeys(item, {"name", "input", "conversion", "analysis"}, "dataset");
            Dataset d;
            d.name = item.at("name").get<std::string>();
            validName(d.name);
            if (lookup.contains(d.name))
                throw std::invalid_argument("Duplicate dataset: " + d.name);
            d.input = inputPath(item.at("input").get<std::string>());
            d.root = d.input.extension() == ".root";
            checkInput(d.input);
            auto c = globalConversion, a = globalAnalysis;
            if (item.contains("conversion"))
                c.merge_patch(item["conversion"]);
            if (item.contains("analysis"))
                a.merge_patch(item["analysis"]);
            objectKeys(c,
                       {"threads", "max_records", "stride", "block_size", "queue_capacity", "extras",
                        "allow_incomplete"},
                       "conversion");
            objectKeys(a,
                       {"threads", "max_records", "particle", "bins", "energy_max_MeV", "xy_max_cm",
                        "project_z_cm", "source_z_offset_cm", "native_plane", "plots", "histories"},
                       "analysis");
            d.conversion.input = d.input;
            d.conversion.output = output / "datasets" / d.name / "conversion";
            d.conversion.threads = integer(c, "threads", threads, 1, 1024);
            d.conversion.max_records = integer(c, "max_records", -1, -1, INT64_MAX);
            d.conversion.stride = integer(c, "stride", 1, 1, INT64_MAX);
            d.conversion.block_size = integer(c, "block_size", 200000, 1, INT64_MAX);
            d.conversion.queue_capacity = integer(c, "queue_capacity", 8, 1, INT64_MAX);
            d.conversion.extras = c.value("extras", false);
            d.conversion.allow_incomplete = c.value("allow_incomplete", false);
            d.conversion.logging = childLog;
            d.analysis.output = output / "datasets" / d.name / "analysis";
            d.analysis.threads = integer(a, "threads", threads, 1, 1024);
            d.analysis.max_records = integer(a, "max_records", -1, -1, INT64_MAX);
            d.analysis.bins = integer(a, "bins", 120, 8, 1000);
            d.analysis.particle = integer(a, "particle", 0, 0, 5);
            d.analysis.energy_max = optionalNumber(a, "energy_max_MeV");
            d.analysis.xy_max = optionalNumber(a, "xy_max_cm");
            if (a.contains("project_z_cm"))
                d.analysis.project_z = optionalNumber(a, "project_z_cm", false);
            d.analysis.source_z_offset_cm = optionalNumber(a, "source_z_offset_cm", false).value_or(0.0);
            if (a.value("native_plane", false))
                d.analysis.project_z.reset();
            d.analysis.plots = !opt.no_plots && a.value("plots", true);
            d.analysis.histories = a.value("histories", true);
            d.analysis.logging = childLog;
            lookup[d.name] = datasets.size();
            datasets.push_back(std::move(d));
        }
        std::vector<Pair> comparisons;
        std::set<std::string> pairNames;
        const auto recipes = config.value("comparisons", json::array());
        if (!recipes.is_array())
            throw std::invalid_argument("comparisons must be an array");
        for (const auto &item : recipes) {
            objectKeys(item, {"name", "reference", "candidate", "plots"}, "comparison");
            Pair p;
            p.reference = item.at("reference").get<std::string>();
            p.candidate = item.at("candidate").get<std::string>();
            if (!lookup.contains(p.reference) || !lookup.contains(p.candidate))
                throw std::invalid_argument("Comparison refers to an unknown dataset");
            p.name = item.value("name", p.candidate + "_vs_" + p.reference);
            validName(p.name);
            if (!pairNames.insert(p.name).second)
                throw std::invalid_argument("Duplicate comparison: " + p.name);
            const auto &a = datasets[lookup.at(p.reference)].analysis;
            const auto &b = datasets[lookup.at(p.candidate)].analysis;
            if (!a.energy_max || !a.xy_max || a.bins != b.bins || a.energy_max != b.energy_max ||
                a.xy_max != b.xy_max)
                throw std::invalid_argument(
                    "Comparisons require identical explicit bins, energy_max_MeV and xy_max_cm");
            if (a.particle != b.particle)
                throw std::invalid_argument("Compared datasets must select the same particle type");
            if (a.project_z != b.project_z)
                throw std::invalid_argument(
                    "Compared datasets must use the same projection plane or both select native_plane");
            p.plots = !opt.no_plots && item.value("plots", a.plots);
            comparisons.push_back(std::move(p));
        }
        manifest["requested_config"] = config;
        manifest["resolved_config"] = {
            {"output_dir", output.string()}, {"datasets", json::array()}, {"comparisons", json::array()}};
        for (const auto &d : datasets) {
            const auto &c = d.conversion;
            const auto &a = d.analysis;
            json analysis = {{"threads", a.threads},
                             {"max_records", a.max_records},
                             {"bins", a.bins},
                             {"particle", a.particle},
                             {"plots", a.plots},
                             {"histories", a.histories},
                             {"source_z_offset_cm", a.source_z_offset_cm},
                             {"native_plane", !a.project_z.has_value()},
                             {"output", a.output.string()}};
            if (a.energy_max)
                analysis["energy_max_MeV"] = *a.energy_max;
            if (a.xy_max)
                analysis["xy_max_cm"] = *a.xy_max;
            if (a.project_z)
                analysis["project_z_cm"] = *a.project_z;
            manifest["resolved_config"]["datasets"].push_back({{"name", d.name},
                                                               {"input", d.input.string()},
                                                               {"input_format", d.root ? "ROOT" : "IAEA"},
                                                               {"analysis", analysis},
                                                               {"conversion",
                                                                {{"applied", !d.root},
                                                                 {"threads", c.threads},
                                                                 {"max_records", c.max_records},
                                                                 {"stride", c.stride},
                                                                 {"block_size", c.block_size},
                                                                 {"queue_capacity", c.queue_capacity},
                                                                 {"extras", c.extras},
                                                                 {"allow_incomplete", c.allow_incomplete},
                                                                 {"output", c.output.string()}}}});
        }
        for (const auto &p : comparisons)
            manifest["resolved_config"]["comparisons"].push_back({{"name", p.name},
                                                                  {"reference", p.reference},
                                                                  {"candidate", p.candidate},
                                                                  {"plots", p.plots}});
        manifest["resolved_config"]["preprocessing"] = json::array();
        for (const auto &p : preparations) {
            json stage = {{"name", p.name}, {"operation", p.operation}};
            if (p.operation == "cut")
                stage["input"] = p.cut.input.string();
            else {
                stage["inputs"] = json::array();
                for (const auto &in : p.merge.inputs)
                    stage["inputs"].push_back(in.string());
            }
            stage["output_base"] = (p.operation == "cut" ? p.cut.output : p.merge.output).string();
            manifest["resolved_config"]["preprocessing"].push_back(stage);
        }
        manifest["status"] = "running";
        save(output / "pipeline_manifest.json", manifest);
        index(output, manifest);
        Progress progress(log, opt.logging.progress_seconds);
        progress.start("Pipeline preparation, datasets and comparisons",
                       preparations.size() + datasets.size() + comparisons.size());
        for (const auto &p : preparations) {
            log.info("Preprocessing {}: {}", p.name, p.operation);
            manifest["preprocessing"][p.name] = {{"status", "running"}, {"operation", p.operation}};
            save(output / "pipeline_manifest.json", manifest);
            const auto result = p.operation == "cut" ? cut(p.cut) : merge(p.merge);
            manifest["preprocessing"][p.name] = {
                {"status", "complete"}, {"operation", p.operation}, {"output_base", result.string()}};
            save(output / "pipeline_manifest.json", manifest);
            progress.advance();
            index(output, manifest);
        }
        for (auto &d : datasets) {
            log.info("Dataset {}: {}", d.name, d.input.string());
            auto &status = manifest["datasets"][d.name];
            status = {{"status", "running"}, {"input", d.input.string()}};
            save(output / "pipeline_manifest.json", manifest);
            d.analysis.input = d.root ? d.input : convert(d.conversion);
            const auto result = analyze(d.analysis);
            status = {{"status", "complete"},
                      {"input", d.input.string()},
                      {"particle_root", d.analysis.input.string()},
                      {"report", (result / "report.html").string()},
                      {"analysis_root", (result / "analysis.root").string()}};
            save(output / "pipeline_manifest.json", manifest);
            progress.advance();
            index(output, manifest);
        }
        for (const auto &p : comparisons) {
            log.info("Comparison {}: {} versus {}", p.name, p.candidate, p.reference);
            manifest["comparisons"][p.name] = {{"status", "running"}};
            save(output / "pipeline_manifest.json", manifest);
            ComparisonOptions c;
            c.reference = datasets[lookup.at(p.reference)].analysis.output / "analysis.root";
            c.candidate = datasets[lookup.at(p.candidate)].analysis.output / "analysis.root";
            c.output = output / "comparisons" / p.name;
            c.plots = p.plots;
            c.logging = childLog;
            const auto result = compare(c);
            manifest["comparisons"][p.name] = {{"status", "complete"},
                                               {"report", (result / "comparison.html").string()}};
            save(output / "pipeline_manifest.json", manifest);
            progress.advance();
            index(output, manifest);
        }
        progress.finish();
        manifest["status"] = "complete";
        save(output / "pipeline_manifest.json", manifest);
        index(output, manifest);
        log.info("Finished: {} datasets, {} comparisons | index: {}", datasets.size(), comparisons.size(),
                 (output / "index.html").string());
        return output;
    } catch (const std::exception &error) {
        log.error("Pipeline failed: {}", error.what());
        manifest["status"] = "failed";
        manifest["error"] = error.what();
        for (const auto *section : {"preprocessing", "datasets", "comparisons"})
            for (auto &item : manifest[section])
                if (item.value("status", std::string()) == "running")
                    item["status"] = "failed";
        save(output / "pipeline_manifest.json", manifest);
        index(output, manifest);
        throw;
    }
}
} // namespace phsp
