// Integrates the selection/concatenation workflows from jhajduga/phspCutter and
// jhajduga/phspMerger. See docs/legacy_tools.md and the retained cutter MIT notice.
#include "phsp/processing.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <set>
#include <sstream>
#include <stdexcept>

#include "iaea_phsp.h"
#include "iaea_record.h"
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

namespace phsp {
namespace {
namespace fs = std::filesystem;
using json = nlohmann::json;
fs::path basePath(fs::path path) {
    if (path.empty())
        throw std::invalid_argument("IAEA basename is required");
    path = fs::absolute(path).lexically_normal();
    if (path.extension() == ".IAEAheader" || path.extension() == ".IAEAphsp")
        path.replace_extension();
    return path;
}
std::string readText(const fs::path &path) {
    std::ifstream in(path);
    if (!in)
        throw std::runtime_error("Cannot read " + path.string());
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
std::vector<long long> numbers(const std::string &text, const std::string &name) {
    std::istringstream input(text);
    std::string line;
    bool active = false;
    std::vector<long long> out;
    while (std::getline(input, line)) {
        std::istringstream row(line);
        std::string token;
        if (!(row >> token))
            continue;
        if (token == "$" + name + ":") {
            active = true;
            continue;
        }
        if (!active)
            continue;
        if (token.front() == '$')
            break;
        std::istringstream numeric(line);
        long long value;
        if (numeric >> value)
            out.push_back(value);
    }
    return out;
}
long long scalar(const std::string &text, const std::string &name) {
    const auto v = numbers(text, name);
    if (v.empty())
        throw std::runtime_error("Missing IAEA field " + name);
    return v.front();
}
struct Handle {
    IAEA_I32 id = 0;
    bool active = false;
    void open(const fs::path &base, int access) {
        auto text = base.string();
        IAEA_I32 result = 0, mode = access;
        iaea_new_source(&id, text.data(), &mode, &result, static_cast<int>(text.size()));
        if (result < 0)
            throw std::runtime_error(fmt::format("Cannot open IAEA source {} (code {})", text, result));
        active = true;
    }
    void close() {
        if (active) {
            IAEA_I32 result = 0;
            iaea_destroy_source(&id, &result);
            active = false;
        }
    }
    ~Handle() { close(); }
    Handle() = default;
    Handle(const Handle &) = delete;
};
struct Record {
    IAEA_I32 increment = 0, type = 0;
    IAEA_Float e = 0, weight = 0, x = 0, y = 0, z = 0, u = 0, v = 0, w = 0;
    std::array<IAEA_Float, NUM_EXTRA_FLOAT> floats{};
    std::array<IAEA_I32, NUM_EXTRA_LONG> integers{};
};
struct Info {
    fs::path base;
    std::string header;
    long long available = 0, original = 0, declared = 0, record_length = 0, tail = 0;
    std::vector<int> float_types, integer_types, opaque_indices;
    bool complete = false;
    std::vector<std::pair<long long, long long>> partial_ranges;
    bool invalidHistory(long long i) const {
        for (const auto &range : partial_ranges)
            if (i >= range.first && i <= range.second)
                return true;
        return false;
    }
    json asJson() const {
        return {{"input", base.string()},       {"available_records", available},
                {"declared_records", declared}, {"original_histories", original},
                {"trailing_bytes", tail},       {"source_complete", complete}};
    }
};
struct Reader {
    Info info;
    Handle source;
    explicit Reader(const fs::path &path, bool allow) {
        info.base = basePath(path);
        info.header = readText(info.base.string() + ".IAEAheader");
        const auto contents = numbers(info.header, "RECORD_CONTENTS");
        if (contents.size() < 9 || contents[7] < 0 || contents[7] > NUM_EXTRA_FLOAT || contents[8] < 0 ||
            contents[8] > NUM_EXTRA_LONG)
            throw std::runtime_error("Invalid IAEA record/extras layout");
        long long length = 5;
        for (size_t i = 0; i < 7; ++i) {
            if (contents[i] != 0 && contents[i] != 1)
                throw std::runtime_error("Invalid IAEA stored-variable flag");
            if (i != 5)
                length += 4 * contents[i];
        }
        length += 4 * (contents[7] + contents[8]);
        info.record_length = scalar(info.header, "RECORD_LENGTH");
        if (length != info.record_length)
            throw std::runtime_error("IAEA RECORD_LENGTH disagrees with its layout");
        const auto bytes = static_cast<long long>(fs::file_size(info.base.string() + ".IAEAphsp"));
        info.available = bytes / length;
        info.tail = bytes % length;
        info.declared = scalar(info.header, "PARTICLES");
        info.original = scalar(info.header, "ORIG_HISTORIES");
        if (info.declared < 0 || info.original < 0)
            throw std::runtime_error("Negative IAEA record/history count");
        const auto byteOrder = scalar(info.header, "BYTE_ORDER");
        const auto nativeOrder = std::endian::native == std::endian::little ? 1234 : 4321;
        if (byteOrder != nativeOrder)
            throw std::runtime_error("IAEA byte order is not native; incomplete mode cannot override it");
        const bool integrity =
            bytes == scalar(info.header, "CHECKSUM") && info.available == info.declared && info.tail == 0;
        if (!integrity && !allow)
            throw std::runtime_error(
                "IAEA size/count mismatch; use --allow-incomplete to process complete available records");
        info.complete = integrity && info.header.find("PHSP_TOOLS_SOURCE_COMPLETE=0") == std::string::npos;
        const auto provenance = fs::path(info.base.string() + ".processing.json");
        if (fs::exists(provenance)) {
            auto j = json::parse(readText(provenance));
            info.complete = info.complete && j.value("source_complete", false);
            for (const auto &range : j.value("incomplete_history_ranges", json::array())) {
                const auto first = range.at(0).get<long long>(), last = range.at(1).get<long long>();
                if (first < 0 || last < first || last >= info.available)
                    throw std::runtime_error("Invalid processing history range");
                info.partial_ranges.emplace_back(first, last);
            }
        } else if (info.header.find("PHSP_TOOLS_PROCESSING_METADATA_REQUIRED=1") != std::string::npos)
            throw std::runtime_error(
                "Missing .processing.json required to preserve incomplete-history boundaries");
        source.open(info.base, 1);
        std::array<IAEA_I32, NUM_EXTRA_LONG> longs{};
        std::array<IAEA_I32, NUM_EXTRA_FLOAT> floats{};
        IAEA_I32 result = 0;
        iaea_get_type_extra_variables(&source.id, &result, longs.data(), floats.data());
        if (result < 0)
            throw std::runtime_error("Cannot read IAEA extra-variable types");
        for (int i = 0; i < contents[7]; ++i)
            info.float_types.push_back(floats[i]);
        unsigned markers = 0;
        for (int i = 0; i < contents[8]; ++i) {
            if (longs[i] == 1)
                ++markers;
            else {
                info.integer_types.push_back(longs[i]);
                info.opaque_indices.push_back(i);
            }
        }
        if (markers > 1)
            throw std::runtime_error("Multiple history-increment fields are ambiguous");
    }
    Record read() {
        Record r;
        iaea_get_particle(&source.id, &r.increment, &r.type, &r.e, &r.weight, &r.x, &r.y, &r.z, &r.u, &r.v,
                          &r.w, r.floats.data(), r.integers.data());
        if (r.increment < 0)
            throw std::runtime_error("Unexpected IAEA record read failure");
        return r;
    }
};
struct Writer {
    fs::path output, temporary;
    Handle source;
    Info schema;
    bool drop = false;
    long long written = 0;
    std::vector<std::pair<long long, long long>> partial_ranges;
    void markPartial(long long first, long long last) {
        if (last >= first)
            partial_ranges.emplace_back(first, last);
    }
    Writer(const fs::path &path, Reader &first, bool discard)
        : output(basePath(path)), temporary(output.string() + ".partial"), schema(first.info), drop(discard) {
        fs::create_directories(output.parent_path());
        const auto longCount = drop ? 1 : 1 + schema.integer_types.size();
        if (longCount > NUM_EXTRA_LONG)
            throw std::runtime_error("No space to preserve a history-increment field; select --drop-extras");
        source.open(temporary, 2);
        IAEA_I32 result = 0;
        iaea_copy_header(&first.source.id, &source.id, &result);
        if (result < 0)
            throw std::runtime_error("Cannot copy IAEA descriptive header");
        IAEA_I32 nf = drop ? 0 : schema.float_types.size(), ni = longCount;
        iaea_set_extra_numbers(&source.id, &nf, &ni);
        for (IAEA_I32 i = 0; i < nf; ++i) {
            IAEA_I32 type = schema.float_types[i];
            iaea_set_type_extrafloat_variable(&source.id, &i, &type);
            if (type < 0)
                throw std::runtime_error("Unsupported extra float type");
        }
        for (IAEA_I32 i = 0; i < ni; ++i) {
            IAEA_I32 type = i == 0 ? 1 : schema.integer_types[i - 1];
            iaea_set_type_extralong_variable(&source.id, &i, &type);
            if (type < 0)
                throw std::runtime_error("Unsupported extra integer type");
        }
    }
    void write(Record r, const Info &input, long long increment) {
        if (increment < 0 || increment > INT32_MAX)
            throw std::runtime_error("History increment exceeds IAEA int32 capacity");
        std::array<IAEA_I32, NUM_EXTRA_LONG> integers{};
        integers[0] = increment;
        if (!drop)
            for (size_t i = 0; i < input.opaque_indices.size(); ++i)
                integers[i + 1] = r.integers[input.opaque_indices[i]];
        IAEA_I32 stat = increment;
        iaea_write_particle(&source.id, &stat, &r.type, &r.e, &r.weight, &r.x, &r.y, &r.z, &r.u, &r.v, &r.w,
                            r.floats.data(), integers.data());
        if (stat < 0)
            throw std::runtime_error("IAEA output write failed");
        ++written;
    }
    fs::path finish(long long original, bool complete, json provenance) {
        IAEA_I64 n = original;
        IAEA_I32 result = 0;
        iaea_set_total_original_particles(&source.id, &n);
        iaea_update_header(&source.id, &result);
        if (result < 0)
            throw std::runtime_error("Cannot finalize IAEA header");
        source.close();
        auto header = readText(temporary.string() + ".IAEAheader");
        std::sort(partial_ranges.begin(), partial_ranges.end());
        std::vector<std::pair<long long, long long>> ranges;
        for (const auto &r : partial_ranges) {
            if (!ranges.empty() && r.first <= ranges.back().second + 1)
                ranges.back().second = std::max(ranges.back().second, r.second);
            else
                ranges.push_back(r);
        }
        const auto marker = std::string("PHSP_TOOLS_SOURCE_COMPLETE=") + (complete ? "1" : "0") +
                            (ranges.empty() ? "" : "\nPHSP_TOOLS_PROCESSING_METADATA_REQUIRED=1");
        const auto pos = header.find("$ADDITIONAL_NOTES:");
        if (pos == std::string::npos)
            header += "\n$ADDITIONAL_NOTES:\n" + marker + "\n";
        else
            header.insert(header.find('\n', pos) + 1, marker + "\n");
        {
            std::ofstream out(temporary.string() + ".IAEAheader");
            if (!(out << header))
                throw std::runtime_error("Cannot write IAEA provenance marker");
        }
        provenance["source_complete"] = complete;
        provenance["written_records"] = written;
        provenance["incomplete_history_ranges"] = ranges;
        provenance["original_histories"] = original;
        provenance["output_base"] = output.string();
        {
            std::ofstream out(temporary.string() + ".processing.json");
            if (!(out << provenance.dump(2) << '\n'))
                throw std::runtime_error("Cannot write processing provenance");
        }
        fs::rename(temporary.string() + ".IAEAphsp", output.string() + ".IAEAphsp");
        fs::rename(temporary.string() + ".processing.json", output.string() + ".processing.json");
        fs::rename(temporary.string() + ".IAEAheader", output.string() + ".IAEAheader");
        return output;
    }
};
void distinctOutput(const fs::path &output, const std::vector<fs::path> &inputs) {
    const auto base = basePath(output), temporary = fs::path(base.string() + ".partial");
    for (const auto &input : inputs) {
        const auto source = basePath(input);
        for (const auto &target : {base, temporary}) {
            if (fs::weakly_canonical(target) == fs::weakly_canonical(source))
                throw std::invalid_argument("Output must not overwrite an input IAEA pair");
            for (const auto *ext : {".IAEAheader", ".IAEAphsp"})
                if (fs::exists(target.string() + ext) &&
                    fs::equivalent(target.string() + ext, source.string() + ext))
                    throw std::invalid_argument("Output aliases an input file");
        }
    }
}
} // namespace

fs::path cut(const CutOptions &o) {
    for (double v : {o.project_z_cm, o.source_z_offset_cm, o.x_min_cm, o.x_max_cm, o.y_min_cm, o.y_max_cm})
        if (!std::isfinite(v))
            throw std::invalid_argument("Cut geometry must be finite");
    if (o.x_min_cm > o.x_max_cm || o.y_min_cm > o.y_max_cm || o.max_records < -1)
        throw std::invalid_argument("Invalid cut bounds or input limit");
    distinctOutput(o.output, {o.input});
    const auto output = basePath(o.output);
    Logger log("cut", o.logging, output.string() + ".log");
    try {
        Reader input(o.input, o.allow_incomplete);
        Writer writer(output, input, o.drop_extras);
        const auto limit =
            o.max_records < 0 ? input.info.available : std::min(input.info.available, o.max_records);
        Progress progress(log, o.logging.progress_seconds);
        progress.start("Select IAEA aperture crossings", limit);
        log.info("Aperture at Z={} cm: X=[{},{}], Y=[{},{}]; output positions={}", o.project_z_cm, o.x_min_cm,
                 o.x_max_cm, o.y_min_cm, o.y_max_cm, o.write_projected ? "projected" : "original");
        long long pending = 0, lastGroup = 0;
        for (long long i = 0; i < limit; ++i) {
            auto r = input.read();
            pending += r.increment;
            progress.advance();
            if (r.increment > 0)
                lastGroup = writer.written;
            if (!std::isfinite(r.x) || !std::isfinite(r.y) || !std::isfinite(r.z) || !std::isfinite(r.u) ||
                !std::isfinite(r.v) || !std::isfinite(r.w))
                continue;
            if (std::abs(r.w) <= 1e-8 || (o.positive_z_only && r.w <= 0))
                continue;
            const double t = (o.project_z_cm - (double(r.z) + o.source_z_offset_cm)) / r.w;
            if (t < 0)
                continue;
            const double x = r.x + t * r.u, y = r.y + t * r.v;
            if (!std::isfinite(x) || !std::isfinite(y) || x < o.x_min_cm || x > o.x_max_cm ||
                y < o.y_min_cm || y > o.y_max_cm)
                continue;
            if (o.write_projected) {
                r.x = x;
                r.y = y;
                r.z = o.project_z_cm;
            }
            if (input.info.invalidHistory(i))
                writer.markPartial(writer.written, writer.written);
            writer.write(r, input.info, pending);
            pending = 0;
        }
        if (!input.info.complete || limit < input.info.available)
            writer.markPartial(lastGroup, writer.written - 1);
        progress.finish();
        auto result = writer.finish(
            input.info.original, input.info.complete && limit == input.info.available,
            {{"operation", "cut"},
             {"input", input.info.asJson()},
             {"read_records", limit},
             {"project_z_cm", o.project_z_cm},
             {"source_z_offset_cm", o.source_z_offset_cm},
             {"x_range_cm", {o.x_min_cm, o.x_max_cm}},
             {"y_range_cm", {o.y_min_cm, o.y_max_cm}},
             {"write_projected", o.write_projected},
             {"positive_z_only", o.positive_z_only},
             {"allow_incomplete", o.allow_incomplete},
             {"max_records", o.max_records},
             {"extras_dropped", o.drop_extras},
             {"history_policy",
              "Preserve original-history normalization and carry increments over rejected records."}});
        log.info("Finished: {} accepted / {} read | {}", writer.written, limit, result.string());
        return result;
    } catch (const std::exception &e) {
        log.error("Cut failed: {}", e.what());
        throw;
    }
}
fs::path merge(const MergeOptions &o) {
    if (o.inputs.empty() || o.max_records_per_input < -1)
        throw std::invalid_argument("Merge requires inputs and a valid record limit");
    distinctOutput(o.output, o.inputs);
    const auto output = basePath(o.output);
    Logger log("merge", o.logging, output.string() + ".log");
    try {
        std::vector<Info> inputs;
        std::set<fs::path> unique;
        long long planned = 0, original = 0;
        bool complete = true;
        std::vector<int> floats, integers;
        for (const auto &path : o.inputs) {
            if (!unique.insert(fs::weakly_canonical(basePath(path))).second)
                throw std::invalid_argument("Duplicate merge input");
            Reader r(path, o.allow_incomplete);
            if (!o.drop_extras && !inputs.empty() &&
                (r.info.float_types != floats || r.info.integer_types != integers))
                throw std::invalid_argument(
                    "Incompatible extra-variable schemas; use --drop-extras to keep history increments only");
            if (inputs.empty()) {
                floats = r.info.float_types;
                integers = r.info.integer_types;
            }
            const auto limit = o.max_records_per_input < 0
                                   ? r.info.available
                                   : std::min(r.info.available, o.max_records_per_input);
            if (limit > 0 && planned > 0 && r.read().increment == 0)
                throw std::invalid_argument("A later merge input starts without a history boundary; refusing "
                                            "to join independent histories");
            if (original > INT64_MAX - r.info.original || planned > INT64_MAX - limit)
                throw std::overflow_error("Merged counts exceed int64");
            original += r.info.original;
            planned += limit;
            complete &= r.info.complete && limit == r.info.available;
            inputs.push_back(r.info);
        }
        Reader first(o.inputs.front(), o.allow_incomplete);
        Writer writer(output, first, o.drop_extras);
        first.source.close();
        Progress progress(log, o.logging.progress_seconds);
        progress.start("Merge IAEA particle records", planned);
        json provenance = {{"operation", "merge"},
                           {"inputs", json::array()},
                           {"extras_dropped", o.drop_extras},
                           {"allow_incomplete", o.allow_incomplete},
                           {"max_records_per_input", o.max_records_per_input},
                           {"normalization_policy", "Sum original histories across independent input runs; "
                                                    "retain each file's scored-history boundaries."}};
        for (const auto &info : inputs) {
            log.info("Merge input: {}", info.base.string());
            Reader reader(info.base, o.allow_incomplete);
            const auto limit = o.max_records_per_input < 0
                                   ? info.available
                                   : std::min(info.available, o.max_records_per_input);
            long long lastGroup = writer.written;
            for (long long i = 0; i < limit; ++i) {
                auto r = reader.read();
                if (r.increment > 0)
                    lastGroup = writer.written;
                if (info.invalidHistory(i))
                    writer.markPartial(writer.written, writer.written);
                writer.write(r, info, r.increment);
                progress.advance();
            }
            if (!info.complete || limit < info.available)
                writer.markPartial(lastGroup, writer.written - 1);
            provenance["inputs"].push_back(info.asJson());
        }
        progress.finish();
        const auto result = writer.finish(original, complete, provenance);
        log.info("Finished: {} records from {} sources | {}", writer.written, inputs.size(), result.string());
        return result;
    } catch (const std::exception &e) {
        log.error("Merge failed: {}", e.what());
        throw;
    }
}
} // namespace phsp
