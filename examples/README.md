# Examples

[Back to README](../README.md)

## Analysis/comparison pipeline

Copy `pipeline.json`, replace inputs and choose suitable histogram ranges:

```sh
build/bin/phspPipeline examples/pipeline.json
```

Paths are relative to the JSON file. `../results/beam_comparison` therefore points
to this project's `results/beam_comparison`. The current example uses a five-million
record preview and permits incomplete input. These limits and ranges are not universal.
The particle-analysis target is explicitly 100 cm; [coordinate assumptions](../docs/geometry.md)
must match the physical source.

## Preparation pipeline

[preprocessing.json](preprocessing.json) demonstrates merge → rectangular cut →
conversion → analysis. Replace `/data/...` inputs before running:

```sh
build/bin/phspPipeline examples/preprocessing.json
```

`@name` refers to a previous preparation output. See [pipeline settings](../docs/pipeline.md)
and [cutter/merger semantics](../docs/processing.md).

## Custom particle analysis

```sh
root -l -b -q 'examples/custom_analysis.C("results/beam/conversion/beam.root","custom_analysis.root")'
```

This macro compares photon spectra in a core r<2 cm and halo r>=10 cm, and writes a
mean-energy profile versus radius. Adapt regions to the field. Its input is the
**converted particle ROOT**, so the macro uses original scoring coordinates. It does
not automatically inherit `phspAnalyze` projection settings.

## Custom history analysis

```sh
root -l -b -q 'examples/history_analysis.C("results/beam/analysis/analysis.root","custom_histories.root")'
```

Its input is the **analysis** output containing `histories/histories`. It selects
complete groups, studies multi-record and mixed photon–electron histories, and maps
their species energy sums. If exact history analysis is unavailable, the table is
absent; consult the report for the reason.
