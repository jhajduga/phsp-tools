# Analysis catalogue and interpretation

[Back to README](../README.md)

This catalogue describes the objects written by `phspAnalyze` and `phspCompare`.
Names below are suffixes, for example `photon/photon_energy` or `electron/electron_xy`.
Groups exist for `all`, `photon`, `electron`, `positron`, `neutron`, `proton`, `other`.
The **Distributions and correlations** section contains a separate subsection for
all particles and each species. Each nonempty group receives its full figure set;
empty groups are identified explicitly and retain their empty ROOT objects.

## Conventions and selection

- E: **kinetic energy** in MeV, after decoding the IAEA energy-sign history marker.
- x,y,z: positions in cm at the **analysis plane**, default Z=100 cm. See [geometry](geometry.md).
- u,v,wz: direction cosines; wz is `dir_w`.
- W: nonnegative statistical weight from `weight`.
- r = sqrt(x²+y²), theta = acos(wz), phi = atan2(v,u); plotted angles are degrees.
- Weighted statistics use W. `energy_records` and history counts use unit weights.
- JSON `rms_spread` means sqrt(variance), rather than sqrt(mean(q²)).

Particle analyses reject invalid numeric values, negative E/W, directions with
|u²+v²+wz²−1| > 0.001, and rays unable to reach the selected target in forward time.
A zero-weight record contributes to record counts and the unweighted mean energy,
but not weighted moments. Empty statistics are `null` in JSON.

All particle distributions, moments, profiles, PCA and optics use the same selected
rays and projected coordinates. **Scored-history aggregates use the original source
records**, independently of particle-type or projection selection. The converter's
ROOT tree retains original coordinates.

## 1. Individual-record energy

| Object | Axis / content | Interpretation |
|---|---|---|
| `energy` | E [MeV] / sum W per bin | Weighted energy spectrum, accounting for Monte Carlo weights. |
| `energy_records` | E [MeV] / record count | Stored-record spectrum; splitting and roulette can make it differ from the weighted spectrum. |

JSON includes unweighted and weighted mean E, weighted spread and sum E·W.
Moments include all accepted records, independently of histogram limits. A small
`--energy-max` can therefore clip the displayed spectrum without changing its
reported full-sample mean. Check overflow before comparing the two.

`energy_window_0.510_to_0.512_MeV_records` counts records in this kinetic-energy
window. An excess can be interesting for photons, but the window alone does not
identify annihilation particles. No peak fitting or background subtraction is performed.

## 2. Position, profiles and halo

| Object | Axis / content | Interpretation |
|---|---|---|
| `x`, `y` | Position [cm] / sum W | Width, displacement and tails along each axis. |
| `xy` | X/Y [cm] / sum W per bin | Field shape, asymmetry and structure at the analysis plane. |
| `radius` | r [cm] / sum W per annulus | Weighted records in successive rings. |
| `radial_crossing_density` | r [cm] / sum W / annulus area [cm⁻²] | Azimuthally averaged crossing density, removing the increasing area of outer rings. |
| `radial_cumulative` (TGraph) | r [cm] / cumulative weight fraction | A value of 0.9 identifies a radius containing 90% of the weight within the histogram range. |

Annular density divides a bin by `pi*(r_high²-r_low²)`. This differs from records
per unit radius. XY maps are not automatically divided by bin area or |wz|.

JSON `species/<type>/spatial` gives 50/80/90/95/99% containment radii and mirror
asymmetry. Radii are approximated from bins about x=y=0, without recentering at the
centroid. A large radius can indicate halo, a broad field or a displaced beam.

Profile mirror asymmetry is `0.5*sum(abs(p_i-p_mirror(i)))` with p normalized over
visible bins. Zero means agreement with the reflection; one means disjoint mass.
This is a descriptive measure, not a clinical symmetry or flatness definition.

Containment and asymmetry use **in-range bins only**. Overflow can underestimate
radii and alter asymmetry. Check `histograms/*/out_of_range_sum`.

## 3. Directions and position–angle correlations

| Object | Axis / content | Interpretation |
|---|---|---|
| `theta` | Theta [deg] / sum W | Near 0: +Z motion; near 90: nearly tangential; near 180: −Z motion. |
| `phi` | Phi [deg] / sum W | Preferred transverse azimuth; −180..180 has an arbitrary seam. |
| `x_angle` | x [cm], atan2(u,wz) [deg] / sum W | Direction versus position; a sloping band can indicate convergence or divergence. |
| `y_angle` | y [cm], atan2(v,wz) [deg] / sum W | Corresponding Y-axis structure. |

JSON gives backward-record count, backward weight fraction and tangential count
(`abs(wz)<=1e-8`) among accepted rays. Default projection onto a downstream plane
excludes backward rays that cannot reach it; use `--native-plane` to inspect the
original backward component. Source/accepted/rejected counts are reported separately.

Theta bins are not divided by sin(theta), so this is not a distribution per steradian.
“Forward” means +Z in the configured reference frame, rather than an inferred beam axis.

## 4. Energy versus position and direction

