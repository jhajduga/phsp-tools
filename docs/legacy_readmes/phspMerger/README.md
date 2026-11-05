# phspMerger

> [!IMPORTANT]
> **This repository is obsolete and is no longer maintained.**
> Its functionality has moved to **[PHSP Tools](https://github.com/jhajduga/phsp-tools)**.
> Use the new repository for current code, documentation and issue reports.

This repository contains the original standalone IAEA phase-space merger and is
retained for historical reference. Further development takes place in PHSP Tools.

## Replacement: PHSP Tools

PHSP Tools includes `phspMerger` as a C++ application alongside aperture cutting,
IAEA-to-ROOT conversion, particle/history analysis and source comparison.
Merging can run directly from the command line or as a stage in a JSON pipeline.

The integrated merger provides:

- Concatenation of IAEA sources in input order, including their final records.
- Preservation of history boundaries and compatible extra fields.
- Explicit removal of opaque extras when schemas differ, while retaining history increments.
- Original-primary-history counts summed across independent runs.
- Input validation, processing provenance and progress logging.

## Migration

Follow the [PHSP Tools build instructions](https://github.com/jhajduga/phsp-tools/blob/HEAD/docs/building.md),
then run the application from the PHSP Tools directory:

```sh
build/bin/phspMerger /data/run1 /data/run2 \
  --output results/merged/beam
```

The application writes a merged IAEA pair plus processing metadata. Inputs should
represent independent runs with compatible coordinate frames and source definitions.
Merging does not automatically align scoring surfaces or reweight different beams.

The earlier positional output syntax is also supported:

```sh
build/bin/phspMerger /data/run1 /data/run2 results/merged/beam
```

The earlier Python interface is not included in PHSP Tools. Use its CLI or JSON
pipelines for new workflows.

- [Merger documentation and options](https://github.com/jhajduga/phsp-tools/blob/HEAD/docs/processing.md)
- [Merge → cut → convert → analyze example](https://github.com/jhajduga/phsp-tools/blob/HEAD/examples/preprocessing.json)
- [PHSP Tools repository](https://github.com/jhajduga/phsp-tools)
- [Report an issue in PHSP Tools](https://github.com/jhajduga/phsp-tools/issues)
