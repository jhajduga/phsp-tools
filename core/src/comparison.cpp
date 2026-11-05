#include "phsp/comparison.hpp"
#include "root_support.hpp"
#include <TCanvas.h>
#include <TFile.h>
#include <TH1.h>
#include <TH2.h>
#include <TLegend.h>
#include <TObjString.h>
#include <TROOT.h>
#include <TStyle.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;
namespace {
bool sameAxis(const TAxis &a, const TAxis &b) {
    if (a.GetNbins() != b.GetNbins())
        return false;
    for (int i = 1; i <= a.GetNbins() + 1; ++i)
        if (a.GetBinLowEdge(i) != b.GetBinLowEdge(i))
            return false;
    return true;
}
double total(const TH1 &h) {
    double sum = 0;
    for (int b = 0; b < h.GetNcells(); ++b)
        sum += h.GetBinContent(b);
    return sum;
}
std::unique_ptr<TH1> copy(const TH1 &h, const std::string &name) {
    std::unique_ptr<TH1> out(static_cast<TH1 *>(h.Clone(name.c_str())));
    out->SetDirectory(nullptr);
    return out;
}
json distances(const TH1 &a, const TH1 &b) {
    json j;
    double tv = 0, js = 0, cdfA = 0, cdfB = 0, wasserstein = 0;
    for (int i = 0; i < a.GetNcells(); ++i) {
        const double p = a.GetBinContent(i), q = b.GetBinContent(i), m = .5 * (p + q);
        tv += .5 * std::abs(p - q);
        if (p > 0)
            js += .5 * p * std::log(p / m);
        if (q > 0)
            js += .5 * q * std::log(q / m);
    }
    j["total_variation"] = tv;
    j["jensen_shannon_divergence_nats"] = js;
    if (a.GetDimension() == 1 && a.GetBinContent(0) == 0 && b.GetBinContent(0) == 0 &&
        a.GetBinContent(a.GetNbinsX() + 1) == 0 && b.GetBinContent(b.GetNbinsX() + 1) == 0) {
        for (int i = 1; i < a.GetNbinsX(); ++i) {
            cdfA += a.GetBinContent(i);
            cdfB += b.GetBinContent(i);
            wasserstein += std::abs(cdfA - cdfB) * (a.GetBinCenter(i + 1) - a.GetBinCenter(i));
        }
        j["wasserstein_bin_centers_axis_units"] = wasserstein;
    }
    return j;
}
void write(const fs::path &path, const std::string &text) {
    std::ofstream f(path);
    if (!f || !(f << text))
        throw std::runtime_error("Cannot write " + path.string());
}
} // namespace
std::filesystem::path phsp::compare(const ComparisonOptions &opt) {
    if (opt.output.empty() || opt.reference.empty() || opt.candidate.empty())
        throw std::invalid_argument("Comparison reference, candidate and output are required");
    const auto dir = fs::absolute(opt.output);
    const auto referencePath = fs::absolute(opt.reference), candidatePath = fs::absolute(opt.candidate);
    Logger log("compare", opt.logging, dir / "comparison.log");
    try {
        RootMessages rootMessages;
        Progress progress(log, opt.logging.progress_seconds);
        log.info("Reference: {}", referencePath.string());
        log.info("Candidate: {}", candidatePath.string());
        const bool plots = opt.plots;
        for (int i = 1; i <= 2; ++i)
            if (fs::weakly_canonical(dir / "comparison.root") ==
                fs::weakly_canonical(i == 1 ? referencePath : candidatePath))
                throw std::runtime_error("Output would overwrite an input");
        TFile a(referencePath.c_str(), "READ"), b(candidatePath.c_str(), "READ");
        if (a.IsZombie() || b.IsZombie())
            throw std::runtime_error("Cannot open comparison input");
        if (!a.Get("analysis_report") || !b.Get("analysis_report"))
            throw std::runtime_error("Inputs must be analysis.root files from phspAnalyze");
        const auto metadataA =
            json::parse(static_cast<TObjString *>(a.Get("analysis_report"))->GetString().Data());
        const auto metadataB =
            json::parse(static_cast<TObjString *>(b.Get("analysis_report"))->GetString().Data());
        if (!metadataA.contains("analysis_plane") || !metadataB.contains("analysis_plane"))
            throw std::runtime_error(
                "Missing analysis-plane metadata. Re-run both analyses with the current version.");
        const auto &planeA = metadataA["analysis_plane"];
        const auto &planeB = metadataB["analysis_plane"];
        if (planeA["mode"] != planeB["mode"] || planeA["target_z_cm"] != planeB["target_z_cm"])
            throw std::runtime_error(
                "Different analysis planes. Re-run both analyses at the same --project-z.");
        if (planeA["mode"] == "native" && metadataA["geometry"].contains("z_min_cm") &&
            metadataB["geometry"].contains("z_min_cm")) {
            for (const auto *key : {"z_min_cm", "z_max_cm"})
                if (std::abs(metadataA["geometry"][key].get<double>() -
                             metadataB["geometry"][key].get<double>()) > 1e-4)
                    throw std::runtime_error(
                        "Native source Z ranges differ. Project both sources to a common reference plane.");
        }
        const std::vector<std::string> groups{"all",     "photon", "electron", "positron",
                                              "neutron", "proton", "other"};
        const std::vector<std::string> variables{
            "energy",        "energy_records", "x",       "y",       "radius",   "theta", "phi", "xy",
            "energy_radius", "energy_theta",   "x_angle", "y_angle", "xy_energy"};
        progress.start("Validate comparison histogram bins", groups.size() * variables.size());
        // Validate all bin edges before creating output, so a range mismatch gives
        // one actionable error, not a partly comparable report.
        for (const auto &g : groups)
            for (const auto &v : variables) {
                const auto path = g + "/" + g + "_" + v;
                const auto *hA = dynamic_cast<TH1 *>(a.Get(path.c_str()));
                const auto *hB = dynamic_cast<TH1 *>(b.Get(path.c_str()));
                if (!hA || !hB)
                    throw std::runtime_error("Missing histogram " + path);
                if (hA->GetDimension() != hB->GetDimension() || !sameAxis(*hA->GetXaxis(), *hB->GetXaxis()) ||
                    (hA->GetDimension() > 1 && !sameAxis(*hA->GetYaxis(), *hB->GetYaxis())))
                    throw std::runtime_error(
                        "Different bins in " + path +
                        ". Re-run analyses with identical --bins, --energy-max and --xy-max.");
                progress.advance();
            }
        progress.finish();
        fs::create_directories(dir / "plots");
        TFile output((dir / "comparison.root").c_str(), "RECREATE");
        if (output.IsZombie())
            throw std::runtime_error("Cannot create comparison.root");
        gROOT->SetBatch(true);
        gStyle->SetOptStat(0);
        TCanvas canvas("comparison", "PHSP comparison", 1100, 760);
        json report;
        report["reference"] = referencePath.string();
        report["candidate"] = candidatePath.string();
        report["analysis_plane"] = planeA;
        report["source_geometry"] = {{"reference", metadataA.value("source_geometry", json::object())},
                                     {"candidate", metadataB.value("source_geometry", json::object())}};
        report["plot_groups"] = json::object();
        report["notes"] = {
            "Shape distributions are normalized by the total weight including underflow and overflow. Raw "
            "sums are retained separately.",
            "Distances describe binned distributions. They are not p-values or clinical QA criteria; "
            "particle correlations and variance reduction require a history-aware uncertainty model.",
            "Difference is candidate minus reference. Ratio is candidate/reference; denominator-zero bins "
            "are zero and flagged in the ratio_undefined mask.",
            "Check matching scoring surfaces, coordinate systems, particle selections, units and "
            "normalization in both analysis reports before interpreting differences."};
        report["plots"] = json::array();
        progress.start("Compare distributions and render plots", groups.size() * variables.size());
        for (const auto &g : groups) {
            log.info("Compare group: {}", g);
            auto *target = output.mkdir(g.c_str());
            target->cd();
            for (const auto &v : variables) {
                const auto name = g + "_" + v, path = g + "/" + name;
                const auto &hA = *static_cast<TH1 *>(a.Get(path.c_str()));
                const auto &hB = *static_cast<TH1 *>(b.Get(path.c_str()));
                auto reference = copy(hA, name + "_reference"), candidate = copy(hB, name + "_candidate");
                const double sumA = total(hA), sumB = total(hB);
                auto &item = report["histograms"][name];
                item["reference_raw_sum"] = sumA;
                item["candidate_raw_sum"] = sumB;
                item["available"] = sumA > 0 && sumB > 0;
                progress.advance();
                if (sumA <= 0 || sumB <= 0) {
                    item["reason"] = "Empty or zero-weight distribution";
                    continue;
                }
                reference->Scale(1 / sumA);
                candidate->Scale(1 / sumB);
                item["distances"] = distances(*reference, *candidate);
                auto delta = copy(*candidate, name + "_difference"),
                     ratio = copy(*candidate, name + "_ratio"),
                     mask = copy(*candidate, name + "_ratio_undefined");
                delta->Add(reference.get(), -1);
                ratio->Divide(reference.get());
                mask->Reset();
                for (int i = 0; i < mask->GetNcells(); ++i)
                    if (reference->GetBinContent(i) == 0)
                        mask->SetBinContent(i, 1);
                reference->Write();
                candidate->Write();
                delta->Write();
                ratio->Write();
                mask->Write();
                if (!plots)
                    continue;
                canvas.Clear();
                TLegend legend(.62, .73, .88, .88);
                legend.SetBorderSize(0);
                if (hA.GetDimension() == 1) {
                    reference->SetLineColor(kBlue + 2);
                    candidate->SetLineColor(kRed + 1);
                    reference->SetLineWidth(2);
                    candidate->SetLineWidth(2);
                    reference->SetMaximum(1.15 * std::max(reference->GetMaximum(), candidate->GetMaximum()));
                    reference->GetYaxis()->SetTitle("Normalized bin probability");
                    reference->Draw("HIST");
                    candidate->Draw("HIST SAME");
                    legend.AddEntry(reference.get(), "Reference", "l");
                    legend.AddEntry(candidate.get(), "Candidate", "l");
                    legend.Draw();
                } else {
                    delta->SetTitle((name + ": candidate - reference").c_str());
                    delta->Draw("COLZ");
                }
                const auto file = "plots/" + name + ".svg";
                canvas.SaveAs((dir / file).c_str());
                report["plots"].push_back(file);
                report["plot_groups"][g].push_back(file);
            }
        }
        progress.finish();
        progress.start("Write comparison reports", 1);
        output.cd();
        TObjString object(report.dump().c_str());
        object.Write("comparison_report");
        if (output.TestBit(TFile::kWriteError))
            throw std::runtime_error("ROOT comparison output write failed");
        write(dir / "comparison.json", report.dump(2) + "\n");
        std::string html =
            "<!doctype html><html lang=\"en\"><meta charset=\"utf-8\"><title>PHSP "
            "comparison</title><style>body{font:16px system-ui;max-width:1250px;margin:32px auto;padding:0 "
            "20px;background:#f5f7fa;color:#17243a}section{background:white;margin:20px "
            "0;padding:20px;border-radius:12px}.gallery{display:grid;grid-template-columns:repeat(auto-fit,"
            "minmax(330px,1fr));gap:16px}img{width:100%}td,th{padding:8px;text-align:left}table{border-"
            "collapse:collapse;width:100%}tr{border-bottom:1px solid #ddd}</style><h1>PHSP "
            "comparison</h1><p><a href=\"comparison.json\">JSON</a> · <a "
            "href=\"comparison.root\">ROOT</a></p>";
        html += "<p>Analysis surface: " + planeA["mode"].get<std::string>();
        if (planeA["mode"] == "projected")
            html += "; reference Z = " + planeA["target_z_cm"].dump() + " cm (vacuum drift)";
        html += ".</p><p>Shape distributions include underflow/overflow in their normalization. Distances "
                "describe shape differences; they are not significance tests or dose "
                "comparisons.</p><h2>Distributions and correlations</h2>";
        const std::vector<std::string> labels{"All particles", "Photons", "Electrons",      "Positrons",
                                              "Neutrons",      "Protons", "Other particles"};
        for (size_t i = 0; i < groups.size(); ++i) {
            const auto &g = groups[i];
            html += "<section id=\"species-" + g + "\"><h3>" + labels[i] +
                    "</h3><table><tr><th>Histogram</th><th>Total variation</th><th>Jensen-Shannon "
                    "[nat]</th></tr>";
            bool any = false;
            for (const auto &v : variables) {
                const auto name = g + "_" + v;
                const auto &item = report["histograms"][name];
                if (!item["available"].get<bool>())
                    continue;
                any = true;
                html += "<tr><td>" + name + "</td><td>" + item["distances"]["total_variation"].dump() +
                        "</td><td>" + item["distances"]["jensen_shannon_divergence_nats"].dump() +
                        "</td></tr>";
            }
            html += "</table>";
            if (!any)
                html += "<p>No nonempty histogram pairs for this particle type.</p>";
            html += "<div class=\"gallery\">";
            if (report["plot_groups"].contains(g))
                for (const auto &plot : report["plot_groups"][g])
                    html += "<a href=\"" + plot.get<std::string>() + "\"><img loading=\"lazy\" src=\"" +
                            plot.get<std::string>() + "\"></a>";
            html += "</div></section>";
        }
        html += "</html>";
        write(dir / "comparison.html", html);
        progress.advance();
        progress.finish();
        log.info("Finished: {} histogram pairs | {} plots | report: {}", groups.size() * variables.size(),
                 report["plots"].size(), (dir / "comparison.html").string());
        return dir;
    } catch (const std::exception &error) {
        log.error("Comparison failed: {}", error.what());
        throw;
    }
}