| Object | Content | Interpretation |
|---|---|---|
| `energy_radius` | r [cm] × E [MeV], sum W | Spectral changes from the axis to the off-axis region. |
| `energy_theta` | theta [deg] × E [MeV], sum W | Relation between energy and divergence. |
| `xy_energy` | X/Y [cm], sum E·W [MeV] per bin | Where weighted kinetic energy crosses the analysis plane. |
| `mean_energy_x`, `mean_energy_y` | TProfile: position [cm] → mean E [MeV] | Weighted mean energy along each axis. |
| `mean_energy_radius` | TProfile: r [cm] → mean E [MeV] | Core-to-halo spectral trend. |
| `mean_energy_xy` | TProfile2D: X/Y [cm] → mean E [MeV] | Spatial energy variation separately from local particle abundance. |

The bin mean is `sum(E*W)/sum(W)`. An `xy_energy` excess can reflect more particles,
higher energies, or both; inspect `xy` and `mean_energy_xy` alongside it. Empty profile
bins are not measurements of zero mean energy: inspect bin entries/weight in ROOT.
TProfile errors use entry statistics and do not automatically account for within-history correlations.

## 5. Composition and moments

`report.json/species` includes these fields for each group:

| Field | Meaning / interpretation |
|---|---|
| `records` | Accepted stored-record count. |
| `sum_weights` | Sum W, the denominator for weighted particle fractions. |
| `weighted_particle_fraction` | Species share of all selected particle weight. |
| `sum_weighted_kinetic_energy_MeV` | Sum E·W. |
| `weighted_kinetic_energy_fraction` | Species share of total selected E·W. |
| `mean`, `rms_spread` | Weighted centres and spreads of E,x,y,z,r,theta,wz. |

Record fraction, weight fraction and energy fraction describe different properties.
Many low-weight records can contribute little weighted mass. If `--particle` is used,
`all` represents that selected species.

## 6. Covariance, correlation and PCA

JSON `covariance` and `correlation` cover E,x,y,z,r,theta,wz in the order listed in
`columns`. The correlation matrix is also a ROOT TH2 and a rendered figure.

- Positive Pearson correlation: quantities tend to increase together.
- Negative correlation: one tends to decrease as the other increases.
- Near zero: little **linear** association; nonlinear structure may remain.
- Constant columns: JSON correlation is `null`, displayed ROOT cells are zero.

PCA diagonalizes the weighted **correlation matrix**, so variables are standardized.
Zero-variance columns are omitted; in projected analyses this includes the constant Z.
`TMatrixDSymEigen` supplies eigenvalues/eigenvectors. `explained_variance_fraction`
gives each component's share, and `eigenvectors_by_column[i][j]` is column i's loading
in component j. A dominant component describes shared linear variation.

Correlation and PCA do not establish causation or replace comparison of the full
joint distribution. Their values depend on the selected sample.

## 7. Emittance, Twiss and free-drift waist

`forward_beam` uses wz>1e-8, with slopes x'=u/wz and y'=v/wz. For each axis:

```text
emittance = sqrt(Var(x)*Var(x') - Cov(x,x')²)
beta = Var(x)/emittance
alpha = -Cov(x,x')/emittance
gamma = Var(x')/emittance
waist_drift = -Cov(x,x')/Var(x')
```

| Result | Unit | Simplified interpretation |
|---|---|---|
| Geometric RMS emittance | cm for dimensionless slope; conventionally cm·rad in the paraxial approximation | Combined size/divergence structure, including their correlation. |
| Beta / gamma | cm / cm⁻¹ | Size and slope contributions scaled by emittance. |
| Alpha | dimensionless | Position–slope correlation with the sign convention above. |
| `waist_drift_cm` | cm from the analysis plane | Positive: minimum size after +Z drift; negative: minimum lies upstream. |

These are geometric moments without relativistic normalization. Default projection
makes them describe a single common plane. Native analyses with varying Z mix scoring
positions and include a warning. Waist predicts the minimum position variance under
free drift only; interactions, focusing elements and collimation are not simulated.

## 8. Weights and quality

`all_weights` is record count versus W. `effective_records=(sum(W))²/sum(W²)` describes
weight concentration: equal positive weights give the record count; unequal weights
reduce it. It is **not** automatically the number of independent histories. Splitting
can produce correlated particles despite large N_eff. ROOT Sumw2 particle errors
assume independent entries; history statistics are reported separately.

`quality` separates invalid E/W/numeric values from non-unit directions among otherwise
valid records. It also reports `source_selected_records` and `projection_rejected_records`.
`histograms` gives in-range and out-of-range sums. Full-sample moments can differ from
clipped histograms. Direction checks apply after decoding: the reference IAEA library
may already have normalized invalid transverse cosines.

## 9. Scored histories: multiplicity and energy

Histograms in `histories/` include **complete groups only**, at the original scoring
surface. These do not become new histories after geometric projection.

