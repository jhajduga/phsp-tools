# Common reference-plane geometry

[Back to README](../README.md)

## Default: Z = 100 cm

Particle analysis uses **100 cm in a shared target-centred +Z coordinate system** by
default. This is a conventional linac comparison plane: a published virtual-source
model validation compares particle distributions both at the PHSP surface and at
an isocentre at Z=100 cm. [Published source-model study](https://pmc.ncbi.nlm.nih.gov/articles/PMC9121055/).

This default is an explicit convention, not a determination of the physical
isocentre from the file. IAEA files can use different coordinate origins and scoring
surfaces. TOPAS likewise states that the coordinate system of an externally generated
phase space cannot be known automatically. [TOPAS phase-space documentation](https://opentopas.readthedocs.io/en/latest/parameters/source/phasespace.html).

Blank coordinate-system or nominal-SSD fields provide no origin calibration. Nominal
SSD is not substituted for source Z or treated as a universal target-to-isocentre distance.

## Source Z

If `RECORD_CONTENTS` marks Z as constant, the IAEA reader obtains it from
`RECORD_CONSTANT` and materializes it in the ROOT `pos_z_cm` branch. If Z is stored,
**each record's Z is used**. A statistical header range describes the source but
cannot replace per-record positions on a cylindrical or non-planar surface.

Reports retain the header's constant/range when present, the observed original Z
range, the applied source offset, the target and the coordinate assumption. The
converter retains original coordinates; analysis does not modify the input ROOT.

## Transformation

```text
z_reference = pos_z_cm + source_z_offset_cm
t = (project_z_cm - z_reference) / dir_w
x_analysis = pos_x_cm + t*dir_u
y_analysis = pos_y_cm + t*dir_v
z_analysis = project_z_cm
```

A ray is accepted only when its direction is not tangential (`abs(dir_w)>1e-8`),
t>=0 and the resulting coordinates are finite. Backward rays upstream of a downstream
target are excluded because they never reach it in forward time. Counts before and
after this selection, including rejected rays, are reported separately.

This transformation drives **all particle histograms, moments, profiles, PCA,
containment metrics and beam-optics results**. It is not merely an extra XY image.
Energy and direction remain unchanged by this vacuum model. `projected_xy` is retained
as a ROOT alias for the all-particle reference-plane XY histogram.

Scored-history multiplicity and energy sums still describe the original recorded
histories. Their definition is not changed by the geometric selection.

## Overrides

```sh
build/bin/phspAnalyze beam.root --project-z 100
build/bin/phspAnalyze beam.root --project-z 100 --source-z-offset 25
build/bin/phspAnalyze beam.root --native-plane
```

JSON fields are `project_z_cm` (default 100), `source_z_offset_cm` (default 0), and
`native_plane` (default false). An offset is appropriate only for a **known** origin
shift. For example, if the same +Z physical frame places isocentre at local Z=0,
an offset of +100 cm maps that local origin to reference Z=100 cm. The toolkit does
not infer rotations, reverse the beam axis, align X/Y origins or infer machine geometry.

Choose a different target if 100 cm is behind the source for the desired rays.
There is no silent backward extrapolation or automatic move to another target.

## What is and is not simulated

Projection is straight-line motion in vacuum. It omits air/matter interactions,
scattering, absorption, energy loss, collimators, jaws, an MLC and dose deposition.
For electrons, positrons and hadrons, these omissions are especially important when
interpreting a downstream distribution. Geometric agreement at the reference plane
does not validate a transport or dose model.

Sources scored before and after beam-shaping components can retain physically
important differences after projection. A common plane removes a geometric mismatch;
it does not make different source definitions equivalent.

## Comparison safeguards

Pipeline configuration requires matching projection settings for compared datasets.
`phspCompare` also reads analysis-plane metadata and rejects different targets or a
mixture of native/projected analyses. Native analyses with different observed Z ranges
are rejected. Old reports lacking plane metadata must be regenerated. Equal numeric
Z alone does not prove that coordinate origins/orientations represent the same physical frame.
