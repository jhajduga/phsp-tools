#include <ROOT/RDataFrame.hxx>
#include <TFile.h>
#include <iostream>

// root -l -b -q 'examples/custom_analysis.C("results/beam/conversion/beam.root")'
void custom_analysis(const char *input, const char *output = "custom_analysis.root") {
    ROOT::RDataFrame df("phsp", input);
    auto photons = df.Filter("particle_type == 1 && energy_MeV >= 0 && weight >= 0")
                       .Define("r_cm", "sqrt(pos_x_cm*pos_x_cm + pos_y_cm*pos_y_cm)");
    auto central = photons.Filter("r_cm < 2.0", "central 2 cm");
    auto halo = photons.Filter("r_cm >= 10.0", "halo beyond 10 cm");
    auto centralEnergy = central.Histo1D({"central_energy", "Central photons;E [MeV];Sum weights", 140, 0, 7},
                                         "energy_MeV", "weight");
    auto haloEnergy =
        halo.Histo1D({"halo_energy", "Halo photons;E [MeV];Sum weights", 140, 0, 7}, "energy_MeV", "weight");
    auto profile =
        photons.Profile1D({"mean_energy_radius", "Photon mean energy;r [cm];Mean E [MeV]", 100, 0, 32},
                          "r_cm", "energy_MeV", "weight");
    auto centralWeight = central.Sum("weight");
    auto haloWeight = halo.Sum("weight");
    // All actions above share one event loop. Sum() is weighted explicitly.
    std::cout << "Central sum(w)=" << *centralWeight << ", halo sum(w)=" << *haloWeight << '\n';
    TFile file(output, "RECREATE");
    centralEnergy->Write();
    haloEnergy->Write();
    profile->Write();
}
