# Literature and design references

[Back to README](../README.md)

## Joint distributions and source comparisons

*Application of Conditional Generative Adversarial Networks to Efficiently Generate
Photon Phase Space in Medical Linear Accelerators of Different Primary Beam Parameters*,
Applied Sciences 13(12), 7204 (2023):
[publisher article](https://www.mdpi.com/2076-3417/13/12/7204),
[XML](https://www.mdpi.com/2076-3417/13/12/7204/xml).

The study models joint photon position, energy and momentum distributions and compares
Monte Carlo/GAN phase-space histograms, followed by phantom/patient dose calculations.
Its discussion of weights and annihilation photons motivates inspecting more than
one-dimensional energy spectra. This toolkit includes E–r, E–theta, position–direction,
mean-energy profiles, correlations, PCA and source comparisons. Its histogram distances
are additional descriptive methods, not a reproduction of that paper's validation
protocol. Dose validation requires a separate transport calculation.

## Spatial maps and species

*Monte Carlo evaluation of particle interactions within the patient-dependent part of
Elekta 6 MV photon beam applying IAEA phase space data*:
[full article](https://pmc.ncbi.nlm.nih.gov/articles/PMC8726437/).

The IAEA analysis presents spectra, photon/electron/positron XY maps and energy versus
X/Y. These support separate species distributions and spatial energy maps. The paper's
transport and dosimetric comparisons extend beyond this toolkit's scored-record analysis.

[GAMOS radiotherapy documentation](https://fismed.ciemat.es/GAMOS/GAMOS_doc/GAMOS.6.0.0/Radiotherapy/Radiotherapy.html)
describes per-species X/Y/XY, radius, angular and energy histograms, and counts relative
to original histories. A similar species organization is used here.

## Reference planes

[A published virtual-source model study](https://pmc.ncbi.nlm.nih.gov/articles/PMC9121055/)
compares distributions at the PHSP surface and at an isocentre at Z=100 cm. This supports
a conventional comparison plane under target-centred coordinates, not an inference
that every IAEA source shares that frame. [TOPAS phase-space source documentation](https://opentopas.readthedocs.io/en/latest/parameters/source/phasespace.html)
explains that an external source's coordinate system is not automatically known.
Our chosen default, transforms and limitations are documented in [geometry](geometry.md).

## Histories and correlated particles

[IAEA INDC(NDS)-0484](https://www-nds.iaea.org/phsp/documents/indc-nds-0484.pdf)
describes the data standard. The bundled reference implementation, particularly
`iaea_get_particle` and extra-long type 1 in
[iaea_phsp.h](../core/vendor/iaea/include/iaea_phsp.h), defines the decoded increment
used by this toolkit.

[BEAMnrc manual](https://nrc-cnrc.github.io/EGSnrc/doc/pirs509a-beamnrc.pdf) and
[DOSXYZnrc manual](https://nrc-cnrc.github.io/EGSnrc/doc/pirs794-dosxyznrc.pdf)
explain signed-energy history markers and the statistical importance of correlated
particle groups. We retain both increments and a cumulative ID: a stored record is
not automatically an independent history.

[TOPAS](https://opentopas.readthedocs.io/en/latest/parameters/source/phasespace.html)
also describes old IAEA-source issues such as an unmarked first record in some files.
An unknown starting group is consequently not accepted as a complete history.

## ROOT facilities

[ROOT RDataFrame documentation](https://root.cern.ch/doc/master/classROOT_1_1RDataFrame.html)
is the reference for lazy actions, filters, histograms and implicit multithreading.
The implementation uses APIs available in the tested ROOT 6.34 environment:
`RDataFrame`, `RDatasetSpec`, `TH1D/TH2D`, `TProfile/TProfile2D`, `TGraph`,
`TMatrixDSymEigen`, `TTreeReader`, `TTree` and `TCanvas`.

Containment, mirror asymmetry, geometric optics, PCA and distribution distances are
additional descriptive analyses. They are not all attributed to the MDPI paper.