| Object | Axis / content | Interpretation |
|---|---|---|
| `history_length` | Records per history / histories | Typical and extreme stored multiplicity. |
| `history_energy` | Sum E [MeV] / histories | Variation in recorded kinetic energy per scored history. |
| `history_weighted_energy` | Sum E·W [MeV] / histories | Corresponding particle-weighted energy sums. |
| `history_max_energy` | Maximum E [MeV] / histories | Most energetic stored record in each history. |
| `history_length_energy` | Multiplicity × sum E | Whether larger scored groups contain more recorded energy. |

Axes extend automatically for large multiplicities/sums. Each complete history has
unit entry weight. The sum of its particle weights is not treated as a universal
weight for the whole history.

`histories/histories` stores ID, multiplicity, record-index bounds, per-species counts
and energy sums, total E/E·W/W, maximum E and `complete`. For custom distributions
of energy within a history, see [history_analysis.C](../examples/history_analysis.C).

JSON includes mean multiplicity, mean energy sums, spreads, correlations and
`standard_error_of_mean`. The latter uses complete scored histories as statistical
units: sqrt(population_variance/(N−1)) for N>1.

**Length means records on the scoring surface**, not track length, all cascade
particles or transport steps. Sum E is not a primary-energy balance. Splitting and
repeated crossings can affect the recorded values.

`known_empty_histories_from_increments` counts explicit gaps in history increments.
Empty histories are not inserted into multiplicity histograms; their means therefore
differ from means over all original primary histories. Unknown starting groups,
truncated groups and groups flagged by preparation provenance are excluded from
complete-group statistics. [History integrity rules](data_format.md).

## 10. Energy by record order within a history

`all_history_particle_energy` plots `history_particle_index` × E [MeV], weighted by W.
The index starts at zero and means order of recording at the surface. This particle
map also includes incomplete histories and follows the particle/plane selection.
Its index axis has 64 bins; later indices contribute to reported overflow. Individual
energies and IDs remain available in the original ROOT tree.

This map can show spectra changing with record order. It does not establish particle
age, parent–child relationships or interaction chronology.

## 11. Common-plane projection

`--project-z Z` defaults to **100 cm** and drives all particle analyses. For each ray:

```text
z_reference = z_source + source_z_offset_cm
t = (Z-z_reference)/wz
x_analysis = x_source+t*u
y_analysis = y_source+t*v
```

Only finite intersections with abs(wz)>1e-8 and t>=0 are accepted. The converter reads
constant Z from the header or uses each record's stored Z. `projected_xy` is a ROOT
alias of the all-particle XY map. `--native-plane` opts out explicitly.

Histogram ranges still use `--xy-max`; drift can increase overflow. Energy/direction
are unchanged. This models straight-line vacuum travel without energy loss or
scattering, especially relevant to interpretation of charged particles. Coordinate
origin, beam-axis and physical-isocentre assumptions are documented in [geometry](geometry.md).

## 12. Original-primary-history normalization

`normalization` requires a complete, header-consistent source, no stride, no conversion
prefix and no analysis prefix. Preparation provenance must also indicate a complete
source. Counts, sum W and sum E·W are divided by `$ORIG_HISTORIES`.

These are values per declared original primary history, including histories with no
scored particle. A complete aperture cut retains the original denominator. A merge
sums denominators for **independent runs**. Partial/uncertain sources disable this
normalization rather than guessing the missing fraction.

## 13. Comparing two analyses

Compared objects, for each particle group: `energy`, `energy_records`, `x`, `y`,
`radius`, `theta`, `phi`, `xy`, `energy_radius`, `energy_theta`, `x_angle`, `y_angle`,
`xy_energy`. Bin edges and analysis planes must match. Native sources with different
observed Z ranges are rejected. Mean-energy profiles, PCA, emittance and history
histograms are not automatically compared.

Histograms are normalized over **all bins, including underflow/overflow**. JSON retains
raw sums. ROOT object suffixes:

| Suffix | Interpretation |
|---|---|
| `_reference`, `_candidate` | Normalized binned distributions. |
| `_difference` | Candidate−reference; positive regions indicate relative excess in candidate. |
| `_ratio` | Candidate/reference; 1 indicates equal bin content. |
| `_ratio_undefined` | Mask of zero-denominator bins; their ratio is stored as zero. |

| JSON metric | Formula / unit | Interpretation |
|---|---|---|
| `total_variation` | 0.5·sum(abs(p−q)), range 0..1 | Zero: equal bin masses; one: disjoint support. |
| `jensen_shannon_divergence_nats` | 0.5·KL(p,m)+0.5·KL(q,m), m=(p+q)/2 | Zero: equal; maximum ln(2) for disjoint distributions. |
| `wasserstein_bin_centers_axis_units` | Integrated CDF difference using bin-centre spacing | Typical mass displacement in axis units, such as MeV or cm. |

Wasserstein is a bin-centre approximation, available only for 1D distributions with
no nonzero underflow/overflow. Empty/zero-weight pairs have `available=false`.
HTML/PDF show 1D overlays and 2D differences in **each species subsection**.

These are shape differences, not p-values. Normalization removes total-yield differences,
so also inspect raw sums and source normalization. Matching bins/targets do not prove
matching physical coordinate frames, units, selections or weight semantics. No gamma
analysis or phantom transport is performed.
