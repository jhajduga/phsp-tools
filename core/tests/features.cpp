#include "phsp/analysis.hpp"
#include "phsp/comparison.hpp"
#include "phsp/conversion.hpp"
#include "phsp/pipeline.hpp"
#include "phsp/processing.hpp"
#include <TFile.h>
#include <TTree.h>
#include <TTreeReader.h>
#include <TTreeReaderValue.h>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace fs = std::filesystem;
using json = nlohmann::json;
void check(bool value, const char *reason) {
    if (!value)
        throw std::runtime_error(reason);
}
void near(double a, double b) { check(std::abs(a - b) < 1e-5, "Unexpected numerical result"); }
json read(const fs::path &path) {
    std::ifstream f(path);
    json j;
    f >> j;
    return j;
}
std::string text(const fs::path &path) {
    std::ifstream f(path);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}
template <class F> void rejects(F &&f) {
    bool failed = false;
    try {
        f();
    } catch (const std::exception &) {
        failed = true;
    }
    check(failed, "Expected a rejected operation");
}
void iaea(const fs::path &base, bool alternate = false, bool marked = true, bool constant = false) {
    std::ofstream data(base.string() + ".IAEAphsp", std::ios::binary);
    const std::array<float, 7> energies{-1, 2, -3, -4, 5, -0.0f, 6};
    const std::array<int, 7> increments{1, 0, 3, 1, 0, 1, 0};
    for (int i = 0; i < 7; ++i) {
        const signed char type = i == 1 ? -2 : 1;
        const std::array<float, 7> values{energies[i], float(i) - 3, 0, 10, 0, 0, i == 1 ? 2.f : 1.f};
        const std::array<int, 6> extras{(!marked && i == 0) ? 0 : increments[i], 11, 12, 13, 14, 15};
        data.write(reinterpret_cast<const char *>(&type), 1);
        for (int k = 0; k < 7; ++k)
            if (!constant || k != 3)
                data.write(reinterpret_cast<const char *>(&values[k]), 4);
        data.write(reinterpret_cast<const char *>(extras.data()), 24);
    }
    data.close();
    std::ofstream header(base.string() + ".IAEAheader");
    header << "$IAEA_INDEX:\n1\n$TITLE:\ntest\n$FILE_TYPE:\n0\n$CHECKSUM:\n"
           << fs::file_size(base.string() + ".IAEAphsp") << "\n$RECORD_CONTENTS:\n1\n1\n"
           << (constant ? 0 : 1) << "\n1\n1\n1\n1\n0\n6\n1\n0\n0\n0\n0\n"
           << (alternate ? 2 : 3) << "\n$RECORD_CONSTANT:\n"
           << (constant ? "10\n" : "\n") << "$RECORD_LENGTH:\n"
           << (constant ? 49 : 53)
           << "\n$BYTE_ORDER:\n1234\n$ORIG_HISTORIES:\n6\n$PARTICLES:\n7\n$PHOTONS:\n6\n$ELECTRONS:\n1\n"
           << "$COORDINATE_SYSTEM_DESCRIPTION:\ntarget centred positive "
              "Z\n$MACHINE_TYPE:\ntest\n$MONTE_CARLO_CODE_VERSION:\nfixture\n"
           << "$GLOBAL_PHOTON_ENERGY_CUTOFF:\n0\n$GLOBAL_PARTICLE_ENERGY_CUTOFF:\n0\n$TRANSPORT_PARAMETERS:"
              "\nfixture\n";
}
void rootRays(const fs::path &path, double sourceZ, bool mixed = false) {
    TFile file(path.c_str(), "RECREATE");
    TTree t("phsp", "known rays");
    Float_t e = 1, weight = 1, x = 0, y = 0, z = 0, u = .6f, v = 0, w = .8f;
    Int_t type = 1;
    t.Branch("energy_MeV", &e);
    t.Branch("weight", &weight);
    t.Branch("pos_x_cm", &x);
    t.Branch("pos_y_cm", &y);
    t.Branch("pos_z_cm", &z);
    t.Branch("dir_u", &u);
    t.Branch("dir_v", &v);
    t.Branch("dir_w", &w);
    t.Branch("particle_type", &type);
    z = sourceZ;
    x = 1 + (sourceZ - 10) * .75;
    t.Fill();
    if (mixed) {
        z = 80;
        x = 53.5;
        t.Fill();
        type = 2;
        u = 0;
        w = 1;
        z = 20;
        x = 2;
        t.Fill();
        type = 3;
        z = 40;
        x = -1;
        t.Fill();
        type = 4;
        z = 30;
        x = 3;
        t.Fill();
        type = 5;
        z = 35;
        x = -2;
        t.Fill();
        type = 1;
        w = -1;
        z = 10;
        t.Fill();
        w = 1;
        z = 120;
        t.Fill();
        u = 1;
        w = 0;
        z = 10;
        t.Fill();
    }
    t.Write();
}
fs::path convert(const fs::path &input, const fs::path &output) {
    phsp::ConversionOptions o;
    o.input = input;
    o.output = output;
    o.extras = true;
    o.threads = 2;
    o.logging.progress_seconds = 0;
    return phsp::convert(o);
}
phsp::AnalysisOptions options(const fs::path &input, const fs::path &output, bool plots = false) {
    phsp::AnalysisOptions o;
    o.input = input;
    o.output = output;
    o.threads = 2;
    o.bins = 16;
    o.energy_max = 8;
    o.xy_max = 100;
    o.plots = plots;
    o.logging.progress_seconds = 0;
    return o;
}
int main(int argc, char **argv) {
    try {
        check(argc == 2, "Expected work directory");
        const fs::path root = fs::absolute(argv[1]);
        fs::create_directories(root);
        rootRays(root / "near.root", 10);
        rootRays(root / "far.root", 50);
        rootRays(root / "mixed.root", 10, true);
        const auto a = phsp::analyze(options(root / "near.root", root / "near"));
        const auto b = phsp::analyze(options(root / "far.root", root / "far"));
        near(read(a / "report.json")["species"]["all"]["mean"]["x_cm"], 68.5);
        near(read(b / "report.json")["species"]["all"]["mean"]["x_cm"], 68.5);
        check(read(a / "report.json")["analysis_plane"]["target_z_cm"] == 100, "Default reference plane");
        phsp::ComparisonOptions compare;
        compare.reference = a / "analysis.root";
        compare.candidate = b / "analysis.root";
        compare.output = root / "aligned";
        compare.plots = false;
        phsp::compare(compare);
        near(read(compare.output / "comparison.json")["histograms"]["all_xy"]["distances"]["total_variation"],
             0);
        auto other = options(root / "far.root", root / "other");
        other.project_z = 110;
        phsp::analyze(other);
        compare.candidate = other.output / "analysis.root";
        compare.output = root / "bad_plane";
        rejects([&] { phsp::compare(compare); });
        auto native = options(root / "far.root", root / "native");
        native.project_z.reset();
        phsp::analyze(native);
        near(read(native.output / "report.json")["species"]["all"]["mean"]["x_cm"], 31);
        auto offset = options(root / "near.root", root / "offset");
        offset.source_z_offset_cm = 40;
        phsp::analyze(offset);
        near(read(offset.output / "report.json")["species"]["all"]["mean"]["x_cm"], 38.5);
        auto mixed = options(root / "mixed.root", root / "mixed", true);
        phsp::analyze(mixed);
        const auto m = read(mixed.output / "report.json");
        check(m["quality"]["selected_records"] == 6, "Reachable-ray count");
        check(m["quality"]["projection_rejected_records"] == 3, "Rejected-ray count");
        for (const auto *type : {"all", "photon", "electron", "positron", "neutron", "proton"})
            check(m["plot_groups"][type].size() >= 18, "Full per-species plots");
        check(!m["plot_groups"].contains("other"), "Empty species does not render placeholder figures");
        const auto html = text(mixed.output / "report.html");
        check(html.find("lang=\"en\"") != std::string::npos, "English analysis HTML");
        check(html.find("species-electron") != std::string::npos, "Species subsection");
        iaea(root / "one");
        iaea(root / "two");
        iaea(root / "alt", true);
        iaea(root / "unmarked", false, false);
        iaea(root / "constant", false, true, true);
        const auto constant = convert(root / "constant", root / "constant_root");
        auto constOptions = options(constant, root / "constant_report");
        phsp::analyze(constOptions);
        near(read(constOptions.output / "report.json")["source_geometry"]["header_constant_z_cm"], 10);
        phsp::CutOptions cut;
        cut.input = root / "one";
        cut.output = root / "cut";
        cut.x_min_cm = 1;
        cut.x_max_cm = 3;
        cut.logging.progress_seconds = 0;
        phsp::cut(cut);
        auto cutRoot = convert(cut.output, root / "cut_root");
        auto cm = read(root / "cut_root/cut.json");
        check(cm["counts"]["written_records"] == 3, "Cutter retains final accepted record");
        check(cm["phsp"]["total_original_particles"] == 6, "Cutter retains original histories");
        {
            TFile f(cutRoot.c_str(), "READ");
            TTreeReader r("phsp", &f);
            TTreeReaderValue<Long64_t> id(r, "history_id");
            TTreeReaderValue<Float_t> z(r, "pos_z_cm");
            const std::array<int, 3> ids{5, 6, 6};
            int i = 0;
            while (r.Next()) {
                check(*id == ids[i++], "Cutter carries skipped history increments");
                near(*z, 10);
            }
            check(i == 3, "Cut row count");
        }
        cut.output = root / "relocated";
        cut.write_projected = true;
        phsp::cut(cut);
        auto relocated = convert(cut.output, root / "relocated_root");
        {
            TFile f(relocated.c_str(), "READ");
            auto *t = static_cast<TTree *>(f.Get("phsp"));
            Float_t z;
            t->SetBranchAddress("pos_z_cm", &z);
            t->GetEntry(0);
            near(z, 100);
        }
        phsp::MergeOptions merge;
        merge.inputs = {root / "one", root / "two"};
        merge.output = root / "merged";
        merge.logging.progress_seconds = 0;
        phsp::merge(merge);
        auto mergedRoot = convert(merge.output, root / "merged_root");
        auto mm = read(root / "merged_root/merged.json");
        check(mm["counts"]["written_records"] == 14, "Merger retains every record");
        check(mm["phsp"]["total_original_particles"] == 12, "Merged original histories");
        merge.output = root / "partial";
        merge.max_records_per_input = 4;
        phsp::merge(merge);
        auto partial = convert(merge.output, root / "partial_root");
        auto po = options(partial, root / "partial_report");
        phsp::analyze(po);
        auto pr = read(po.output / "report.json");
        check(pr["histories"]["complete_histories"] == 4, "Internal truncated histories excluded");
        check(pr["histories"]["incomplete_or_invalid_histories"] == 2, "Both file-end fragments marked");
        check(pr["normalization"]["available"] == false, "Partial merge normalization disabled");
        check(pr["conversion_metadata"]["processing_source_complete"] == false,
              "Incomplete upstream provenance retained");
        const auto sidecar = fs::path(merge.output.string() + ".processing.json");
        fs::rename(sidecar, sidecar.string() + ".saved");
        rejects([&] { convert(merge.output, root / "missing_provenance"); });
        fs::rename(sidecar.string() + ".saved", sidecar);
        merge.inputs = {root / "one", root / "alt"};
        merge.max_records_per_input = -1;
        merge.output = root / "mismatch";
        rejects([&] { phsp::merge(merge); });
        merge.drop_extras = true;
        merge.output = root / "drop";
        phsp::merge(merge);
        convert(merge.output, root / "drop_root");
        check(read(root / "drop_root/drop.json")["phsp"]["n_extra_int_in_root"] == 1,
              "Drop extras retains increments");
        merge.inputs = {root / "one", root / "unmarked"};
        merge.output = root / "unmarked_merge";
        rejects([&] { phsp::merge(merge); });
        cut.output = cut.input;
        rejects([&] { phsp::cut(cut); });
        json config = {
            {"output_dir", "pipeline"},
            {"threads", 2},
            {"analysis", {{"bins", 16}, {"energy_max_MeV", 8}, {"xy_max_cm", 4}, {"plots", false}}},
            {"preprocessing",
             json::array({{{"name", "joined"}, {"operation", "merge"}, {"inputs", {"one", "two"}}},
                          {{"name", "field"},
                           {"operation", "cut"},
                           {"input", "@joined"},
                           {"x_min_cm", 1},
                           {"x_max_cm", 3}}})},
            {"datasets", json::array({{{"name", "selected"}, {"input", "@field"}}})}};
        {
            std::ofstream out(root / "pipeline.json");
            out << config.dump(2);
        }
        phsp::PipelineOptions pipeline;
        pipeline.config = root / "pipeline.json";
        pipeline.logging.progress_seconds = 0;
        phsp::runPipeline(pipeline);
        const auto manifest = read(root / "pipeline/pipeline_manifest.json");
        check(manifest["preprocessing"]["field"]["status"] == "complete", "Preprocessing pipeline");
        check(read(root / "pipeline/datasets/selected/analysis/report.json")["quality"]["selected_records"] ==
                  6,
              "Prepared dataset record count");
        std::cout << "Geometry, species reports, IAEA cutter/merger and preparation pipeline checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
