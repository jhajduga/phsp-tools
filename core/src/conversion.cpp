#include "phsp/conversion.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "iaea_phsp.h"
#include "iaea_record.h" // pulls utilities.h (with min/max macros)

// Kill ancient C macros before ROOT/STL-heavy includes
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

// --- ROOT
#include "TFile.h"
#include "TObjString.h"
#include "TROOT.h"
#include "TTree.h"
#include "TTreeCache.h"

namespace {

static std::string now_iso8601_utc() {
    using namespace std::chrono;
    auto t = system_clock::to_time_t(system_clock::now());
    std::tm tm{};
    gmtime_r(&t, &tm);

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

static std::string read_text_file(const std::filesystem::path &p) {
    std::ifstream in(p, std::ios::in | std::ios::binary);
    if (!in)
        return {};
    std::string data;
    in.seekg(0, std::ios::end);
    data.resize((size_t)in.tellg());
    in.seekg(0, std::ios::beg);
    in.read(data.data(), (std::streamsize)data.size());
    return data;
}

static std::vector<long long> header_numbers(const std::string &text, const std::string &block) {
    std::istringstream input(text);
    std::string line;
    bool active = false;
    std::vector<long long> values;
    while (std::getline(input, line)) {
        std::istringstream row(line);
        std::string token;
        if (!(row >> token))
            continue;
        if (token == "$" + block + ":") {
            active = true;
            continue;
        }
        if (!active)
            continue;
        if (token.front() == '$')
            break;
        long long value = 0;
        std::istringstream number(line);
        if (number >> value)
            values.push_back(value);
    }
    return values;
}

struct Metadata {
    std::string created_at_utc;
    std::string working_directory;
    std::string input_base;
    std::string phsp_name;

    std::string output_dir;
    std::string output_root;
    std::string log_dir;
    std::string log_file;

    int threads = 0;
    size_t block_size = 0;
    size_t queue_cap = 0;
    long long stride = 1;
    long long maxN = -1;
    bool extras = false;
    bool allow_incomplete = false;

    int nExtraF = 0;
    int nExtraI = 0;

    int header_nExtraF = 0;
    int header_nExtraI = 0;

    // PHSP header-ish metadata (what the public IAEA API exposes)
    long long max_particles = -1;
    long long max_histories = 0;
    double max_energy = 0.0;
    long long total_original_particles = 0;
    long long used_original_particles = 0;
    std::vector<int> extra_float_types;
    std::vector<int> extra_long_types;
    std::vector<double> constant_variables;
    std::string iaea_header_path;
    std::string iaea_header_text;
    std::string iaea_phsp_path;
    long long iaea_header_size_bytes = 0;
    long long iaea_phsp_size_bytes = 0;

    long long seen = 0;
    long long used = 0;
    long long written = 0;
    long long history_markers = 0;
    bool source_complete = false;
    int file_size_check_result = 0;
    long long header_checksum_bytes = 0;
    long long record_length_bytes = 0;
    long long available_complete_records = 0;
    long long trailing_bytes = 0;
    bool header_consistent = false;
    bool processing_source_complete = true;
    nlohmann::json processing_provenance = nlohmann::json::object();
};

static nlohmann::json metadata_json(const Metadata &m) {
    nlohmann::json j;
    j["created_at_utc"] = m.created_at_utc;
    j["working_directory"] = m.working_directory;
    j["input_base"] = m.input_base;
    j["phsp_name"] = m.phsp_name;

    j["output"] = {
        {"dir", m.output_dir}, {"root", m.output_root}, {"log_dir", m.log_dir}, {"log_file", m.log_file}};
    j["conversion"] = {{"threads", m.threads},
                       {"block_size", m.block_size},
                       {"queue_cap", m.queue_cap},
                       {"stride", m.stride},
                       {"max", m.maxN},
                       {"extras", m.extras}};
    j["conversion"]["allow_incomplete"] = m.allow_incomplete;
    j["input_integrity"] = {{"file_size_check_result", m.file_size_check_result},
                            {"header_checksum_bytes", m.header_checksum_bytes},
                            {"actual_size_bytes", m.iaea_phsp_size_bytes},
                            {"record_length_bytes", m.record_length_bytes},
                            {"header_particle_records", m.max_particles},
                            {"available_complete_records", m.available_complete_records},
                            {"trailing_bytes", m.trailing_bytes},
                            {"header_consistent", m.header_consistent}};
    j["counts"] = {{"seen_records", m.seen}, {"used_records", m.used}, {"written_records", m.written}};
    j["schema_version"] = 2;
    j["processing_provenance"] = m.processing_provenance;
    j["processing_source_complete"] = m.processing_source_complete;
    j["history"] = {{"markers_seen", m.history_markers},
                    {"source_complete", m.source_complete},
                    {"particle_order_preserved", true},
                    {"exact_particle_membership", m.stride == 1},
                    {"id_semantics", "Cumulative IAEA history increments; zero is an unmarked leading group. "
                                     "IDs may skip empty histories."},
                    {"length_semantics",
                     "Number of scored particle records, not trajectory length or all particles produced."}};
    j["phsp"] = {{"max_particles", m.max_particles},
                 {"max_histories", m.max_histories},
                 {"max_energy", m.max_energy},
                 {"total_original_particles", m.total_original_particles},
                 {"used_original_particles", m.used_original_particles},
                 {"n_extra_float_in_root", m.nExtraF},
                 {"n_extra_int_in_root", m.nExtraI},
                 {"n_extra_float_in_header", m.header_nExtraF},
                 {"n_extra_int_in_header", m.header_nExtraI},
                 {"extra_float_types", m.extra_float_types},
                 {"extra_long_types", m.extra_long_types},
                 {"constant_variables", m.constant_variables},
                 {"iaea_phsp_path", m.iaea_phsp_path},
                 {"iaea_phsp_size_bytes", m.iaea_phsp_size_bytes},
                 {"iaea_header_path", m.iaea_header_path},
                 {"iaea_header_size_bytes", m.iaea_header_size_bytes},
                 {"iaea_header_text", m.iaea_header_text}};
    j["root_schema"] = {{"tree", "phsp"},
                        {"branches",
                         {"record_index",
                          "history_id",
                          "history_increment",
                          "is_new_history",
                          "history_start_known",
                          "history_integrity_known",
                          "history_particle_index",
                          "particle_type",
                          "energy_MeV",
                          "weight",
                          "pos_x_cm",
                          "pos_y_cm",
                          "pos_z_cm",
                          "dir_u",
                          "dir_v",
                          "dir_w",
                          "dir_theta_rad",
                          "dir_phi_rad",
                          "radius_xy_cm",
                          (m.nExtraF > 0 ? "extraF" : ""),
                          (m.nExtraI > 0 ? "extraI" : "")}}};
    return j;
}
static void write_metadata_json(const std::filesystem::path &path, const Metadata &m) {
    std::ofstream f(path);
    if (!f)
        throw std::runtime_error("Cannot write metadata: " + path.string());
    f << metadata_json(m).dump(2) << "\n";
    f.close();
    if (!f)
        throw std::runtime_error("Cannot finish metadata: " + path.string());
}

struct Source {
    IAEA_I32 id = 0;
    bool active = false;
    void close() {
        if (!active)
            return;
        IAEA_I32 result = 0;
        iaea_destroy_source(&id, &result);
        active = false;
    }
    ~Source() { close(); }
};

struct RawRec {
    Long64_t record_index = 0, history_id = 0, history_particle_index = 0;
    Bool_t history_start_known = false;
    Bool_t history_integrity_known = true;
    IAEA_I32 n_stat = 0;
    IAEA_I32 type = 0;
    IAEA_Float E = 0, wt = 0;
    IAEA_Float x = 0, y = 0, z = 0;
    IAEA_Float u = 0, v = 0, w = 0;
    float extraF[NUM_EXTRA_FLOAT]{};
    IAEA_I32 extraI[NUM_EXTRA_LONG]{};
};

struct Row {
    Long64_t record_index = 0, history_id = 0, history_particle_index = 0;
    Bool_t is_new_history = false, history_start_known = false;
    Bool_t history_integrity_known = true;
    Int_t history_increment = 0;
    Int_t particle_type = 0;
    Float_t energy_MeV = 0.f;
    Float_t weight = 0.f;

    Float_t x_cm = 0.f, y_cm = 0.f, z_cm = 0.f;
    Float_t dir_u = 0.f, dir_v = 0.f, dir_w = 0.f;

    Float_t theta_rad = 0.f, phi_rad = 0.f;
    Float_t r_xy_cm = 0.f;

    float extraF[NUM_EXTRA_FLOAT]{};
    Int_t extraI[NUM_EXTRA_LONG]{};
};

// Bounded FIFO queues use standard C++ synchronization.
template <class T> class BoundedQueue {
  public:
    explicit BoundedQueue(size_t cap) : cap_(cap) {}

    bool Push(T &&item) {
        std::unique_lock<std::mutex> lk(mu_);
        not_full_.wait(lk, [&] { return closed_ || q_.size() < cap_; });
        if (closed_)
            return false;
        q_.push_back(std::move(item));
        not_empty_.notify_one();
        return true;
    }

    bool Pop(T &out) {
        std::unique_lock<std::mutex> lk(mu_);
        not_empty_.wait(lk, [&] { return closed_ || !q_.empty(); });
        if (q_.empty())
            return false;
        out = std::move(q_.front());
        q_.pop_front();
        not_full_.notify_one();
        return true;
    }

    void Close() {
        std::lock_guard<std::mutex> lk(mu_);
        closed_ = true;
        not_empty_.notify_all();
        not_full_.notify_all();
    }

  private:
    size_t cap_;
    std::deque<T> q_;
    bool closed_ = false;

    std::mutex mu_;
    std::condition_variable not_empty_;
    std::condition_variable not_full_;
};

static std::string make_phsp_name(const std::string &phspBase) {
    std::filesystem::path p(phspBase);
    auto stem = p.filename().string();
    if (stem.empty())
        stem = p.filename().string();
    if (stem.empty())
        stem = "phsp";
    return stem;
}

} // namespace

std::filesystem::path phsp::convert(const ConversionOptions &options) {
    if (options.input.empty() || options.threads < 1 || options.threads > 1024 || options.block_size < 1 ||
        options.queue_capacity < 1 || options.stride < 1 || options.max_records < -1)
        throw std::invalid_argument("Invalid conversion input, thread count, queue, block, stride or limit");
    auto phspBasePath = std::filesystem::absolute(options.input);
    const auto extension = phspBasePath.extension().string();
    if (extension == ".gz" || extension == ".bz2" || extension == ".xz")
        throw std::invalid_argument("Decompress the IAEA files before conversion");
    if (extension == ".IAEAphsp" || extension == ".IAEAheader")
        phspBasePath.replace_extension();
    const std::string phspBase = phspBasePath.string();
    const auto phspName = make_phsp_name(phspBase);
    const auto workingDirectory = std::filesystem::current_path();
    auto outDir = options.output.empty() ? workingDirectory / "results" / phspName
                                         : std::filesystem::absolute(options.output);
    std::filesystem::create_directories(outDir);
    const auto outRoot = outDir / (phspName + ".root");
    const std::filesystem::path temporaryRoot = outRoot.string() + ".partial";
    const auto metaJson = outDir / (phspName + ".json");
    Logger log("convert", options.logging, outDir / (phspName + ".log"));
    try {
        Progress progress(log, options.logging.progress_seconds);
        progress.start("Validate IAEA source");
        const auto logFile = log.file();
        const auto logDir = logFile.parent_path();
        const int threads = static_cast<int>(options.threads);
        const auto blockSize = options.block_size, queueCap = options.queue_capacity;
        const auto maxN = options.max_records, stride = options.stride;
        const bool writeExtras = options.extras, allowIncomplete = options.allow_incomplete;

        // ---- metadata (initial)
        Metadata meta;
        meta.created_at_utc = now_iso8601_utc();
        meta.working_directory = workingDirectory.string();
        meta.input_base = phspBase;
        meta.phsp_name = phspName;
        meta.output_dir = outDir.string();
        meta.output_root = outRoot.string();
        meta.log_dir = logDir.string();
        meta.log_file = logFile.string();
        meta.threads = threads;
        meta.block_size = blockSize;
        meta.queue_cap = queueCap;
        meta.stride = stride;
        meta.maxN = maxN;
        meta.extras = writeExtras;
        meta.allow_incomplete = allowIncomplete;
        // Metadata is written once at the end (after we know counts and header info).

        log.info("Starting PHSP->ROOT MT");
        log.info("Input base='{}'", phspBase);
        log.info("Output root='{}'", outRoot.string());
        log.info("Meta json='{}'", metaJson.string());
        log.info("threads={}, block={}, queue={}, stride={}, max={}, extras={}", threads, blockSize, queueCap,
                 stride, maxN, (writeExtras ? "yes" : "no"));

        // ---- Open PHSP source
        Source source;
        auto &src = source.id;
        IAEA_I32 res = 0;
        IAEA_I32 accessRead = 1;
        int lenIn = (int)phspBase.size();

        iaea_new_source(&src, const_cast<char *>(phspBase.c_str()), &accessRead, &res, lenIn);
        if (res < 0)
            throw std::runtime_error(fmt::format("iaea_new_source failed res={}", res));
        source.active = true;

        iaea_check_file_size_byte_order(&src, &res);
        meta.file_size_check_result = res;
        // Size mismatches are diagnosed below using actual whole-record counts.
        // Byte-order errors remain fatal even in incomplete-input mode.
        if (res != 0 && res != -3) {
            log.error("file size/byte order check failed res={}", res);
            source.close();
            throw std::runtime_error("IAEA source validation failed; see log for details");
        }

        // ---- extras counts from header
        IAEA_I32 nExtraF = 0, nExtraI = 0;
        iaea_get_extra_numbers(&src, &nExtraF, &nExtraI);
        if (nExtraF < 0)
            nExtraF = 0;
        if (nExtraI < 0)
            nExtraI = 0;
        if (nExtraF > NUM_EXTRA_FLOAT)
            nExtraF = NUM_EXTRA_FLOAT;
        if (nExtraI > NUM_EXTRA_LONG)
            nExtraI = NUM_EXTRA_LONG;

        // Store what's in the header even if we decide not to export extras to ROOT.
        meta.header_nExtraF = (int)nExtraF;
        meta.header_nExtraI = (int)nExtraI;

        if (!writeExtras) {
            nExtraF = 0;
            nExtraI = 0;
        }

        meta.nExtraF = (int)nExtraF;
        meta.nExtraI = (int)nExtraI;

        // --- PHSP header-ish metadata (public IAEA API)
        {
            IAEA_I32 np = -1;
            IAEA_I64 nh = 0;
            iaea_get_max_particles(&src, &np, &nh);
            meta.max_particles = (long long)nh;
            meta.max_histories = 0; // The API above returns particle count, not histories.

            IAEA_Float emax = 0;
            iaea_get_maximum_energy(&src, &emax);
            meta.max_energy = (double)emax;

            IAEA_I64 tot = 0, used0 = 0;
            iaea_get_total_original_particles(&src, &tot);
            iaea_get_used_original_particles(&src, &used0);
            meta.total_original_particles = (long long)tot;
            meta.max_histories = (long long)tot;
            meta.used_original_particles = (long long)used0;

            if (meta.header_nExtraF > 0 || meta.header_nExtraI > 0) {
                meta.extra_float_types.assign((size_t)meta.header_nExtraF, 0);
                meta.extra_long_types.assign((size_t)meta.header_nExtraI, 0);
                IAEA_I32 r0 = 0;
                iaea_get_type_extra_variables(
                    &src, &r0, (meta.header_nExtraI > 0 ? (IAEA_I32 *)meta.extra_long_types.data() : nullptr),
                    (meta.header_nExtraF > 0 ? (IAEA_I32 *)meta.extra_float_types.data() : nullptr));
            }

            meta.constant_variables.clear();
            for (IAEA_I32 idx = 0; idx < 8; ++idx) {
                IAEA_Float v = 0;
                IAEA_I32 r0 = 0;
                iaea_get_constant_variable(&src, &idx, &v, &r0);
                if (r0 == 0)
                    meta.constant_variables.push_back((double)v);
            }

            auto hp = phspBasePath;
            hp += ".IAEAheader";
            meta.iaea_header_path = hp.string();
            meta.iaea_header_text = read_text_file(hp);
            if (std::filesystem::exists(hp)) {
                meta.iaea_header_size_bytes = (long long)std::filesystem::file_size(hp);
            }

            auto pp = phspBasePath;
            pp += ".IAEAphsp";
            meta.iaea_phsp_path = pp.string();
            if (std::filesystem::exists(pp)) {
                meta.iaea_phsp_size_bytes = (long long)std::filesystem::file_size(pp);
            }
        }

        // Bound reads by complete records physically present, not by a potentially
        // stale header count. Never feed a partial trailing record to the IAEA reader.
        {
            const auto lengths = header_numbers(meta.iaea_header_text, "RECORD_LENGTH");
            const auto checksums = header_numbers(meta.iaea_header_text, "CHECKSUM");
            const auto contents = header_numbers(meta.iaea_header_text, "RECORD_CONTENTS");
            const long long recordLength = lengths.empty() ? 0 : lengths.front();
            // Verify the declared length against the layout the reader actually uses.
            // W's sign is encoded in the type byte; W is never a separate float.
            long long layoutLength = 5;
            bool validLayout = contents.size() >= 9;
            if (validLayout) {
                for (size_t i = 0; i < 7; ++i) {
                    if (contents[i] != 0 && contents[i] != 1)
                        validLayout = false;
                    if (i != 5)
                        layoutLength += 4 * contents[i];
                }
                validLayout =
                    validLayout && contents[7] == meta.header_nExtraF && contents[8] == meta.header_nExtraI;
                layoutLength += 4 * (meta.header_nExtraF + meta.header_nExtraI);
            }
            if (recordLength <= 0 || meta.max_particles < 0 || !validLayout || recordLength != layoutLength) {
                log.error("Invalid RECORD_LENGTH/RECORD_CONTENTS layout; --allow-incomplete cannot override "
                          "format errors");
                source.close();
                throw std::runtime_error("IAEA source validation failed; see log for details");
            }
            meta.header_checksum_bytes = checksums.empty() ? -1 : checksums.front();
            meta.record_length_bytes = recordLength;
            meta.available_complete_records = meta.iaea_phsp_size_bytes / recordLength;
            meta.trailing_bytes = meta.iaea_phsp_size_bytes % recordLength;
            meta.header_consistent = meta.file_size_check_result == 0 && meta.trailing_bytes == 0 &&
                                     meta.available_complete_records == meta.max_particles;
            if (!meta.header_consistent) {
                log.warning("IAEA size mismatch: header checksum={} bytes, actual={} bytes, record length={} "
                            "bytes, header particles={}, available complete records={}, trailing bytes={}",
                            meta.header_checksum_bytes, meta.iaea_phsp_size_bytes, recordLength,
                            meta.max_particles, meta.available_complete_records, meta.trailing_bytes);
                if (!allowIncomplete) {
                    log.error("Use --allow-incomplete to convert available complete records. The result will "
                              "be marked incomplete; original-history normalization will be disabled.");
                    source.close();
                    throw std::runtime_error("IAEA source validation failed; see log for details");
                }
                log.warning(
                    "Incomplete-input mode: at most {} complete records will be read; {} trailing bytes will "
                    "be ignored. Source completeness and original-history normalization remain disabled.",
                    meta.available_complete_records, meta.trailing_bytes);
            }
        }

        log.info("PHSP extras: nExtraF={}, nExtraI={}", (int)nExtraF, (int)nExtraI);

        std::vector<std::pair<long long, long long>> partialHistoryRanges;
        const auto processingPath = std::filesystem::path(phspBase + ".processing.json");
        meta.processing_source_complete =
            meta.iaea_header_text.find("PHSP_TOOLS_SOURCE_COMPLETE=0") == std::string::npos;
        if (std::filesystem::exists(processingPath)) {
            meta.processing_provenance = nlohmann::json::parse(read_text_file(processingPath));
            meta.processing_source_complete &= meta.processing_provenance.value("source_complete", false);
            for (const auto &range :
                 meta.processing_provenance.value("incomplete_history_ranges", nlohmann::json::array())) {
                const auto first = range.at(0).get<long long>(), last = range.at(1).get<long long>();
                if (first < 0 || last < first || last >= meta.available_complete_records)
                    throw std::runtime_error("Invalid processing history range");
                partialHistoryRanges.emplace_back(first, last);
            }
        } else if (meta.iaea_header_text.find("PHSP_TOOLS_PROCESSING_METADATA_REQUIRED=1") !=
                   std::string::npos)
            throw std::runtime_error(
                "Missing .processing.json required to preserve incomplete-history boundaries");
        progress.finish();
        auto planned =
            meta.available_complete_records / stride + (meta.available_complete_records % stride != 0);
        if (maxN >= 0)
            planned = std::min(planned, maxN);
        progress.start("Convert particle records", static_cast<std::uint64_t>(planned));

        // ---- Queues
        struct RawBlock : std::vector<RawRec> {
            size_t sequence = 0;
        };
        struct RowBlock : std::vector<Row> {
            size_t sequence = 0;
        };
        BoundedQueue<RawBlock> rawQ(queueCap);
        BoundedQueue<RowBlock> rowQ(queueCap);

        std::atomic<int> workersAlive{threads};
        std::atomic<long long> written{0};
        std::atomic<bool> writerFailed{false};
        std::mutex publishMutex;
        std::condition_variable publishReady;
        size_t nextPublish = 0;
        ROOT::EnableThreadSafety();

        // ---- Worker threads
        std::vector<std::jthread> workers;
        workers.reserve(threads);

        for (int tid = 0; tid < threads; ++tid) {
            workers.emplace_back([&, tid] {
                RawBlock in;
                while (rawQ.Pop(in)) {
                    RowBlock out;
                    out.sequence = in.sequence;
                    out.reserve(in.size());

                    for (const auto &r : in) {
                        Row row;
                        row.record_index = r.record_index;
                        row.history_id = r.history_id;
                        row.history_particle_index = r.history_particle_index;
                        row.is_new_history = r.n_stat > 0;
                        row.history_start_known = r.history_start_known;
                        row.history_integrity_known = r.history_integrity_known;
                        row.history_increment = (Int_t)r.n_stat;
                        row.particle_type = (Int_t)r.type;
                        row.energy_MeV = (Float_t)r.E;
                        row.weight = (Float_t)r.wt;

                        row.x_cm = (Float_t)r.x;
                        row.y_cm = (Float_t)r.y;
                        row.z_cm = (Float_t)r.z;

                        row.dir_u = (Float_t)r.u;
                        row.dir_v = (Float_t)r.v;
                        row.dir_w = (Float_t)r.w;

                        double ww = (double)row.dir_w;
                        ww = std::max(-1.0, std::min(1.0, ww));
                        row.theta_rad = (Float_t)std::acos(ww);
                        row.phi_rad = (Float_t)std::atan2((double)row.dir_v, (double)row.dir_u);
                        row.r_xy_cm =
                            (Float_t)std::sqrt((double)row.x_cm * row.x_cm + (double)row.y_cm * row.y_cm);

                        if (nExtraF > 0) {
                            for (int i = 0; i < nExtraF; ++i)
                                row.extraF[i] = r.extraF[i];
                        }
                        if (nExtraI > 0) {
                            for (int i = 0; i < nExtraI; ++i)
                                row.extraI[i] = (Int_t)r.extraI[i];
                        }

                        out.push_back(row);
                    }

                    // Publish in input order. At most one processed block per worker
                    // waits here, so ordering cannot grow an unbounded reorder buffer.
                    std::unique_lock<std::mutex> lock(publishMutex);
                    publishReady.wait(lock, [&] { return writerFailed || out.sequence == nextPublish; });
                    if (writerFailed || !rowQ.Push(std::move(out)))
                        break;
                    ++nextPublish;
                    lock.unlock();
                    publishReady.notify_all();
                    in.clear();
                }

                int left = --workersAlive;
                if (left == 0)
                    rowQ.Close();
            });
        }

        // ---- Writer thread (single ROOT writer)
        std::jthread writerThread([&] {
            try {

                TFile fout(temporaryRoot.c_str(), "RECREATE", "PHSP converted to ROOT");
                if (fout.IsZombie()) {
                    throw std::runtime_error("Cannot create ROOT file: " + outRoot.string());
                }

                fout.SetCompressionAlgorithm(ROOT::RCompressionSetting::EAlgorithm::kLZ4);
                fout.SetCompressionLevel(4);

                TTree tree("phsp", "IAEA PHSP records");
                Row row; // branch buffer

                // Descriptive ROOT branch names.
                tree.Branch("record_index", &row.record_index, "record_index/L");
                tree.Branch("history_id", &row.history_id, "history_id/L");
                tree.Branch("history_increment", &row.history_increment, "history_increment/I");
                tree.Branch("is_new_history", &row.is_new_history, "is_new_history/O");
                tree.Branch("history_start_known", &row.history_start_known, "history_start_known/O");
                tree.Branch("history_integrity_known", &row.history_integrity_known,
                            "history_integrity_known/O");
                tree.Branch("history_particle_index", &row.history_particle_index,
                            "history_particle_index/L");
                tree.Branch("particle_type", &row.particle_type, "particle_type/I");

                tree.Branch("energy_MeV", &row.energy_MeV, "energy_MeV/F");
                tree.Branch("weight", &row.weight, "weight/F");

                tree.Branch("pos_x_cm", &row.x_cm, "pos_x_cm/F");
                tree.Branch("pos_y_cm", &row.y_cm, "pos_y_cm/F");
                tree.Branch("pos_z_cm", &row.z_cm, "pos_z_cm/F");

                tree.Branch("dir_u", &row.dir_u, "dir_u/F");
                tree.Branch("dir_v", &row.dir_v, "dir_v/F");
                tree.Branch("dir_w", &row.dir_w, "dir_w/F");

                tree.Branch("dir_theta_rad", &row.theta_rad, "dir_theta_rad/F");
                tree.Branch("dir_phi_rad", &row.phi_rad, "dir_phi_rad/F");
                tree.Branch("radius_xy_cm", &row.r_xy_cm, "radius_xy_cm/F");

                if (nExtraF > 0) {
                    tree.Branch("extraF", row.extraF,
                                ("extraF[" + std::to_string((int)nExtraF) + "]/F").c_str());
                }
                if (nExtraI > 0) {
                    tree.Branch("extraI", row.extraI,
                                ("extraI[" + std::to_string((int)nExtraI) + "]/I").c_str());
                }

                tree.SetAutoSave(200LL * 1024 * 1024);
                tree.SetAutoFlush(500000);
                TTreeCache::SetLearnEntries(100);

                RowBlock block;
                while (rowQ.Pop(block)) {
                    for (const auto &r : block) {
                        row = r;
                        if (tree.Fill() < 0)
                            throw std::runtime_error("ROOT tree write failed");
                        ++written;
                        progress.advance();
                    }
                    block.clear();
                }

                if (fout.Write() <= 0 || fout.TestBit(TFile::kWriteError))
                    throw std::runtime_error("ROOT file write failed");
                fout.Close();
                log.info("Writer done. Total written={}", (long long)written.load());
            } catch (const std::exception &e) {
                log.error("Writer failed: {}", e.what());
                writerFailed = true;
                rawQ.Close();
                rowQ.Close();
                publishReady.notify_all();
            }
        });

        // ---- Reader loop (main thread)

        long long seen = 0;
        long long used = 0;
        Long64_t historyId = 0, historyParticleIndex = -1;
        bool historyStartKnown = false, readFailed = false;
        size_t sequence = 0;

        RawBlock cur;
        cur.reserve(blockSize);

        while (!writerFailed && seen < meta.available_complete_records && (maxN < 0 || used < maxN)) {
            RawRec r;

            iaea_get_particle(&src, &r.n_stat, &r.type, &r.E, &r.wt, &r.x, &r.y, &r.z, &r.u, &r.v, &r.w,
                              r.extraF, r.extraI);

            if (r.n_stat < 0) {
                readFailed = true;
                log.error("Unexpected read failure at record {}", seen);
                break;
            }
            r.record_index = seen;
            for (const auto &range : partialHistoryRanges)
                if (seen >= range.first && seen <= range.second) {
                    r.history_integrity_known = false;
                    break;
                }
            if (r.n_stat > 0) {
                historyId += r.n_stat;
                historyParticleIndex = 0;
                historyStartKnown = true;
                ++meta.history_markers;
            } else
                ++historyParticleIndex;
            r.history_id = historyId;
            r.history_particle_index = historyParticleIndex;
            r.history_start_known = historyStartKnown;
            ++seen;

            if (stride > 1 && ((seen - 1) % stride != 0))
                continue;

            ++used;
            cur.push_back(r);

            if (cur.size() >= blockSize) {
                cur.sequence = sequence++;
                if (!rawQ.Push(std::move(cur)))
                    break;
                cur.clear();
                cur.reserve(blockSize);
            }

            if (used % 1000000 == 0) {
                log.debug("Reader seen={}, selected={}", seen, used);
            }
        }

        if (!cur.empty()) {
            cur.sequence = sequence++;
            rawQ.Push(std::move(cur));
        }

        source.close();
        rawQ.Close();

        log.info("Reader done. seen={}, used={}", seen, used);

        // ---- join + final metadata update
        for (auto &w : workers)
            w.join();
        writerThread.join();

        progress.finish();
        progress.start("Publish ROOT and provenance");
        meta.seen = seen;
        meta.used = used;
        meta.written = written.load();
        meta.source_complete = !readFailed && meta.header_consistent && meta.processing_source_complete &&
                               seen == meta.available_complete_records;
        if (writerFailed || readFailed || meta.written != used) {
            log.error("Conversion incomplete: seen={}, used={}, written={}", seen, used, meta.written);
            throw std::runtime_error("Conversion did not write all selected records");
        }
        // Keep provenance inside ROOT as well as in the sidecar.
        {
            TFile update(temporaryRoot.c_str(), "UPDATE");
            if (update.IsZombie())
                throw std::runtime_error("Cannot reopen temporary ROOT file");
            TObjString provenance(metadata_json(meta).dump().c_str());
            if (provenance.Write("phsp_metadata", TObject::kOverwrite) <= 0)
                throw std::runtime_error("Cannot write ROOT metadata");
        }
        const std::filesystem::path temporaryJson = metaJson.string() + ".partial";
        write_metadata_json(temporaryJson, meta);
        std::filesystem::rename(temporaryRoot, outRoot);
        std::filesystem::rename(temporaryJson, metaJson);

        progress.finish();
        log.info("Finished: {} records -> {}", meta.written, outRoot.string());
        return outRoot;
    } catch (const std::exception &error) {
        log.error("Conversion failed: {}", error.what());
        throw;
    }
}
