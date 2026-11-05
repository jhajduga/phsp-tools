# phspCutter

> [!IMPORTANT]
> **This repository is obsolete and is no longer maintained.**
> Its functionality has moved to **[PHSP Tools](https://github.com/jhajduga/phsp-tools)**.
> Use the new repository for current code, documentation and issue reports.

This repository contains the original standalone IAEA phase-space cutter and is
retained for historical reference. Further development takes place in PHSP Tools.

## Replacement: PHSP Tools

PHSP Tools includes `phspCutter` as a C++ application alongside IAEA merging,
IAEA-to-ROOT conversion, particle/history analysis and source comparison.
Cutting can run directly from the command line or as a stage in a JSON pipeline.

The integrated cutter provides:

- Configurable rectangular aperture bounds and reference-plane Z.
- Selection using each particle's scoring position and direction.
- Preservation of original positions, with optional projected-coordinate output.
- History-increment preservation and original-primary-history normalization metadata.
- Input validation, processing provenance and progress logging.

## Migration

Follow the [PHSP Tools build instructions](https://github.com/jhajduga/phsp-tools/blob/HEAD/docs/building.md),
then run the application from the PHSP Tools directory:

```sh
build/bin/phspCutter /data/beam results/cut/beam \
  --project-z 100 \
  --x-min -7 --x-max 7 \
  --y-min -7 --y-max 7
```

This selects rays crossing the rectangle at Z=100 cm and writes a new IAEA pair
plus processing metadata. Original scoring coordinates are retained by default.
The reference plane assumes a compatible coordinate frame; see the
[geometry documentation](https://github.com/jhajduga/phsp-tools/blob/HEAD/docs/geometry.md).

The earlier Python interface is not included in PHSP Tools. Use its CLI or JSON
pipelines for new workflows.

- [Cutter documentation and options](https://github.com/jhajduga/phsp-tools/blob/HEAD/docs/processing.md)
- [Merge → cut → convert → analyze example](https://github.com/jhajduga/phsp-tools/blob/HEAD/examples/preprocessing.json)
- [PHSP Tools repository](https://github.com/jhajduga/phsp-tools)
- [Report an issue in PHSP Tools](https://github.com/jhajduga/phsp-tools/issues)
