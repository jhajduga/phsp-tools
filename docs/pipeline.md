# JSON pipeline

[Back to README](../README.md)

```sh
build/bin/phspPipeline examples/pipeline.json --progress-seconds 2
```

The pipeline calls the same C++ functions as the standalone applications. It does
not execute shell commands and does not require a Python interface.

## Paths, names and execution order

Paths in JSON are relative to the configuration file. CLI `--output DIR` overrides
`output_dir` and is relative to the working directory. Names use ASCII letters,
digits, dots, underscores and hyphens; names must be unique within their section.
`.` and `..` are rejected.

Execution order is preparation stages, datasets, then comparisons. Preparation
references use `@name` and may refer only to earlier stages. Dataset inputs can
reference any completed preparation stage. Independent datasets are processed
sequentially; conversion and RDataFrame use the selected internal thread counts.

## Top-level keys

| Key | Default | Meaning |
|---|---|---|
| `output_dir` | `results` next to config | Run directory. |
| `threads` | 4 | Shared conversion/analysis thread count. |
| `conversion` | empty object | Shared conversion settings. |
| `analysis` | empty object | Shared analysis settings. |
| `preprocessing` | empty array | Ordered IAEA cut/merge operations. |
| `datasets` | required nonempty array | Named IAEA/ROOT inputs. |
| `comparisons` | empty array | Named dataset pairs. |

Unknown keys and invalid types/ranges are rejected rather than silently ignored.
File existence, references and comparison settings are checked before processing.
A preparation output is checked as a planned input before it has been created.

## Conversion settings

| Key | Default | Meaning |
|---|---|---|
| `threads` | top-level threads | Conversion workers. |
| `max_records` | −1, all | Maximum written records. |
| `stride` | 1 | Keep every Nth input record; disables exact history analysis when >1. |
| `block_size` | 200000 | Records per bounded processing block. |
| `queue_capacity` | 8 | Maximum blocks per queue. |
| `extras` | false | Preserve optional IAEA fields in ROOT. |
| `allow_incomplete` | false | Permit size/count mismatch, retain complete records and mark incomplete provenance. |

## Analysis settings

| Key | Default | Meaning |
|---|---|---|
| `threads` | top-level threads | RDataFrame workers. |
| `max_records` | −1, all | ROOT input prefix before type/plane selection. |
| `particle` | 0, all | IAEA type 1..5. |
| `bins` | 120 | Number of bins, 8..1000. |
| `energy_max_MeV` | data maximum | Energy histogram upper edge. |
| `xy_max_cm` | data maximum | Symmetric analysis-plane XY limits. |
| `project_z_cm` | **100** | Common reference target, applied to all particle analyses. |
| `source_z_offset_cm` | 0 | Known source-origin shift before projection. |
| `native_plane` | false | Disable projection; takes precedence over project_z_cm. |
| `plots` | true | SVG and PDF rendering. |
| `histories` | true | Sequential original-history aggregation. |

For comparisons, energy/XY bounds and bin counts must be explicit and identical.
Particle selection, projection mode and target must also agree. Source Z offsets
may differ to align known origins. Coordinate conventions are not inferred.
[Geometry and limitations](geometry.md).

## Per-dataset overrides

Dataset `conversion` and `analysis` objects merge over the shared settings:

```json
{
  "analysis": {"bins": 120, "energy_max_MeV": 7, "xy_max_cm": 32, "project_z_cm": 100},
  "datasets": [
    {"name": "full", "input": "/data/full.IAEAheader"},
    {"name": "preview", "input": "/data/large.IAEAheader",
     "conversion": {"max_records": 200000, "allow_incomplete": true},
     "analysis": {"plots": false, "source_z_offset_cm": 0}}
  ]
}
```

`.root` inputs skip conversion. Scored-history analysis uses all original particle
types in the selected prefix, independently of the particle/plane histogram selection.

## Preparation stages

```json
"preprocessing": [
  {"name": "joined", "operation": "merge", "inputs": ["/data/part1", "/data/part2"]},
  {"name": "field", "operation": "cut", "input": "@joined",
   "x_min_cm": -7, "x_max_cm": 7, "y_min_cm": -7, "y_max_cm": 7}
],
"datasets": [{"name": "selected", "input": "@field"}]
```

Merge fields: `inputs`, `drop_extras`, `allow_incomplete`, `max_records_per_input`.
Cut fields: `input`, `project_z_cm`, `source_z_offset_cm`, `x_min_cm`, `x_max_cm`,
`y_min_cm`, `y_max_cm`, `write_projected`, `positive_z_only`, `max_records`,
`drop_extras`, `allow_incomplete`. `name` and `operation` are required for every stage.
[Defaults, schema compatibility and history semantics](processing.md).

## Comparisons

```json
"comparisons": [
  {"name": "candidate_vs_reference", "reference": "reference", "candidate": "candidate", "plots": true}
]
```

The default name is `<candidate>_vs_<reference>`. No other pairs are automatically
compared. Each group has its own distribution/correlation subsection. See the
[analysis catalogue](analyses.md) for compared quantities and excluded results.

## Logging and outputs

```sh
build/bin/phspPipeline run.json --output results/test --log-level INFO --progress-seconds 2 --no-plots
```

Logging level and interval propagate to all stages. `--no-plots` disables figures
throughout the run. `--log-file` changes only the pipeline log; stage logs stay next
to their own results.

```text
run/
  index.html
  pipeline.log
  pipeline_manifest.json
  preprocessing/<name>/<name>.IAEAheader/.IAEAphsp/.processing.json
  datasets/<name>/conversion/
  datasets/<name>/analysis/
  comparisons/<name>/
```

The HTML index links all stage outputs and reports. The manifest includes requested
and resolved configurations, expanded paths, states and an error message on failure.
States are `validating`, `running`, `complete`, `failed`. Active stages become failed
when an exception interrupts the run; completed stage outputs remain accessible.

Re-runs start from the beginning and replace matching output files. The pipeline
does not clean an existing directory or automatically resume a partial run.
