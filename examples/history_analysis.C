#include <ROOT/RDataFrame.hxx>
#include <TFile.h>
#include <iostream>

// Input is analysis.root from phspAnalyze, not the converted particle file.
// root -l -b -q 'examples/history_analysis.C("results/beam/analysis/analysis.root")'
void history_analysis(const char *input, const char *output = "custom_histories.root") {
    ROOT::RDataFrame df("histories/histories", input);
    auto full = df.Filter("complete", "complete scored histories");
    auto multi = full.Filter("n_particles > 1", "multiple scored particles");
    auto mixed = full.Filter("n_photons > 0 && n_electrons > 0", "photon-electron histories");
    auto meanLength = full.Mean("n_particles");
    auto count = mixed.Count();
    auto summed = multi.Histo1D(
        {"multi_energy", "Multi-particle histories;Sum E [MeV];Histories", 200, 0, 20}, "sum_energy_MeV");
    auto photons = full.Histo1D(
        {"photon_energy_per_history", "Photon energy per history;Sum photon E [MeV];Histories", 200, 0, 20},
        "photon_energy_MeV");
    auto pair =
        full.Histo2D({"photon_electron_energy", "Energy in the same history;Photon E [MeV];Electron E [MeV]",
                      100, 0, 10, 100, 0, 10},
                     "photon_energy_MeV", "electron_energy_MeV");
    std::cout << "Mean scored history length=" << *meanLength << ", mixed histories=" << *count << '\n';
    TFile file(output, "RECREATE");
    summed->Write();
    photons->Write();
    pair->Write();
}
