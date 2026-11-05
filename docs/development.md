# Architecture and development

[Back to README](../README.md)

## Responsibilities

| Location | Responsibility |
|---|---|
| `app/` | Six CLI entry points, help, option parsing and exit codes. |
| `core/include/phsp/` | Public options and C++ functions. |
| `core/src/` | Preparation, conversion, analysis, comparison, pipeline and logging. |
| `core/vendor/iaea/` | Reference IAEA implementation with isolated build settings. |
| `core/tests/` | Synthetic fixtures and independent expected results. |
| `examples/` | Adaptable JSON pipelines and ROOT macros. |

The CMake target `phsp::core` exposes:

```cpp
#include <phsp/processing.hpp>
#include <phsp/conversion.hpp>
#include <phsp/analysis.hpp>
#include <phsp/comparison.hpp>
#include <phsp/pipeline.hpp>
```

`cut`, `merge`, `convert`, `analyze`, `compare` and `runPipeline` accept option structs
and return an output path. Failures throw exceptions; each CLI logs the reason and
returns exit code 1.

```cpp
phsp::AnalysisOptions options;
options.input = "beam.root";
options.output = "results/beam";
options.threads = 4;
options.project_z = 100.0;
const auto report = phsp::analyze(options);
```

```cmake
target_link_libraries(my_application PRIVATE phsp::core)
```

## Preparation and conversion

Cutter/merger use the same reference IAEA API, normalize the history-increment extra
layout and preserve compatible opaque extras. Staged output includes a processing
sidecar for source completeness and partial-history ranges. The header is published
last. The IAEA API has global source handles; preparation is sequential.

One conversion reader assigns record/history identifiers before dispatching bounded
blocks to workers. Results publish in block order through a single ROOT writer.
Mutexes and condition variables use the standard library. Byte/layout checks precede
reads; no source data is read beyond its last complete record. `.partial` outputs
are published only after successful writing. Input IAEA files are never modified.

## Analysis

1. Configure ROOT RDataFrame and source-prefix/type/quality selection.
2. Define common-plane coordinates and forward-time ray acceptance.
3. Evaluate shared counts/ranges and book lazy histogram/profile actions.
4. Reduce per-slot moments with Welford/Chan and write ROOT objects/figures.
5. Optionally aggregate original scored histories sequentially in source order.
6. Write English reports and the RDF graph.

Projected coordinates are doubles. History grouping uses original records and
optional `history_integrity_known`, independently of plane/type selection. Histogram
storage is binned; history aggregation retains only the current group, not all
particles in memory.

ROOT owns process-global graphics, interpreter and implicit-MT state. Pipeline stages
run sequentially. Do not run simultaneous analyze/compare calls in different threads
of one process; use separate processes for independent runs.

## Logging and progress

`Logger` synchronizes its console and per-run file. `Progress` maintains slot counters
and a heartbeat thread, avoiding a shared lock per particle. It also reports stages
waiting for ROOT initialization/JIT. Give every stage a name, scan target and explicit
completion. A scan's target is input records, not accepted particles. Keep INFO useful
and reserve object-level details for DEBUG.

## Extending analysis

Book new histograms before the main RDF loop to avoid unnecessary passes over large
files. Write them in the correct species group and document units, weights, projection
selection and overflow in [the catalogue](analyses.md). State incomplete-group and
correlation assumptions for history analyses.

Tests should have independently known results: weight sums, small-sample moments,
known ray intersections or distribution distances, or histories spanning block/merge
boundaries. Fixtures do not require external gigabyte-scale PHSP files. Existing
checks include identical/disjoint distributions, geometry, species figures, constant
Z headers, final-record preservation, schema mismatch, partial groups and JSON preparation.

Public option changes must be reflected in all affected CLI help and pipeline keys,
not just an API function. Documentation examples must use real supported options.

## Formatting and CI

Use `.clang-format` for project-owned C++; do not reformat `core/vendor`.
Legacy IAEA headers define min/max macros: keep them in their own include group and
undefine the macros immediately afterwards. Use the compiler matching the ROOT ABI.

`.github/workflows/ci.yml` provisions Conda, builds and runs CTest on push/PR without
external data or secrets. The Conda configure/build presets clear `CPLUS_INCLUDE_PATH`
to preserve GCC's standard-header order; the test preset supplies it only at runtime
for ROOT's interpreter. Local test success is not a remote GitHub Actions run.
[Build commands](building.md).

`.github/workflows/pages.yml` builds the mdBook and checks links on pull requests,
then deploys main to GitHub Pages. [Documentation workflow and local preview](documentation.md).
