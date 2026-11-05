# Examples and ROOT macros

[Back to README](../README.md)

Example files live in the repository's [examples directory](../examples/README.md).
Run the following commands from the project root with a compatible ROOT environment.
Replace input paths and choose histogram ranges for your source.

## Two-source analysis and comparison

```sh
build/bin/phspPipeline examples/pipeline.json
```

The [comparison configuration](../examples/pipeline.json) converts and analyzes two
sources, then compares them at a common reference plane. Paths in JSON are relative
to that file. Its current five-million-record limit is a preview, so full-source
primary-history normalization is unavailable. See [pipeline settings](pipeline.md).

## Merge, cut and analyze

[preprocessing.json](../examples/preprocessing.json) demonstrates ordered preparation.
Replace the two independent-run input paths before running:

```sh
build/bin/phspPipeline examples/preprocessing.json
```

```json
{{#include ../examples/preprocessing.json}}
```

## Custom particle analysis

```sh
root -l -b -q 'examples/custom_analysis.C("results/beam/conversion/beam.root","custom_analysis.root")'
```

The [macro source](../examples/custom_analysis.C) compares photon spectra in core
and halo regions and writes a mean-energy profile. It reads converted records at
their original scoring positions; it does not inherit analysis-plane projection.
Adapt its regions to the physical beam.

## Custom history analysis

```sh
root -l -b -q 'examples/history_analysis.C("results/beam/analysis/analysis.root","custom_histories.root")'
```

The [history macro](../examples/history_analysis.C) reads the analysis output's
`histories/histories` table, selects complete groups, and studies multiplicity and
photon/electron energy sums within each scored history. The table is available only
when exact history analysis can be performed. [History integrity](data_format.md).
