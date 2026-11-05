#include "phsp/analysis.hpp"
#include "root_support.hpp"

#include <ROOT/RDFHelpers.hxx>
#include <ROOT/RDataFrame.hxx>
#include <TCanvas.h>
#include <TFile.h>
#include <TGraph.h>
#include <TH1D.h>
#include <TH2D.h>
#include <TLeaf.h>
#include <TMatrixDSym.h>
#include <TMatrixDSymEigen.h>
#include <TObjString.h>
#include <TProfile.h>
#include <TProfile2D.h>
#include <TROOT.h>
#include <TStyle.h>
#include <TTree.h>
#include <TTreeReader.h>
#include <TTreeReaderValue.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <memory>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace phsp {
namespace {
using json = nlohmann::json;
using Node = ROOT::RDF::RNode;
using H1 = ROOT::RDF::RResultPtr<TH1D>;
using H2 = ROOT::RDF::RResultPtr<TH2D>;
using Profile = ROOT::RDF::RResultPtr<TProfile>;
constexpr double pi = 3.14159265358979323846;
constexpr std::array<const char *, 7> species{"all",     "photon", "electron", "positron",
                                              "neutron", "proton", "other"};
constexpr std::array<const char *, 7> speciesLabels{"All particles", "Photons", "Electrons",      "Positrons",
                                                    "Neutrons",      "Protons", "Other particles"};
const std::vector<std::string> particleColumns{"energy_MeV", "weight", "pos_x_cm", "pos_y_cm",
                                               "pos_z_cm",   "dir_u",  "dir_v",    "dir_w"};

json ratio(double a, double b) { return b > 0 ? json(a / b) : json(nullptr); }
void writeText(const std::filesystem::path &path, const std::string &text) {
    std::ofstream out(path);
    if (!out || !(out << text))
        throw std::runtime_error("Cannot write " + path.string());
}
std::string htmlEscape(const std::string &s) {
    std::string out;
    for (char c : s) {
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
std::string display(const json &value) {
    if (value.is_null())
        return "N/A";
    if (value.is_number_float()) {
        std::ostringstream s;
        s << std::setprecision(6) << value.get<double>();
        return s.str();
    }
    return value.dump();
}
bool finiteRecord(float e, float wt, float x, float y, float z, float u, float v, float w) {
    return std::isfinite(e) && std::isfinite(wt) && std::isfinite(x) && std::isfinite(y) &&
           std::isfinite(z) && std::isfinite(u) && std::isfinite(v) && std::isfinite(w) && e >= 0 && wt >= 0;
}
bool validDirection(float u, float v, float w) {
    return std::abs(double(u) * u + double(v) * v + double(w) * w - 1.0) <= 1e-3;
}

// Parallel weighted Welford/Chan moments. No vectors of all particles are retained.
template <size_t N> struct Moments {
    double sw = 0, sw2 = 0;
    std::array<double, N> mean{};
    std::array<std::array<double, N>, N> m2{};
    void add(const std::array<double, N> &x, double w) {
        if (w <= 0)
            return;
        const double total = sw + w;
        std::array<double, N> delta{};
        for (size_t i = 0; i < N; ++i)
            delta[i] = x[i] - mean[i];
        for (size_t i = 0; i < N; ++i)
            mean[i] += delta[i] * w / total;
        for (size_t i = 0; i < N; ++i)
            for (size_t j = 0; j < N; ++j)
                m2[i][j] += delta[i] * delta[j] * sw * w / total;
        sw = total;
        sw2 += w * w;
    }
    void merge(const Moments &b) {
        if (b.sw == 0)
            return;
        if (sw == 0) {
            *this = b;
            return;
        }
        const double total = sw + b.sw;
        std::array<double, N> delta{};
        for (size_t i = 0; i < N; ++i)
            delta[i] = b.mean[i] - mean[i];
        for (size_t i = 0; i < N; ++i)
            for (size_t j = 0; j < N; ++j)
                m2[i][j] += b.m2[i][j] + delta[i] * delta[j] * sw * b.sw / total;
        for (size_t i = 0; i < N; ++i)
            mean[i] += delta[i] * b.sw / total;
        sw = total;
        sw2 += b.sw2;
    }
    double cov(size_t i, size_t j) const { return sw > 0 ? m2[i][j] / sw : 0; }
    json asJson(const std::array<const char *, N> &labels) const {
        json j;
        j["sum_weights"] = sw;
        j["sum_squared_weights"] = sw2;
        j["effective_records"] = ratio(sw * sw, sw2);
        j["columns"] = labels;
        for (size_t i = 0; i < N; ++i) {
            j["mean"][labels[i]] = sw > 0 ? json(mean[i]) : json(nullptr);
            j["rms_spread"][labels[i]] = sw > 0 ? json(std::sqrt(std::max(0., cov(i, i)))) : json(nullptr);
            for (size_t k = 0; k < N; ++k) {
                j["covariance"][i][k] = sw > 0 ? json(cov(i, k)) : json(nullptr);
                j["correlation"][i][k] = ratio(cov(i, k), std::sqrt(std::max(0., cov(i, i) * cov(k, k))));
            }
        }
        return j;
    }
};

struct Stats {
    ULong64_t n = 0, back = 0, tangent = 0, window511 = 0;
    double backWeight = 0, energyWeight = 0;
    Moments<1> unweightedEnergy;
    Moments<7> weighted;
    Moments<4> slopes;
    void add(float e, float wt, double x, double y, double z, float u, float v, float w) {
        ++n;
        if (w < 0) {
            ++back;
            backWeight += wt;
        }
        if (std::abs(w) <= 1e-8)
            ++tangent;
        if (e >= .510 && e <= .512)
            ++window511;
        const double r = std::hypot(x, y), theta = std::acos(std::clamp(double(w), -1., 1.)) * 180 / pi;
        weighted.add({e, x, y, z, r, theta, w}, wt);
        unweightedEnergy.add({e}, 1);
        energyWeight += double(e) * wt;
        if (w > 1e-8)
            slopes.add({x, y, double(u) / w, double(v) / w}, wt);
    }
    void merge(const Stats &b) {
        n += b.n;
        back += b.back;
        tangent += b.tangent;
        window511 += b.window511;
        backWeight += b.backWeight;
        energyWeight += b.energyWeight;
        weighted.merge(b.weighted);
        slopes.merge(b.slopes);
        unweightedEnergy.merge(b.unweightedEnergy);
    }
    json asJson() const {
        auto j = weighted.asJson({"energy_MeV", "x_cm", "y_cm", "z_cm", "radius_cm", "theta_deg", "dir_w"});
        j["records"] = n;
        j["mean_energy_unweighted_MeV"] =
            unweightedEnergy.sw > 0 ? json(unweightedEnergy.mean[0]) : json(nullptr);
        j["sum_weighted_kinetic_energy_MeV"] = energyWeight;
        j["backward_records"] = back;
        j["backward_weight_fraction"] = ratio(backWeight, weighted.sw);
        j["tangential_records"] = tangent;
        j["energy_window_0.510_to_0.512_MeV_records"] = window511;
        auto beam = slopes.asJson({"x_cm", "y_cm", "x_slope", "y_slope"});
        for (size_t a = 0; a < 2; ++a) {
            const double xx = slopes.cov(a, a), pp = slopes.cov(a + 2, a + 2), xp = slopes.cov(a, a + 2);
            const double emittance = std::sqrt(std::max(0., xx * pp - xp * xp));
            const auto axis = a == 0 ? "x" : "y";
            beam[axis]["geometric_rms_emittance_cm"] = slopes.sw > 0 ? json(emittance) : json(nullptr);
            beam[axis]["waist_drift_cm"] = pp > 0 ? json(-xp / pp) : json(nullptr);
            beam[axis]["twiss_beta_cm"] = ratio(xx, emittance);
            beam[axis]["twiss_alpha"] = ratio(-xp, emittance);
            beam[axis]["twiss_gamma_per_cm"] = ratio(pp, emittance);
        }
        j["forward_beam"] = beam;
        return j;
    }
};

struct Histograms {
    std::string name;
    std::vector<H1> one;
    std::vector<H2> two;
    std::vector<Profile> profiles;
    std::optional<ROOT::RDF::RResultPtr<TProfile2D>> mean_energy_xy;
};
Histograms book(Node df, const std::string &name, int bins, double emax, double xymax) {
    Histograms h;
    h.name = name;
    auto one = [&](const char *id, const char *title, const char *col, double lo, double hi,
                   bool weighted = true) {
        ROOT::RDF::TH1DModel model((name + "_" + id).c_str(), title, bins, lo, hi);
        h.one.push_back(weighted ? df.Histo1D(model, col, "weight") : df.Histo1D(model, col));
    };
    auto two = [&](const char *id, const char *title, const char *x, const char *y, double xl, double xh,
                   double yl, double yh) {
        h.two.push_back(
            df.Histo2D({(name + "_" + id).c_str(), title, bins, xl, xh, bins, yl, yh}, x, y, "weight"));
    };
    one("energy", "Weighted energy;Kinetic energy [MeV];Sum of weights", "energy_MeV", 0, emax);
    one("energy_records", "Recorded particle energies;Kinetic energy [MeV];Records", "energy_MeV", 0, emax,
        false);
    one("x", "X profile;x [cm];Sum of weights", "_analysis_x", -xymax, xymax);
    one("y", "Y profile;y [cm];Sum of weights", "_analysis_y", -xymax, xymax);
    one("radius", "Radial crossing distribution;r [cm];Sum of weights", "_radius", 0, std::sqrt(2.) * xymax);
    one("theta", "Polar angle;theta [deg];Sum of weights", "_theta", 0, 180.00001);
    one("phi", "Azimuth;phi [deg];Sum of weights", "_phi", -180.00001, 180.00001);
    two("xy", "Weighted plane crossings;x [cm];y [cm]", "_analysis_x", "_analysis_y", -xymax, xymax, -xymax,
        xymax);
    two("energy_radius", "Energy vs radius;r [cm];Kinetic energy [MeV]", "_radius", "energy_MeV", 0,
        std::sqrt(2.) * xymax, 0, emax);
    two("energy_theta", "Energy vs polar angle;theta [deg];Kinetic energy [MeV]", "_theta", "energy_MeV", 0,
        180.00001, 0, emax);
    two("x_angle", "Position-direction correlation;x [cm];atan2(u,w) [deg]", "_analysis_x", "_angle_x",
        -xymax, xymax, -180.00001, 180.00001);
    two("y_angle", "Position-direction correlation;y [cm];atan2(v,w) [deg]", "_analysis_y", "_angle_y",
        -xymax, xymax, -180.00001, 180.00001);
    h.two.push_back(
        df.Histo2D({(name + "_xy_energy").c_str(), "Weighted kinetic energy crossings;x [cm];y [cm]", bins,
                    -xymax, xymax, bins, -xymax, xymax},
                   "_analysis_x", "_analysis_y", "_energy_weight"));
    h.profiles.push_back(df.Profile1D({(name + "_mean_energy_radius").c_str(),
                                       "Mean energy vs radius;r [cm];Mean kinetic energy [MeV]", bins, 0,
                                       std::sqrt(2.) * xymax},
                                      "_radius", "energy_MeV", "weight"));
    h.profiles.push_back(
        df.Profile1D({(name + "_mean_energy_x").c_str(), "Mean energy vs X;x [cm];Mean kinetic energy [MeV]",
                      bins, -xymax, xymax},
                     "_analysis_x", "energy_MeV", "weight"));
    h.profiles.push_back(
        df.Profile1D({(name + "_mean_energy_y").c_str(), "Mean energy vs Y;y [cm];Mean kinetic energy [MeV]",
                      bins, -xymax, xymax},
                     "_analysis_y", "energy_MeV", "weight"));
    h.mean_energy_xy =
        df.Profile2D({(name + "_mean_energy_xy").c_str(), "Mean kinetic energy [MeV];x [cm];y [cm]", bins,
                      -xymax, xymax, bins, -xymax, xymax},
                     "_analysis_x", "_analysis_y", "energy_MeV", "weight");
    return h;
}

json histogramInfo(const TH1 &h) {
    double inside = 0, outside = 0;
    for (int b = 0; b < h.GetNcells(); ++b) {
        if (h.IsBinUnderflow(b) || h.IsBinOverflow(b))
            outside += h.GetBinContent(b);
        else
            inside += h.GetBinContent(b);
    }
    return {{"entries", h.GetEntries()}, {"in_range_sum", inside}, {"out_of_range_sum", outside}};
}

json spatialMetrics(const TH1D &x, const TH1D &y, const TH1D &radius) {
    json result;
    for (const auto *profile : {&x, &y}) {
        double difference = 0, sum = profile->Integral();
        for (int b = 1; b <= profile->GetNbinsX(); ++b)
            difference +=
                std::abs(profile->GetBinContent(b) - profile->GetBinContent(profile->GetNbinsX() + 1 - b));
        result[profile == &x ? "x_mirror_asymmetry" : "y_mirror_asymmetry"] = ratio(difference, 2 * sum);
    }
    if (radius.Integral() > 0) {
        double probabilities[]{.5, .8, .9, .95, .99}, quantiles[5]{};
        TH1D copy(radius);
        copy.SetDirectory(nullptr);
        copy.GetQuantiles(5, quantiles, probabilities);
        for (size_t i = 0; i < 5; ++i)
            result["containment_radius_cm"][std::to_string(int(probabilities[i] * 100))] = quantiles[i];
    }
    result["scope"] = "Weighted binned in-range crossings only. Containment is centered on x=y=0; mirror "
                      "asymmetry is 0.5*sum|p(x)-p(-x)|. Histogram clipping can bias these metrics.";
    return result;
}

json writeCorrelation(const Stats &stats, const std::string &name) {
    const std::array<const char *, 7> labels{"E", "x", "y", "z", "r", "theta", "w"};
    TH2D h((name + "_correlation").c_str(), "Weighted Pearson correlation;Variable;Variable", 7, 0, 7, 7, 0,
           7);
    std::vector<size_t> active;
    for (size_t i = 0; i < 7; ++i) {
        h.GetXaxis()->SetBinLabel(i + 1, labels[i]);
        h.GetYaxis()->SetBinLabel(i + 1, labels[i]);
        if (stats.weighted.cov(i, i) > 0)
            active.push_back(i);
        for (size_t j = 0; j < 7; ++j) {
            const auto denom = std::sqrt(std::max(0., stats.weighted.cov(i, i) * stats.weighted.cov(j, j)));
            h.SetBinContent(i + 1, j + 1, denom > 0 ? stats.weighted.cov(i, j) / denom : 0);
        }
    }
    h.Write();
    json pca;
    pca["method"] = "Eigenanalysis of weighted correlation matrix (standardized variables). Constant columns "
                    "excluded; descriptive, not an independence test.";
    if (active.empty())
        return pca;
    TMatrixDSym matrix(active.size());
    for (size_t i = 0; i < active.size(); ++i)
        for (size_t j = 0; j < active.size(); ++j)
            matrix(i, j) = h.GetBinContent(active[i] + 1, active[j] + 1);
    TMatrixDSymEigen eigen(matrix);
    const auto values = eigen.GetEigenValues();
    const auto vectors = eigen.GetEigenVectors();
    for (size_t i = 0; i < active.size(); ++i) {
        pca["columns"].push_back(labels[active[i]]);
        pca["eigenvalues"].push_back(values[i]);
        pca["explained_variance_fraction"].push_back(std::max(0., values[i]) / active.size());
        for (size_t j = 0; j < active.size(); ++j)
            pca["eigenvectors_by_column"][i][j] = vectors(i, j);
    }
    return pca;
}

struct Renderer {
    std::filesystem::path dir;
    bool enabled;
    std::vector<std::string> plots;
    std::map<std::string, std::vector<std::string>> plot_groups;
    std::unique_ptr<TCanvas> canvas;
    Renderer(const std::filesystem::path &path, bool active) : dir(path), enabled(active) {
        if (!enabled)
            return;
        std::filesystem::create_directories(dir / "plots");
        canvas = std::make_unique<TCanvas>("phsp_report", "PHSP analysis", 1100, 760);
        canvas->Print(((dir / "report.pdf").string() + "[").c_str());
    }
    std::string label(const std::string &group) const {
        for (size_t i = 0; i < species.size(); ++i)
            if (group == species[i])
                return speciesLabels[i];
        return "Scored histories";
    }
    void save(const char *name, const std::string &group) {
        const std::string file = std::string("plots/") + name + ".svg";
        canvas->SaveAs((dir / file).c_str());
        canvas->Print((dir / "report.pdf").c_str());
        plots.push_back(file);
        plot_groups[group].push_back(file);
    }
    void draw(const TH1 &source, const std::string &group = "histories") {
        if (!enabled || source.GetEntries() == 0)
            return;
        auto copy = std::unique_ptr<TH1>(static_cast<TH1 *>(source.Clone()));
        copy->SetDirectory(nullptr);
        canvas->Clear();
        canvas->SetRightMargin(copy->GetDimension() == 2 ? .17 : .08);
        copy->SetTitle((label(group) + " - " + source.GetTitle()).c_str());
        copy->SetLineColor(kBlue + 2);
        copy->SetLineWidth(2);
        copy->Draw(copy->GetDimension() == 2 ? "COLZ" : "HIST");
        save(copy->GetName(), group);
    }
    void draw(const TGraph &source, const std::string &group) {
        if (!enabled || source.GetN() < 2)
            return;
        TGraph copy(source);
        canvas->Clear();
        canvas->SetRightMargin(.08);
        copy.SetTitle((label(group) + " - " + source.GetTitle()).c_str());
        copy.SetLineColor(kBlue + 2);
        copy.SetLineWidth(2);
        copy.Draw("AL");
        save(copy.GetName(), group);
    }
    ~Renderer() {
        if (canvas)
            canvas->Print(((dir / "report.pdf").string() + "]").c_str());
    }
};

json headerGeometry(const json &meta) {
    const auto text = meta.value("phsp", json::object()).value("iaea_header_text", std::string());
    auto values = [&](const std::string &name) {
        std::vector<std::vector<double>> rows;
        std::istringstream input(text);
        std::string line;
        bool active = false;
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
            std::istringstream numbers(line);
            double v;
            std::vector<double> entry;
            while (numbers >> v)
                entry.push_back(v);
            if (!entry.empty())
                rows.push_back(std::move(entry));
        }
        return rows;
    };
    json result = {{"z_source", "ROOT pos_z_cm (per-record positions)"}};
    const auto contents = values("RECORD_CONTENTS");
    if (contents.size() >= 7) {
        if (contents[2][0] == 0) {
            const auto constants = values("RECORD_CONSTANT");
            const size_t index = (contents[0][0] == 0) + (contents[1][0] == 0);
            result["z_source"] = "IAEA RECORD_CONSTANT, materialized in ROOT pos_z_cm";
            if (constants.size() > index)
                result["header_constant_z_cm"] = constants[index][0];
        } else {
            result["z_source"] = "Per-record Z declared by IAEA RECORD_CONTENTS";
            const auto geometry = values("STATISTICAL_INFORMATION_GEOMETRY");
            const size_t index = (contents[0][0] != 0) + (contents[1][0] != 0);
            if (geometry.size() > index && geometry[index].size() >= 2)
                result["header_z_range_cm"] = {geometry[index][0], geometry[index][1]};
        }
    }
    result["coordinate_origin_inferred"] = false;
    return result;
}

json readMetadata(TFile &file, const std::filesystem::path &input) {
    if (auto *object = dynamic_cast<TObjString *>(file.Get("phsp_metadata")))
        return json::parse(object->GetString().Data());
    auto sidecar = input;
    sidecar.replace_extension(".json");
    if (std::filesystem::exists(sidecar)) {
        std::ifstream in(sidecar);
        json j;
        in >> j;
        return j;
    }
    return json::object();
}

// Histories are deliberately read sequentially: parallel particle callbacks may
// split a history across slots, and old converters did not preserve block order.
json analyzeHistories(TTree &input, TFile &output, const json &meta, const AnalysisOptions &opt,
                      Renderer &render, const Logger &log) {
    json result;
    if (!opt.histories)
        return {{"available", false}, {"reason", "Disabled by --no-histories"}};
    for (const auto *branch : {"history_id", "history_increment", "record_index", "history_start_known"})
        if (!input.GetBranch(branch))
            return {{"available", false},
                    {"reason", "Legacy schema: history_id contains a flag, and original block order is "
                               "unavailable. Reconvert the IAEA source."}};
    if (meta.value("schema_version", 0) < 2)
        return {{"available", false},
                {"reason", "No schema-v2 provenance; history ordering and sampling cannot be verified."}};
    if (meta.value("history", json::object()).value("markers_seen", 0LL) == 0)
        return {{"available", false},
                {"reason", "No positive IAEA history markers were observed; individual histories cannot be "
                           "reconstructed."}};
    if (meta.value("conversion", json::object()).value("stride", 1LL) != 1)
        return {{"available", false},
                {"reason", "Particle stride removes members of histories. Reconvert with --stride 1."}};

    auto *dir = output.mkdir("histories");
    dir->cd();
    TTree histories("histories", "Contiguous scored histories; see complete branch");
    Long64_t id = 0, count = 0, photons = 0, electrons = 0, positrons = 0, neutrons = 0, protons = 0;
    Long64_t first = 0, last = 0;
    Double_t energy = 0, weightedEnergy = 0, sumWeights = 0, maxEnergy = 0;
    Double_t photonEnergy = 0, electronEnergy = 0, positronEnergy = 0, neutronEnergy = 0, protonEnergy = 0;
    Bool_t complete = false;
    histories.Branch("history_id", &id);
    histories.Branch("n_particles", &count);
    histories.Branch("n_photons", &photons);
    histories.Branch("n_electrons", &electrons);
    histories.Branch("n_positrons", &positrons);
    histories.Branch("n_neutrons", &neutrons);
    histories.Branch("n_protons", &protons);
    histories.Branch("first_record_index", &first);
    histories.Branch("last_record_index", &last);
    histories.Branch("sum_energy_MeV", &energy);
    histories.Branch("sum_weighted_energy_MeV", &weightedEnergy);
    histories.Branch("sum_weights", &sumWeights);
    histories.Branch("max_energy_MeV", &maxEnergy);
    histories.Branch("photon_energy_MeV", &photonEnergy);
    histories.Branch("electron_energy_MeV", &electronEnergy);
    histories.Branch("positron_energy_MeV", &positronEnergy);
    histories.Branch("neutron_energy_MeV", &neutronEnergy);
    histories.Branch("proton_energy_MeV", &protonEnergy);
    histories.Branch("complete", &complete);
    // Axis extension keeps history histograms bounded in memory without a prior pass.
    TH1D length("history_length", "Complete scored histories;Particle records per history;Histories", 64, 0,
                64);
    TH1D sumE("history_energy", "Complete scored histories;Sum kinetic energy [MeV];Histories", 120, 0, 12);
    TH1D weightedE("history_weighted_energy", "Complete scored histories;Sum E*w [MeV];Histories", 120, 0,
                   12);
    TH1D maxE("history_max_energy", "Complete scored histories;Maximum particle energy [MeV];Histories", 120,
              0, 12);
    for (auto *h : {&length, &sumE, &weightedE, &maxE})
        h->SetCanExtend(TH1::kXaxis);
    TH2D lengthEnergy("history_length_energy",
                      "Complete scored histories;Particle records;Sum kinetic energy [MeV]", 64, 0, 64, 120,
                      0, 12);
    lengthEnergy.SetCanExtend(TH1::kAllAxes);
    Moments<3> stats;
    Long64_t groups = 0, full = 0, incomplete = 0, skippedEmpty = 0;
    bool have = false, known = false, bad = false;
    auto flush = [&](bool endKnown) {
        if (!have)
            return;
        ++groups;
        complete = known && !bad && endKnown;
        if (histories.Fill() < 0)
            throw std::runtime_error("Cannot write history tree");
        if (complete) {
            ++full;
            stats.add({double(count), energy, weightedEnergy}, 1);
            length.Fill(count);
            sumE.Fill(energy);
            weightedE.Fill(weightedEnergy);
            maxE.Fill(maxEnergy);
            lengthEnergy.Fill(count, energy);
        } else
            ++incomplete;
    };
    TTreeReader reader(&input);
    TTreeReaderValue<Long64_t> rId(reader, "history_id"), rIndex(reader, "record_index");
    TTreeReaderValue<Int_t> rIncrement(reader, "history_increment"), type(reader, "particle_type");
    TTreeReaderValue<Bool_t> rKnown(reader, "history_start_known");
    TTreeReaderValue<Float_t> e(reader, "energy_MeV"), w(reader, "weight");
    std::unique_ptr<TTreeReaderValue<Bool_t>> integrity;
    if (input.GetBranch("history_integrity_known"))
        integrity = std::make_unique<TTreeReaderValue<Bool_t>>(reader, "history_integrity_known");
    Long64_t previousIndex = -1;
    Long64_t expectedId = 0;
    const Long64_t limit =
        opt.max_records < 0 ? input.GetEntries() : std::min(input.GetEntries(), opt.max_records);
    Long64_t read = 0;
    Progress progress(log, opt.logging.progress_seconds);
    progress.start("Aggregate histories", static_cast<std::uint64_t>(limit));
    while (read < limit && reader.Next()) {
        expectedId += *rIncrement;
        if (*rIncrement < 0 || *rId != expectedId)
            throw std::runtime_error("History pass: ID is inconsistent with cumulative IAEA increments");
        if (*rIndex != previousIndex + 1 || (have && *rId < id))
            throw std::runtime_error(
                "History pass: reordered or missing records; cannot reconstruct histories");
        previousIndex = *rIndex;
        if (!have || *rId != id) {
            flush(true);
            if (*rIncrement > 1)
                skippedEmpty += *rIncrement - 1;
            id = *rId;
            first = *rIndex;
            count = photons = electrons = positrons = neutrons = protons = 0;
            energy = weightedEnergy = sumWeights = maxEnergy = 0;
            photonEnergy = electronEnergy = positronEnergy = neutronEnergy = protonEnergy = 0;
            known = *rKnown && *rIncrement > 0;
            bad = false;
            have = true;
        } else if (*rIncrement != 0)
            throw std::runtime_error("History pass: inconsistent history increment");
        ++count;
        if (integrity && !**integrity)
            bad = true;
        last = *rIndex;
        photons += *type == 1;
        electrons += *type == 2;
        positrons += *type == 3;
        neutrons += *type == 4;
        protons += *type == 5;
        if (!std::isfinite(*e) || !std::isfinite(*w) || *e < 0 || *w < 0)
            bad = true;
        else {
            energy += *e;
            weightedEnergy += double(*e) * (*w);
            sumWeights += *w;
            maxEnergy = std::max(maxEnergy, double(*e));
            if (*type == 1)
                photonEnergy += *e;
            else if (*type == 2)
                electronEnergy += *e;
            else if (*type == 3)
                positronEnergy += *e;
            else if (*type == 4)
                neutronEnergy += *e;
            else if (*type == 5)
                protonEnergy += *e;
        }
        ++read;
        progress.advance();
    }
    if (read != limit)
        throw std::runtime_error("History pass: unexpected end of tree");
    bool finalEndKnown =
        meta.value("history", json::object()).value("source_complete", false) && read == input.GetEntries();
    // A --max cut is also complete if the next row begins a new history.
    if (read < input.GetEntries() && reader.Next())
        finalEndKnown = *rId != id && *rIncrement > 0;
    flush(finalEndKnown);
    histories.Write();
    result = stats.asJson({"particles_per_scored_history", "sum_energy_MeV", "sum_weighted_energy_MeV"});
    result["available"] = true;
    result["groups"] = groups;
    result["complete_histories"] = full;
    result["incomplete_or_invalid_histories"] = incomplete;
    result["known_empty_histories_from_increments"] = skippedEmpty;
    result["particle_selection"] = "History analysis uses all particle types before --particle selection.";
    result["length_definition"] =
        "Number of particle records crossing the scoring surface in a history. Not track length.";
    result["energy_definition"] =
        "Kinetic energy recorded at the surface, not energy deposited or conserved primary energy.";
    result["histogram_uncertainty"] = "One fill per complete scored history; unit history weights.";
    const std::array<const char *, 3> historyLabels{"particles_per_scored_history", "sum_energy_MeV",
                                                    "sum_weighted_energy_MeV"};
    for (size_t i = 0; i < 3; ++i)
        result["standard_error_of_mean"][historyLabels[i]] =
            full > 1 ? json(std::sqrt(stats.cov(i, i) / (full - 1))) : json(nullptr);
    for (auto *h : {&length, &sumE, &weightedE, &maxE}) {
        h->Write();
        render.draw(*h);
    }
    lengthEnergy.Write();
    render.draw(lengthEnergy);
    progress.finish();
    log.info("Histories: {} complete, {} partial/invalid, {} known empty", full, incomplete, skippedEmpty);
    output.cd();
    return result;
}

std::string summaryMarkdown(const json &j) {
    std::ostringstream out;
    out << "# PHSP analysis report\n\nInput: `" << j["input"].get<std::string>() << "`\n\n"
        << "Input records: " << j["quality"]["input_records"]
        << ". Accepted at the analysis surface: " << j["quality"]["selected_records"]
        << ".\n\n## Analysis surface\n\n";
    if (j["analysis_plane"]["mode"] == "projected")
        out << "Reference Z: " << display(j["analysis_plane"]["target_z_cm"])
            << " cm (straight-line vacuum projection).\n\n";
    else
        out << "Native scoring surface; no projection.\n\n";
    out << "Source Z offset: " << display(j["analysis_plane"]["source_z_offset_cm"]) << " cm. "
        << j["source_geometry"]["z_source"].get<std::string>() << ".\n\n"
        << "| Particles | Records | Sum of weights | Weighted mean energy [MeV] |\n|---|---:|---:|---:|\n";
    for (size_t i = 0; i < species.size(); ++i) {
        const auto &s = j["species"][species[i]];
        out << "| " << speciesLabels[i] << " | " << s["records"] << " | " << display(s["sum_weights"])
            << " | " << display(s["mean"]["energy_MeV"]) << " |\n";
    }
    out << "\n## Scored histories\n\n";
    const auto &h = j["histories"];
    if (h["available"].get<bool>())
        out << "Complete histories: " << h["complete_histories"]
            << ". Incomplete or invalid: " << h["incomplete_or_invalid_histories"]
            << ".\n\nMean scored records per complete history: "
            << display(h["mean"]["particles_per_scored_history"])
            << ". Mean summed kinetic energy: " << display(h["mean"]["sum_energy_MeV"]) << " MeV.\n\n"
            << "History results use original scoring-surface records. ROOT table: `histories/histories`, "
               "with a `complete` flag.\n";
    else
        out << h["reason"].get<std::string>() << "\n";
    out << "\n## Distributions and correlations\n\n";
    for (size_t i = 0; i < species.size(); ++i) {
        const auto &s = j["species"][species[i]];
        out << "### " << speciesLabels[i] << "\n\nRecords: " << s["records"]
            << "; weighted mean energy: " << display(s["mean"]["energy_MeV"])
            << " MeV; mean radius: " << display(s["mean"]["radius_cm"]) << " cm.\n\n";
    }
    out << "## Quality and interpretation\n\n";
    for (const auto &note : j["notes"])
        out << "- " << note.get<std::string>() << '\n';
    out << "\nFull statistics, covariance/correlation matrices and histogram overflow: `report.json`. "
        << "ROOT objects: `analysis.root`. Plots: `plots/` and `report.pdf` when enabled.\n";
    return out.str();
}
void writeReports(const std::filesystem::path &dir, const json &report, const Renderer &renderer) {
    writeText(dir / "report.json", report.dump(2) + "\n");
    writeText(dir / "report.md", summaryMarkdown(report));
    std::ostringstream out;
    out << "<!doctype html><html lang=\"en\"><meta charset=\"utf-8\"><meta name=\"viewport\" "
           "content=\"width=device-width,initial-scale=1\">"
        << "<title>PHSP analysis report</title><style>body{font:16px system-ui;max-width:1250px;margin:32px "
           "auto;padding:0 20px;background:#f5f7fa;color:#17243a}"
        << "section,details{background:white;padding:24px;border-radius:12px;margin:20px "
           "0}table{border-collapse:collapse;width:100%}th,td{text-align:left;padding:10px;border-bottom:1px "
           "solid #e5e9ef}"
        << "pre{white-space:pre-wrap}.gallery{display:grid;grid-template-columns:repeat(auto-fit,minmax("
           "330px,1fr));gap:16px}figure{margin:0}img{width:100%}a{color:#145bbc}nav "
           "a{margin-right:16px}summary{cursor:pointer}</style>"
        << "<h1>PHSP analysis report</h1><p><a href=\"analysis.root\">ROOT</a> · <a "
           "href=\"report.json\">JSON</a> · <a href=\"report.md\">Markdown</a>";
    if (renderer.enabled)
        out << " · <a href=\"report.pdf\">PDF</a>";
    out << "</p><section><p>Input: <code>" << htmlEscape(report["input"].get<std::string>()) << "</code></p>"
        << "<p>Input records: <strong>" << report["quality"]["input_records"]
        << "</strong> · Accepted at the analysis surface: <strong>" << report["quality"]["selected_records"]
        << "</strong></p><h2>Analysis surface</h2>";
    if (report["analysis_plane"]["mode"] == "projected")
        out << "<p>Reference Z: <strong>" << display(report["analysis_plane"]["target_z_cm"])
            << " cm</strong>. Forward-time straight-line vacuum projection, applied to all particle "
               "distributions and moments.</p>";
    else
        out << "<p>Native scoring surface; projection disabled.</p>";
    out << "<p>Source Z: " << htmlEscape(report["source_geometry"]["z_source"].get<std::string>())
        << ". Origin offset: " << display(report["analysis_plane"]["source_z_offset_cm"]) << " cm.</p>";
    if (report["source_geometry"].contains("observed_z_min_cm"))
        out << "<p>Observed original source Z range: "
            << display(report["source_geometry"]["observed_z_min_cm"]) << " to "
            << display(report["source_geometry"]["observed_z_max_cm"]) << " cm.</p>";
    out << "<table><tr><th>Particles</th><th>Records</th><th>Sum of weights</th><th>Weighted mean E "
           "[MeV]</th></tr>";
    for (size_t i = 0; i < species.size(); ++i) {
        const auto &s = report["species"][species[i]];
        out << "<tr><td>" << speciesLabels[i] << "</td><td>" << s["records"] << "</td><td>"
            << display(s["sum_weights"]) << "</td><td>" << display(s["mean"]["energy_MeV"]) << "</td></tr>";
    }
    out << "</table><h2>Scored histories</h2>";
    const auto &h = report["histories"];
    if (h["available"].get<bool>())
        out << "<p>Complete: <strong>" << h["complete_histories"]
            << "</strong> · Incomplete or invalid: " << h["incomplete_or_invalid_histories"]
            << "</p><p>Mean records per complete history: "
            << display(h["mean"]["particles_per_scored_history"])
            << "; mean summed E: " << display(h["mean"]["sum_energy_MeV"])
            << " MeV. Histories refer to the original scoring surface.</p>";
    else
        out << "<p>" << htmlEscape(h["reason"].get<std::string>()) << "</p>";
    out << "</section><details><summary>Definitions, quality and limitations</summary><ul>";
    for (const auto &note : report["notes"])
        out << "<li>" << htmlEscape(note.get<std::string>()) << "</li>";
    out << "</ul></details><h2>Distributions and correlations</h2><nav>";
    for (size_t i = 0; i < species.size(); ++i)
        out << "<a href=\"#species-" << species[i] << "\">" << speciesLabels[i] << "</a>";
    out << "</nav>";
    auto gallery = [&](const std::string &group) {
        const auto it = renderer.plot_groups.find(group);
        if (it == renderer.plot_groups.end()) {
            out << "<p>No plots: this group is empty or plot rendering is disabled. Numerical results remain "
                   "in JSON and ROOT.</p>";
            return;
        }
        out << "<div class=\"gallery\">";
        for (const auto &plot : it->second)
            out << "<figure><a href=\"" << plot << "\"><img loading=\"lazy\" src=\"" << plot << "\" alt=\""
                << plot << "\"></a></figure>";
        out << "</div>";
    };
    for (size_t i = 0; i < species.size(); ++i) {
        const auto &s = report["species"][species[i]];
        out << "<section id=\"species-" << species[i] << "\"><h3>" << speciesLabels[i]
            << "</h3><p>Records: " << s["records"] << "; sum of weights: " << display(s["sum_weights"])
            << "; weighted mean E: " << display(s["mean"]["energy_MeV"]) << " MeV.</p>";
        gallery(species[i]);
        out << "</section>";
    }
    out << "<section><h2>History distributions</h2>";
    gallery("histories");
    out << "</section>"
        << "<details><summary>Full statistics and matrices</summary><pre>" << htmlEscape(report.dump(2))
        << "</pre></details></html>";
    writeText(dir / "report.html", out.str());
}
} // namespace

std::filesystem::path analyze(const AnalysisOptions &opt) {
    if (opt.threads == 0 || opt.bins < 8 || opt.bins > 1000 || opt.max_records < -1 || opt.particle < 0 ||
        opt.particle > 5)
        throw std::invalid_argument("Invalid analysis threads, bin count, record limit or particle code");
    for (const auto &range : {opt.energy_max, opt.xy_max})
        if (range && (!std::isfinite(*range) || *range <= 0))
            throw std::invalid_argument("Histogram ranges must be finite and positive");
    if (!std::isfinite(opt.source_z_offset_cm))
        throw std::invalid_argument("Source Z offset must be finite");
    if (opt.project_z && !std::isfinite(*opt.project_z))
        throw std::invalid_argument("Projection Z must be finite");
    const auto inputPath = std::filesystem::absolute(opt.input);
    const auto outPath = opt.output.empty()
                             ? inputPath.parent_path() / (inputPath.stem().string() + "_analysis")
                             : std::filesystem::absolute(opt.output);
    if (std::filesystem::weakly_canonical(outPath / "analysis.root") ==
        std::filesystem::weakly_canonical(inputPath))
        throw std::runtime_error("Output would overwrite the input ROOT file");
    Logger log("analyze", opt.logging, outPath / "analysis.log");
    try {
        RootMessages rootMessages;
        Progress progress(log, opt.logging.progress_seconds);
        progress.start("Open ROOT and initialize RDataFrame");
        log.info("Input: {} | threads={} | plots={} | histories={}", inputPath.string(), opt.threads,
                 opt.plots, opt.histories);
        TFile input(inputPath.c_str(), "READ");
        if (input.IsZombie())
            throw std::runtime_error("Cannot open input ROOT file");
        auto *tree = dynamic_cast<TTree *>(input.Get("phsp"));
        if (!tree)
            throw std::runtime_error("Missing TTree 'phsp'");
        for (const auto &col : particleColumns) {
            const auto *leaf = tree->GetLeaf(col.c_str());
            if (!leaf || std::string(leaf->GetTypeName()) != "Float_t")
                throw std::runtime_error("Missing Float_t column: " + col);
        }
        if (!tree->GetBranch("particle_type"))
            throw std::runtime_error("Missing particle_type");
        const auto meta = readMetadata(input, inputPath);
        std::filesystem::create_directories(outPath);
        gROOT->SetBatch(true);
        TH1::SetDefaultSumw2(true);
        gStyle->SetOptStat(0);
        if (ROOT::IsImplicitMTEnabled())
            ROOT::DisableImplicitMT();
        if (opt.threads > 1)
            ROOT::EnableImplicitMT(opt.threads);
        ROOT::RDF::Experimental::RDatasetSpec dataset;
        dataset.AddSample({"phsp", "phsp", inputPath.string()});
        // Restrict I/O at the dataset level: Filter(rdfentry_<N) alone still scans
        // every entry of a multi-gigabyte file, even for a tiny preview.
        if (opt.max_records > 0)
            dataset.WithGlobalRange({0, opt.max_records});
        ROOT::RDataFrame df(std::move(dataset));
        progress.finish();
        const auto planned =
            opt.max_records < 0 ? tree->GetEntries() : std::min(tree->GetEntries(), opt.max_records);
        progress.start("Scan quality and histogram ranges", static_cast<std::uint64_t>(planned),
                       df.GetNSlots());
        Node raw = df;
        if (opt.max_records >= 0)
            raw = raw.Filter([n = opt.max_records](ULong64_t i) { return i < static_cast<ULong64_t>(n); },
                             {"rdfentry_"}, "input prefix");
        raw = raw.DefineSlot("_phsp_progress",
                             [&progress](unsigned slot) {
                                 progress.advance(slot);
                                 return true;
                             })
                  .Filter([](bool seen) { return seen; }, {"_phsp_progress"});
        auto total = raw.Count();
        auto numeric =
            raw.Filter(finiteRecord, particleColumns, "finite values, nonnegative kinetic energy and weight");
        auto numericCount = numeric.Count();
        auto good =
            numeric.Filter(validDirection, {"dir_u", "dir_v", "dir_w"}, "unit direction (tolerance 1e-3)");
        auto goodCount = good.Count();
        Node selected = good;
        if (opt.particle)
            selected = selected.Filter([p = opt.particle](int type) { return type == p; }, {"particle_type"},
                                       "particle selection");
        auto sourceCount = selected.Count();
        auto sourceMinZ = selected.Min<float>("pos_z_cm"), sourceMaxZ = selected.Max<float>("pos_z_cm");
        const auto target = opt.project_z;
        const double offset = opt.source_z_offset_cm;
        Node atPlane = selected;
        if (target)
            atPlane = atPlane.Filter(
                [target, offset](float z, float w) {
                    return std::abs(w) > 1e-8 && (*target - (double(z) + offset)) / w >= 0;
                },
                {"pos_z_cm", "dir_w"}, "ray reaches reference plane");
        atPlane =
            atPlane
                .Define("_analysis_x",
                        [target, offset](float x, float z, float u, float w) {
                            return target ? double(x) + (*target - (double(z) + offset)) * u / w : double(x);
                        },
                        {"pos_x_cm", "pos_z_cm", "dir_u", "dir_w"})
                .Define("_analysis_y",
                        [target, offset](float y, float z, float v, float w) {
                            return target ? double(y) + (*target - (double(z) + offset)) * v / w : double(y);
                        },
                        {"pos_y_cm", "pos_z_cm", "dir_v", "dir_w"})
                .Define("_analysis_z",
                        [target, offset](float z) { return target.value_or(double(z) + offset); },
                        {"pos_z_cm"})
                .Filter([](double x, double y,
                           double z) { return std::isfinite(x) && std::isfinite(y) && std::isfinite(z); },
                        {"_analysis_x", "_analysis_y", "_analysis_z"}, "finite reference coordinates");
        auto selectedCount = atPlane.Count();
        auto minX = atPlane.Min<double>("_analysis_x"), maxX = atPlane.Max<double>("_analysis_x");
        auto minY = atPlane.Min<double>("_analysis_y"), maxY = atPlane.Max<double>("_analysis_y");
        auto minZ = atPlane.Min<double>("_analysis_z"), maxZ = atPlane.Max<double>("_analysis_z");
        auto maxE = atPlane.Max<float>("energy_MeV"), minW = atPlane.Min<float>("weight"),
             maxW = atPlane.Max<float>("weight");
        const auto selectedN = selectedCount.GetValue(); // Executes all scout actions in one pass.
        progress.finish();
        log.info("Quality: {} input | {} selected | {} invalid numeric/weight | {} invalid directions",
                 *total, selectedN, *total - *numericCount, *numericCount - *goodCount);
        double emax = opt.energy_max.value_or(selectedN ? std::max(.001, double(*maxE) * 1.00001) : 1.);
        double xymax = opt.xy_max.value_or(
            selectedN ? std::max(.001, std::max({std::abs(double(*minX)), std::abs(double(*maxX)),
                                                 std::abs(double(*minY)), std::abs(double(*maxY))}) *
                                           1.00001)
                      : 1.);
        log.info("Histograms: {} bins | E=[0,{}] MeV | X/Y=[-{},{}] cm", opt.bins, emax, xymax, xymax);
        progress.start("Fill particle distributions", static_cast<std::uint64_t>(planned), df.GetNSlots());

        Node derived =
            atPlane
                .Define("_radius", [](double x, double y) { return std::hypot(x, y); },
                        {"_analysis_x", "_analysis_y"})
                .Define("_theta",
                        [](float w) { return std::acos(std::clamp(double(w), -1., 1.)) * 180 / pi; },
                        {"dir_w"})
                .Define("_phi", [](float u, float v) { return std::atan2(double(v), double(u)) * 180 / pi; },
                        {"dir_u", "dir_v"})
                .Define("_angle_x",
                        [](float u, float w) { return std::atan2(double(u), double(w)) * 180 / pi; },
                        {"dir_u", "dir_w"})
                .Define("_angle_y",
                        [](float v, float w) { return std::atan2(double(v), double(w)) * 180 / pi; },
                        {"dir_v", "dir_w"})
                .Define("_energy_weight", [](float e, float w) { return double(e) * w; },
                        {"energy_MeV", "weight"});
        std::vector<Histograms> hist;
        for (int i = 0; i < 7; ++i) {
            Node group = derived;
            if (i > 0)
                group =
                    group.Filter([i](int p) { return i == 6 ? p < 1 || p > 5 : p == i; }, {"particle_type"});
            hist.push_back(book(group, species[i], opt.bins, emax, xymax));
        }
        auto weightHistogram = derived.Histo1D({"all_weights", "Statistical weights;Weight;Records", opt.bins,
                                                selectedN ? std::min(0., double(*minW)) : 0.,
                                                selectedN ? std::max(1., double(*maxW) * 1.00001) : 1.},
                                               "weight");
        std::optional<H2> orderEnergy;
        if (tree->GetBranch("history_particle_index"))
            orderEnergy = derived.Histo2D({"all_history_particle_energy",
                                           "Particle energies by index (includes partial histories);Index in "
                                           "scored history;Kinetic energy [MeV]",
                                           64, 0, 64, opt.bins, 0, emax},
                                          "history_particle_index", "energy_MeV", "weight");
        std::vector<std::array<Stats, 7>> slots(df.GetNSlots());
        auto columns = particleColumns;
        columns[2] = "_analysis_x";
        columns[3] = "_analysis_y";
        columns[4] = "_analysis_z";
        columns.insert(columns.begin(), "particle_type");
        derived.ForeachSlot(
            [&](unsigned slot, int type, float e, float wt, double x, double y, double z, float u, float v,
                float w) {
                slots[slot][0].add(e, wt, x, y, z, u, v, w);
                slots[slot][type >= 1 && type <= 5 ? type : 6].add(e, wt, x, y, z, u, v, w);
            },
            columns); // Fills all booked histograms and accumulators in the same pass.
        progress.finish();
        std::array<Stats, 7> stats;
        for (const auto &slot : slots)
            for (size_t i = 0; i < 7; ++i)
                stats[i].merge(slot[i]);

        json report;
        report["report_schema_version"] = 2;
        report["input"] = inputPath.string();
        report["root_version"] = gROOT->GetVersion();
        report["conversion_metadata"] = meta;
        report["options"] = {{"threads", opt.threads},   {"max_records", opt.max_records},
                             {"particle", opt.particle}, {"bins", opt.bins},
                             {"energy_max_MeV", emax},   {"xy_max_cm", xymax}};
        report["quality"] = {{"tree_records", tree->GetEntries()},
                             {"input_records", *total},
                             {"invalid_numeric_energy_or_weight_records", *total - *numericCount},
                             {"nonunit_direction_records", *numericCount - *goodCount},
                             {"valid_before_particle_selection", *goodCount},
                             {"selected_records", selectedN}};
        report["notes"] = json::array(
            {"Histograms use statistical particle weights; energy_records counts stored records. Means and "
             "covariances use nonnegative weights. Invalid numeric values, negative energy/weights and "
             "nonunit directions are excluded.",
             "Particle histogram Sumw2 errors and effective_records assume independent records. Correlated "
             "particles in a history and variance reduction can invalidate that uncertainty interpretation.",
             "History length is the number of scored particle records, not track length or number of all "
             "particles produced. Energy sums are surface kinetic energies, not deposited dose or an energy "
             "conservation test.",
             "XY crossing maps use sum(w) per bin; xy_energy uses sum(E*w). No implicit 1/abs(w) "
             "track-fluence correction is applied.",
             "Beam emittance and Twiss parameters use forward particles with w>1e-8 and slopes u/w,v/w. They "
             "describe a beam plane only when Z is constant; waist estimates assume free straight-line "
             "drift.",
             "The 0.510-0.512 MeV window is an energy selection only; it does not establish annihilation "
             "provenance.",
             "Unrecorded empty primary histories cannot be individually reconstructed without history "
             "increments. Header primary count is used for normalization only for a full unsampled "
             "conversion."});
        if (!meta.value("input_integrity", json::object()).value("header_consistent", true))
            report["notes"].push_back(
                "The source size or record count disagrees with its IAEA header. Only available complete "
                "records were converted. Original-history normalization is disabled and the final scored "
                "history is treated as incomplete.");
        if (meta.contains("processing_provenance") && !meta.value("processing_source_complete", true))
            report["notes"].push_back(
                "Upstream cut/merge provenance marks the source as incomplete. Full-source normalization "
                "is disabled; groups flagged by processing provenance are excluded from complete-history "
                "statistics, including groups at internal merge boundaries.");
        if (selectedN && double(*maxZ) - *minZ > 1e-4)
            report["notes"].push_back(
                "Z varies across records: XY maps combine scoring surfaces; emittance/waist numbers are "
                "descriptive and not a single-plane measurement.");
        report["geometry"] = selectedN ? json{{"z_min_cm", *minZ}, {"z_max_cm", *maxZ}} : json::object();
        report["analysis_plane"] = {
            {"mode", opt.project_z ? "projected" : "native"},
            {"target_z_cm", opt.project_z ? json(*opt.project_z) : json(nullptr)},
            {"source_z_offset_cm", opt.source_z_offset_cm},
            {"coordinate_convention",
             "Shared +Z frame; default Z=100 cm assumes the target at Z=0. Coordinate origins are not "
             "inferred from an IAEA filename or nominal SSD."},
            {"transport",
             "Straight-line vacuum drift, without scattering, absorption, collimators or energy loss."}};
        report["source_geometry"] = headerGeometry(meta);
        if (*sourceCount > 0) {
            report["source_geometry"]["observed_z_min_cm"] = *sourceMinZ;
            report["source_geometry"]["observed_z_max_cm"] = *sourceMaxZ;
        }
        report["quality"]["source_selected_records"] = *sourceCount;
        report["quality"]["projection_rejected_records"] = *sourceCount - selectedN;
        if (opt.project_z) {
            log.info("Reference plane Z={} cm | source Z offset={} cm | {} reachable / {} source records",
                     *opt.project_z, opt.source_z_offset_cm, selectedN, *sourceCount);
            report["notes"].push_back(
                "Particle histograms, moments, profiles, PCA and beam optics use only rays that reach the "
                "reference plane in their forward time direction. History statistics remain on the original "
                "scored records and are not regrouped by projection.");
            report["notes"].push_back(
                "The default 100 cm plane is a conventional linac reference under target-centred +Z "
                "coordinates, not an isocentre inferred from the header. Use source_z_offset_cm for a known "
                "origin shift or choose an explicit target. Vacuum drift does not model air, jaws, MLCs, "
                "matter or charged-particle scattering.");
            if (selectedN == 0 && *sourceCount > 0)
                log.warning("No selected rays reach the reference plane. Check Z, beam direction and "
                            "source_z_offset_cm.");
        }

        for (size_t i = 0; i < 7; ++i) {
            report["species"][species[i]] = stats[i].asJson();
            report["species"][species[i]]["weighted_particle_fraction"] =
                ratio(stats[i].weighted.sw, stats[0].weighted.sw);
            report["species"][species[i]]["weighted_kinetic_energy_fraction"] =
                ratio(stats[i].energyWeight, stats[0].energyWeight);
        }
        const bool fullInput = opt.max_records < 0 || opt.max_records >= tree->GetEntries();
        const bool normalized = meta.value("schema_version", 0) >= 2 && fullInput &&
                                meta.value("history", json::object()).value("source_complete", false) &&
                                meta.value("conversion", json::object()).value("stride", 1LL) == 1;
        const double primaryCount = meta.value("phsp", json::object()).value("total_original_particles", 0.);
        report["normalization"]["available"] = normalized && primaryCount > 0;
        if (normalized && primaryCount > 0) {
            report["normalization"]["original_histories"] = primaryCount;
            report["normalization"]["weighted_crossings_per_original_history"] =
                stats[0].weighted.sw / primaryCount;
            report["normalization"]["recorded_particle_records_per_original_history"] =
                stats[0].n / primaryCount;
            report["normalization"]["weighted_kinetic_energy_MeV_per_original_history"] =
                stats[0].energyWeight / primaryCount;
        }
        TFile output((outPath / "analysis.root").c_str(), "RECREATE");
        if (output.IsZombie())
            throw std::runtime_error("Cannot create analysis.root");
        Renderer renderer(outPath, opt.plots);
        progress.start("Write particle results and render plots", hist.size());
        for (auto &group : hist) {
            log.debug("Write {} histogram group", group.name);
            auto *dir = output.mkdir(group.name.c_str());
            dir->cd();
            const auto speciesIndex = static_cast<size_t>(&group - hist.data());
            report["species"][group.name]["spatial"] =
                spatialMetrics(*group.one[2], *group.one[3], *group.one[4]);
            report["species"][group.name]["pca"] = writeCorrelation(stats[speciesIndex], group.name);
            if (stats[speciesIndex].n > 0) {
                auto *correlation = dynamic_cast<TH2D *>(dir->Get((group.name + "_correlation").c_str()));
                if (correlation)
                    renderer.draw(*correlation, group.name);
            }
            for (auto &ptr : group.one) {
                auto &h = ptr.GetValue();
                h.Write();
                report["histograms"][h.GetName()] = histogramInfo(h);
                renderer.draw(h, group.name);
            }
            for (auto &ptr : group.two) {
                auto &h = ptr.GetValue();
                h.Write();
                report["histograms"][h.GetName()] = histogramInfo(h);
                renderer.draw(h, group.name);
            }
            // Divide radial counts by the exact annulus area, never by its bin width alone.
            auto radial = std::unique_ptr<TH1D>(
                static_cast<TH1D *>(group.one[4]->Clone((group.name + "_radial_crossing_density").c_str())));
            radial->SetDirectory(nullptr);
            radial->SetTitle(
                "Azimuth-averaged crossing density;r [cm];Sum of weights / annulus area [cm^{-2}]");
            for (int b = 1; b <= radial->GetNbinsX(); ++b) {
                const double lo = radial->GetXaxis()->GetBinLowEdge(b),
                             hi = radial->GetXaxis()->GetBinUpEdge(b);
                const double area = pi * (hi * hi - lo * lo);
                radial->SetBinContent(b, radial->GetBinContent(b) / area);
                radial->SetBinError(b, radial->GetBinError(b) / area);
            }
            radial->SetEntries(group.one[4]->GetEntries());
            radial->Write();
            renderer.draw(*radial, group.name);
            TGraph cumulative;
            cumulative.SetName((group.name + "_radial_cumulative").c_str());
            cumulative.SetTitle("Weighted radial containment;r [cm];In-range cumulative fraction");
            double accumulated = 0, totalWeight = group.one[4]->Integral();
            cumulative.SetPoint(0, 0, 0);
            for (int b = 1; b <= group.one[4]->GetNbinsX(); ++b) {
                accumulated += group.one[4]->GetBinContent(b);
                cumulative.SetPoint(b, group.one[4]->GetXaxis()->GetBinUpEdge(b),
                                    totalWeight > 0 ? accumulated / totalWeight : 0);
            }
            cumulative.Write();
            if (stats[speciesIndex].n > 0)
                renderer.draw(cumulative, group.name);
            for (auto &ptr : group.profiles) {
                auto &h = ptr.GetValue();
                h.Write();
                renderer.draw(h, group.name);
            }
            group.mean_energy_xy->GetValue().Write();
            renderer.draw(group.mean_energy_xy->GetValue(), group.name);
            progress.advance();
        }
        output.cd();
        weightHistogram->Write();
        renderer.draw(*weightHistogram, "all");
        if (orderEnergy) {
            auto &h = orderEnergy->GetValue();
            h.Write();
            renderer.draw(h, "all");
            report["histograms"][h.GetName()] = histogramInfo(h);
        }
        if (opt.project_z) {
            auto &h = hist[0].two[0].GetValue();
            h.Write("projected_xy");
            report["projection"] = {{"target_z_cm", *opt.project_z},
                                    {"records", selectedN},
                                    {"source_records", *sourceCount},
                                    {"rejected_records", *sourceCount - selectedN},
                                    {"source_z_offset_cm", opt.source_z_offset_cm},
                                    {"applied_to_all_particle_analyses", true},
                                    {"histogram", histogramInfo(h)},
                                    {"model", "Straight-line drift without interactions; target must lie "
                                              "ahead along particle direction."}};
        }
        progress.finish();
        report["histories"] = analyzeHistories(*tree, output, meta, opt, renderer, log);
        if (!report["histories"]["available"].get<bool>())
            log.warning("History analysis unavailable: {}", report["histories"]["reason"].get<std::string>());
        progress.start("Write HTML, JSON, Markdown and RDF graph", 1);
        report["plots"] = renderer.plots;
        report["plot_groups"] = renderer.plot_groups;
        TObjString reportObject(report.dump().c_str());
        output.cd();
        reportObject.Write("analysis_report");
        if (output.TestBit(TFile::kWriteError))
            throw std::runtime_error("ROOT output write error");
        ROOT::RDF::SaveGraph(df, (outPath / "dataframe.dot").string());
        writeReports(outPath, report, renderer);
        progress.advance();
        progress.finish();
        log.info("Finished: {} records | {} plots | report: {}", selectedN, renderer.plots.size(),
                 (outPath / "report.html").string());
        return outPath;
    } catch (const std::exception &error) {
        log.error("Analysis failed: {}", error.what());
        throw;
    }
}
} // namespace phsp
