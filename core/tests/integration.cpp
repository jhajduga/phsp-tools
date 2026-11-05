#include <TFile.h>
#include <TH1D.h>
#include <TObjString.h>
#include <TTree.h>
#include <TTreeReader.h>
#include <TTreeReaderValue.h>
#include <nlohmann/json.hpp>

#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;
void check(bool ok, const std::string &message) {
    if (!ok)
        throw std::runtime_error(message);
}
void near(double a, double b, const std::string &message) {
    check(std::abs(a - b) <= 1e-7 * std::max({1., std::abs(a), std::abs(b)}),
          message + ": " + std::to_string(a) + " != " + std::to_string(b));
}
std::string quote(const std::string &s) {
    std::string q = "'";
    for (char c : s)
        q += c == '\'' ? "'\\''" : std::string(1, c);
    return q + "'";
}
void run(const std::vector<std::string> &args, const fs::path &log, bool success = true) {
    std::string cmd;
    for (const auto &a : args)
        cmd += quote(a) + " ";
    cmd += ">" + quote(log.string()) + " 2>&1";
    const int status = std::system(cmd.c_str());
    if ((status == 0) != success) {
        std::ifstream in(log);
        std::cerr << in.rdbuf();
        throw std::runtime_error("Unexpected process exit: " + cmd);
    }
}
json readJson(const fs::path &p) {
    std::ifstream in(p);
    json j;
    in >> j;
    return j;
}
void replaceText(const fs::path &path, const std::string &from, const std::string &to) {
    std::ifstream in(path);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    auto updated = text;
    const auto at = updated.find(from);
    check(at != std::string::npos, "fixture header field missing");
    updated.replace(at, from.size(), to);
    std::ofstream out(path);
    out << updated;
}

// Independently encode IAEA records: one signed type byte, seven float32,
// one int32 incremental-history extra. Seven rows repeat over many block edges.
void fixture(const fs::path &base, int repeats, bool withIncrements = true, bool withMarkers = true) {
    std::ofstream data(base.string() + ".IAEAphsp", std::ios::binary);
    const std::array<float, 7> energies{-1, 2, -3, -4, 5, -0.0f, 6};
    const std::array<int, 7> increments{1, 0, 3, 1, 0, 1, 0};
    for (int k = 0; k < repeats; ++k) {
        for (int i = 0; i < 7; ++i) {
            const signed char type = i == 1 ? -2 : 1;
            const std::array<float, 7> row{withMarkers ? energies[i] : std::abs(energies[i]),
                                           float(i) - 3,
                                           0,
                                           10,
                                           0,
                                           0,
                                           i == 1 ? 2.f : 1.f};
            data.write(reinterpret_cast<const char *>(&type), 1);
            data.write(reinterpret_cast<const char *>(row.data()), sizeof(row));
            if (withIncrements)
                data.write(reinterpret_cast<const char *>(&increments[i]), 4);
        }
    }
    data.close();
    std::ofstream header(base.string() + ".IAEAheader");
    header << "$IAEA_INDEX:\n1\n$TITLE:\ntest\n$FILE_TYPE:\n0\n$CHECKSUM:\n"
           << fs::file_size(base.string() + ".IAEAphsp") << "\n$RECORD_CONTENTS:\n1\n1\n1\n1\n1\n1\n1\n0\n"
           << (withIncrements ? "1\n1\n" : "0\n") << "$RECORD_CONSTANT:\n\n$RECORD_LENGTH:\n"
           << (withIncrements ? 33 : 29) << "\n$BYTE_ORDER:\n1234\n$ORIG_HISTORIES:\n"
           << 6 * repeats << "\n$PARTICLES:\n"
           << 7 * repeats << "\n$PHOTONS:\n"
           << 6 * repeats << "\n$ELECTRONS:\n"
           << repeats << "\n$COORDINATE_SYSTEM_DESCRIPTION:\nx y z in cm\n$MACHINE_TYPE:\ntest\n"
           << "$MONTE_CARLO_CODE_VERSION:\nfixture\n$GLOBAL_PHOTON_ENERGY_CUTOFF:\n0\n"
           << "$GLOBAL_PARTICLE_ENERGY_CUTOFF:\n0\n$TRANSPORT_PARAMETERS:\nfixture\n";
}

int main(int argc, char **argv) {
    try {
        check(argc == 6, "expected converter analyzer compare pipeline work-dir");
        const std::string converter = argv[1], analyzer = argv[2], compare = argv[3], pipeline = argv[4];
        const auto work = fs::absolute(argv[5]);
        fs::create_directories(work);
        const auto base = work / "synthetic.v2";
        constexpr int repeats = 503;
        fixture(base, repeats);
        run({converter, base.string(), "--output-dir", (work / "converted").string(), "--threads", "4",
             "--block", "1000", "--queue", "1", "--extras"},
            work / "convert.log");
        const auto converted = work / "converted/synthetic.v2.root";
        {
            TFile input(converted.c_str(), "READ");
            auto *tree = dynamic_cast<TTree *>(input.Get("phsp"));
            check(tree && tree->GetEntries() == repeats * 7, "converted particle count");
            check(input.Get("phsp_metadata") != nullptr, "embedded metadata");
            TTreeReader r(tree);
            TTreeReaderValue<Long64_t> index(r, "record_index"), id(r, "history_id"),
                order(r, "history_particle_index");
            TTreeReaderValue<Int_t> increment(r, "history_increment");
            TTreeReaderValue<Float_t> w(r, "dir_w"), energy(r, "energy_MeV");
            Long64_t i = 0;
            const std::array<int, 7> expectedId{1, 1, 4, 5, 5, 6, 6};
            const std::array<int, 7> expectedOrder{0, 1, 0, 0, 1, 0, 1};
            while (r.Next()) {
                check(*index == i, "input order across blocks");
                check(*id == (i / 7) * 6 + expectedId[i % 7], "cumulative history IDs including gaps");
                check(*order == expectedOrder[i % 7], "particle order in history");
                check(*energy >= 0, "positive kinetic energy");
                check(*w == (i % 7 == 1 ? -1 : 1), "signed particle direction");
                if (i % 7 == 5)
                    check(*increment == 1, "negative zero starts history");
                ++i;
            }
        }
        const auto meta = readJson(work / "converted/synthetic.v2.json");
        check(meta["history"]["source_complete"] == true, "complete conversion metadata");
        check(meta["phsp"]["max_particles"] == repeats * 7, "IAEA API particle count metadata");
        json reference;
        for (const std::string threads : {"1", "3"}) {
            const auto dest = work / ("analysis" + threads);
            run({analyzer, converted.string(), "--output", dest.string(), "--threads", threads, "--bins",
                 "16", "--no-plots", "--native-plane"},
                work / ("analysis" + threads + ".log"));
            const auto j = readJson(dest / "report.json");
            const auto &s = j["species"]["all"];
            check(s["records"] == repeats * 7, "analysis record count");
            near(s["sum_weights"], repeats * 8, "weight sum");
            near(s["mean"]["energy_MeV"], 23. / 8, "weighted mean energy");
            near(s["mean_energy_unweighted_MeV"], 3., "unweighted mean energy");
            near(s["effective_records"], repeats * 64. / 10, "weight effective records");
            near(s["backward_weight_fraction"], .25, "backward weight fraction");
            const auto &h = j["histories"];
            check(h["complete_histories"] == repeats * 4, "complete histories");
            check(h["known_empty_histories_from_increments"] == repeats * 2, "empty history increments");
            near(h["mean"]["particles_per_scored_history"], 1.75, "mean scored history length");
            near(h["mean"]["sum_energy_MeV"], 5.25, "mean energy per history");
            near(h["mean"]["sum_weighted_energy_MeV"], 5.75, "weighted energy per history");
            check(j["normalization"]["available"] == true, "primary normalization");
            check(j["analysis_plane"]["mode"] == "native", "native-plane analysis");
            TFile output((dest / "analysis.root").c_str(), "READ");
            auto *spectrum = dynamic_cast<TH1D *>(output.Get("all/all_energy"));
            near(spectrum->Integral(), repeats * 8, "weighted histogram integral");
            check(spectrum->GetSumw2N() > 0, "weighted histogram errors");
            auto *historyTree = dynamic_cast<TTree *>(output.Get("histories/histories"));
            check(historyTree && historyTree->GetEntries() == repeats * 4, "history tree");
            check(fs::exists(dest / "dataframe.dot"), "RDF graph");
            if (threads == "1")
                reference = j;
            else
                near(s["mean"]["energy_MeV"], reference["species"]["all"]["mean"]["energy_MeV"],
                     "thread reproducibility");
        }
        run({compare, (work / "analysis1/analysis.root").string(),
             (work / "analysis3/analysis.root").string(), "--output", (work / "comparison").string(),
             "--no-plots"},
            work / "compare.log");
        const auto comparison = readJson(work / "comparison/comparison.json");
        near(comparison["histograms"]["all_energy"]["distances"]["total_variation"], 0,
             "identical spectra TV");
        near(comparison["histograms"]["all_xy"]["distances"]["jensen_shannon_divergence_nats"], 0,
             "identical XY JS");
        // Move the candidate's mass to an empty bin: disjoint binned support has
        // TV=1 and JS=ln(2), independent of the reference spectrum's shape.
        fs::copy_file(work / "analysis1/analysis.root", work / "disjoint.root",
                      fs::copy_options::overwrite_existing);
        double expectedDistance = 0;
        {
            TFile altered((work / "disjoint.root").c_str(), "UPDATE");
            auto *h = dynamic_cast<TH1D *>(altered.Get("all/all_energy"));
            check(h && h->GetBinContent(2) == 0, "empty reference energy bin");
            const double center = h->GetBinCenter(2), sum = h->Integral();
            for (int i = 1; i <= h->GetNbinsX(); ++i)
                expectedDistance += h->GetBinContent(i) * std::abs(h->GetBinCenter(i) - center) / sum;
            h->Reset();
            h->Fill(center);
            altered.cd("all");
            h->Write("all_energy", TObject::kOverwrite);
        }
        run({compare, (work / "analysis1/analysis.root").string(), (work / "disjoint.root").string(),
             "--output", (work / "disjoint_comparison").string(), "--no-plots"},
            work / "disjoint_compare.log");
        auto disjoint =
            readJson(work / "disjoint_comparison/comparison.json")["histograms"]["all_energy"]["distances"];
        near(disjoint["total_variation"], 1, "disjoint TV");
        near(disjoint["jensen_shannon_divergence_nats"], std::log(2.), "disjoint JS");
        near(disjoint["wasserstein_bin_centers_axis_units"], expectedDistance, "Wasserstein distance");
        // One pipeline handles both IAEA and ROOT inputs, with paths resolved
        // relative to its config. Every stage uses the same bin definitions.
        json config = {
            {"output_dir", "pipeline_run"},
            {"threads", 2},
            {"conversion", {{"max_records", 7}, {"extras", true}}},
            {"analysis",
             {{"max_records", 7}, {"bins", 16}, {"energy_max_MeV", 7}, {"xy_max_cm", 4}, {"plots", false}}},
            {"datasets", json::array({{{"name", "iaea"}, {"input", "synthetic.v2"}},
                                      {{"name", "root"}, {"input", "converted/synthetic.v2.root"}},
                                      {{"name", "iaea_again"}, {"input", "synthetic.v2"}}})},
            {"comparisons", json::array({{{"reference", "iaea"}, {"candidate", "root"}}})}};
        {
            std::ofstream out(work / "pipeline.json");
            out << config.dump(2);
        }
        run({pipeline, (work / "pipeline.json").string(), "--progress-seconds", "0.1"},
            work / "pipeline_process.log");
        const auto manifest = readJson(work / "pipeline_run/pipeline_manifest.json");
        check(manifest["status"] == "complete", "pipeline completion manifest");
        check(manifest["datasets"]["iaea"]["status"] == "complete", "pipeline conversion stage");
        check(manifest["datasets"]["root"]["status"] == "complete", "pipeline ROOT input stage");
        check(manifest["datasets"]["iaea_again"]["status"] == "complete",
              "repeated IAEA conversion in one process");
        check(manifest["comparisons"]["root_vs_iaea"]["status"] == "complete", "pipeline comparison stage");
        check(manifest["resolved_config"]["datasets"][0]["analysis"]["bins"] == 16,
              "resolved pipeline options");
        check(fs::exists(work / "pipeline_run/index.html"), "pipeline HTML index");
        near(
            readJson(work /
                     "pipeline_run/comparisons/root_vs_iaea/comparison.json")["histograms"]["all_energy"]
                                                                             ["distances"]["total_variation"],
            0, "pipeline same particle distributions");
        for (const auto &path :
             {work / "pipeline_run/pipeline.log", work / "pipeline_run/datasets/iaea/analysis/analysis.log",
              work / "pipeline_run/comparisons/root_vs_iaea/comparison.log"}) {
            std::ifstream log(path);
            const std::string text((std::istreambuf_iterator<char>(log)), std::istreambuf_iterator<char>());
            check(text.find("START ") != std::string::npos && text.find("DONE ") != std::string::npos,
                  "stage boundaries logged");
        }
        config["datasets"][1]["analysis"] = {{"bins", 32}};
        {
            std::ofstream out(work / "invalid_pipeline.json");
            out << config.dump(2);
        }
        run({pipeline, (work / "invalid_pipeline.json").string(), "--output",
             (work / "invalid_pipeline").string()},
            work / "invalid_pipeline.log", false);
        check(readJson(work / "invalid_pipeline/pipeline_manifest.json")["status"] == "failed",
              "failed pipeline manifest");
        check(!fs::exists(work / "invalid_pipeline/datasets"),
              "mismatched bins rejected before dataset processing");
        // A limit through a history drops that final history from exact summaries.
        run({converter, base.string(), "--max", "4", "--output-dir", (work / "partial").string(), "--threads",
             "2"},
            work / "partial.log");
        run({analyzer, (work / "partial/synthetic.v2.root").string(), "--output",
             (work / "partial_analysis").string(), "--no-plots"},
            work / "partial_analysis.log");
        auto partial = readJson(work / "partial_analysis/report.json");
        check(partial["histories"]["complete_histories"] == 2, "partial last history excluded");
        check(partial["histories"]["incomplete_or_invalid_histories"] == 1, "partial history flagged");
        check(partial["normalization"]["available"] == false, "partial primary normalization disabled");
        for (const auto &pair : {std::pair{"2", "1"}, std::pair{"4", "2"}}) {
            const auto dest = work / ("prefix" + std::string(pair.first));
            run({analyzer, converted.string(), "--max", pair.first, "--output", dest.string(), "--no-plots"},
                work / "prefix.log");
            auto j = readJson(dest / "report.json");
            check(j["histories"]["complete_histories"] == std::stoi(pair.second), "analysis prefix boundary");
        }
        run({converter, base.string(), "--stride", "2", "--output-dir", (work / "stride").string(),
             "--threads", "2"},
            work / "stride.log");
        run({analyzer, (work / "stride/synthetic.v2.root").string(), "--output",
             (work / "stride_analysis").string(), "--no-plots"},
            work / "stride_analysis.log");
        check(readJson(work / "stride_analysis/report.json")["histories"]["available"] == false,
              "stride histories disabled");
        // Marker-only IAEA files also preserve a zero-energy history start.
        fixture(work / "noextra", 1, false);
        run({converter, (work / "noextra").string(), "--output-dir", (work / "marker").string(), "--threads",
             "1"},
            work / "marker.log");
        run({analyzer, (work / "marker/noextra.root").string(), "--output",
             (work / "marker_analysis").string(), "--no-plots"},
            work / "marker_analysis.log");
        check(readJson(work / "marker_analysis/report.json")["histories"]["complete_histories"] == 4,
              "signbit-only markers including negative zero");
        fixture(work / "nomarkers", 1, false, false);
        run({converter, (work / "nomarkers").string(), "--output-dir",
             (work / "nomarkers_converted").string(), "--threads", "1"},
            work / "nomarkers_convert.log");
        run({analyzer, (work / "nomarkers_converted/nomarkers.root").string(), "--output",
             (work / "nomarkers_analysis").string(), "--no-plots"},
            work / "nomarkers_analysis.log");
        check(readJson(work / "nomarkers_analysis/report.json")["histories"]["available"] == false,
              "missing history markers");
        // Legacy ROOT input with invalid rows and zero weights: particle analysis
        // remains available, but a history flag must never become a fake event ID.
        {
            TFile legacy((work / "legacy.root").c_str(), "RECREATE");
            TTree t("phsp", "Legacy test with QA problems");
            Float_t e = 0, w = 1, x = 0, y = 0, z = 10, u = 0, v = 0, dw = 1;
            Int_t type = 1, history = 0;
            t.Branch("energy_MeV", &e);
            t.Branch("weight", &w);
            t.Branch("pos_x_cm", &x);
            t.Branch("pos_y_cm", &y);
            t.Branch("pos_z_cm", &z);
            t.Branch("dir_u", &u);
            t.Branch("dir_v", &v);
            t.Branch("dir_w", &dw);
            t.Branch("particle_type", &type);
            t.Branch("history_id", &history);
            const std::array<float, 8> energies{1, 3, std::numeric_limits<float>::quiet_NaN(), 4, 5, 6, 7, 2};
            const std::array<float, 8> weights{1, 2, 1, -1, 0, 1, 1, 0};
            for (int i = 0; i < 8; ++i) {
                e = energies[i];
                w = weights[i];
                x = i - 3;
                dw = i == 5 ? .5f : 1.f;
                type = i == 1 ? 2 : 1;
                t.Fill();
            }
            t.Write();
        }
        run({analyzer, (work / "legacy.root").string(), "--output", (work / "legacy_analysis").string(),
             "--bins", "16", "--energy-max", "2", "--xy-max", "1", "--no-plots"},
            work / "legacy_analysis.log");
        const auto qa = readJson(work / "legacy_analysis/report.json");
        check(qa["quality"]["invalid_numeric_energy_or_weight_records"] == 2, "invalid numeric rows");
        check(qa["quality"]["nonunit_direction_records"] == 1, "invalid directions");
        check(qa["species"]["all"]["records"] == 5, "valid rows including zero weights");
        near(qa["species"]["all"]["mean"]["energy_MeV"], 3.5, "QA weighted mean");
        near(qa["histograms"]["all_energy"]["out_of_range_sum"], 3, "histogram overflow reported");
        check(qa["histories"]["available"] == false, "legacy histories disabled");
        run({analyzer, (work / "legacy.root").string(), "--output", (work / "electrons").string(),
             "--particle", "2", "--no-plots"},
            work / "electrons.log");
        check(readJson(work / "electrons/report.json")["species"]["all"]["records"] == 1,
              "particle selection");
        run({compare, (work / "analysis1/analysis.root").string(),
             (work / "legacy_analysis/analysis.root").string(), "--output",
             (work / "bad_comparison").string(), "--no-plots"},
            work / "bad_compare.log", false);
        // Header/checksum mismatch must fail instead of reporting complete conversion.
        {
            std::ofstream out(base.string() + ".IAEAphsp", std::ios::binary | std::ios::app);
            out.put('x');
        }
        run({converter, base.string(), "--output-dir", (work / "broken").string()}, work / "broken.log",
            false);
        run({converter, base.string(), "--allow-incomplete", "--threads", "2", "--output-dir",
             (work / "salvaged").string()},
            work / "salvaged.log");
        auto salvaged = readJson(work / "salvaged/synthetic.v2.json");
        check(salvaged["counts"]["written_records"] == repeats * 7,
              "trailing byte is never read as a record");
        check(salvaged["input_integrity"]["trailing_bytes"] == 1, "trailing byte provenance");
        check(salvaged["history"]["source_complete"] == false, "trailing byte source remains incomplete");
        // Reproduce the real TNSIM case: the file ends inside a record and its
        // header describes a larger source. Salvage only whole records.
        fixture(work / "truncated", 1);
        fs::resize_file(work / "truncated.IAEAphsp", 6 * 33 + 21);
        run({converter, (work / "truncated").string(), "--output-dir", (work / "truncated_strict").string()},
            work / "truncated_strict.log", false);
        run({converter, (work / "truncated").string(), "--allow-incomplete", "--threads", "2", "--extras",
             "--output-dir", (work / "truncated_converted").string()},
            work / "truncated.log");
        const auto truncated = readJson(work / "truncated_converted/truncated.json");
        check(truncated["counts"]["written_records"] == 6, "truncated whole-record count");
        check(truncated["input_integrity"]["available_complete_records"] == 6, "available record provenance");
        check(truncated["input_integrity"]["trailing_bytes"] == 21, "partial record bytes");
        check(truncated["input_integrity"]["header_checksum_bytes"] == 231, "original header size retained");
        check(truncated["input_integrity"]["actual_size_bytes"] == 219, "actual size retained");
        check(truncated["history"]["source_complete"] == false, "truncated source completeness");
        run({analyzer, (work / "truncated_converted/truncated.root").string(), "--output",
             (work / "truncated_analysis").string(), "--no-plots"},
            work / "truncated_analysis.log");
        auto truncatedAnalysis = readJson(work / "truncated_analysis/report.json");
        check(truncatedAnalysis["normalization"]["available"] == false,
              "incomplete source original-history normalization disabled");
        check(truncatedAnalysis["histories"]["complete_histories"] == 3,
              "complete histories preceding truncation");
        check(truncatedAnalysis["histories"]["incomplete_or_invalid_histories"] == 1,
              "final truncated history excluded");
        check(truncatedAnalysis["notes"].dump().find("The source size") != std::string::npos,
              "incomplete-source report note");
        // A stale count must not discard valid records physically present.
        fixture(work / "stale_count", 1);
        replaceText(work / "stale_count.IAEAheader", "$PARTICLES:\n7\n", "$PARTICLES:\n2\n");
        run({converter, (work / "stale_count").string(), "--output-dir", (work / "stale_strict").string()},
            work / "stale_strict.log", false);
        run({converter, (work / "stale_count").string(), "--allow-incomplete", "--threads", "1",
             "--output-dir", (work / "stale_converted").string()},
            work / "stale.log");
        auto stale = readJson(work / "stale_converted/stale_count.json");
        check(stale["counts"]["written_records"] == 7,
              "read physical records rather than stale header count");
        check(stale["history"]["source_complete"] == false, "stale count prevents completeness");
        fixture(work / "byte_order", 1);
        replaceText(work / "byte_order.IAEAheader", "$BYTE_ORDER:\n1234\n", "$BYTE_ORDER:\n4321\n");
        run({converter, (work / "byte_order").string(), "--allow-incomplete", "--output-dir",
             (work / "byte_order_rejected").string()},
            work / "byte_order.log", false);
        fixture(work / "bad_layout", 1);
        replaceText(work / "bad_layout.IAEAheader", "$RECORD_LENGTH:\n33\n", "$RECORD_LENGTH:\n34\n");
        run({converter, (work / "bad_layout").string(), "--allow-incomplete", "--output-dir",
             (work / "bad_layout_rejected").string()},
            work / "bad_layout.log", false);
        run({analyzer, converted.string(), "--max", "0", "--output", (work / "empty").string(), "--no-plots"},
            work / "empty.log");
        check(readJson(work / "empty/report.json")["species"]["all"]["records"] == 0, "empty selection");
        run({analyzer, converted.string(), "--bins", "0"}, work / "bad_cli.log", false);
        run({converter, base.string(), "--threads", "bad"}, work / "bad_convert_cli.log", false);
        std::cout << "PHSP conversion and analysis integration checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
