# Cutter and merger

[Back to README](../README.md)

Both applications read and write IAEA phase space directly. They use the shared C++
core and logger and can also run as JSON preparation stages. No Python interface is
required. Inputs must be unpacked IAEA pairs; a basename or either file extension is
accepted. Output arguments are basenames, not output directories.

## Rectangular aperture selection

```sh
build/bin/phspCutter /data/beam results/field/beam \
  --project-z 100 --x-min -7 --x-max 7 --y-min -7 --y-max 7
```

The default reproduces the old cutter's intended selection: **−7≤X≤7 cm and
−7≤Y≤7 cm at Z=100 cm**, requiring +Z motion. It projects each ray from its actual
scoring position and accepts only forward-time intersections within the inclusive
bounds. This is a geometric rectangle selection, not transport through a collimator.

| CLI | JSON key | Default / meaning |
|---|---|---|
| `--project-z` | `project_z_cm` | 100 cm, aperture plane. |
| `--source-z-offset` | `source_z_offset_cm` | 0 cm, known Z-origin alignment before intersection. |
| `--x-min`, `--x-max` | `x_min_cm`, `x_max_cm` | −7, +7 cm. |
| `--y-min`, `--y-max` | `y_min_cm`, `y_max_cm` | −7, +7 cm. |
| `--write-projected` | `write_projected` | false: retain original X/Y/Z; true: write positions at the aperture in the reference frame. |
| `--both-directions` | `positive_z_only: false` | By default positive_z_only is true; otherwise either direction is allowed if it reaches the plane in forward time. |
| `--max N` | `max_records` | All records; a nonnegative N limits **input records scanned**, not accepted particles. |
| `--drop-extras` | `drop_extras` | false: preserve opaque extras. |
| `--allow-incomplete` | `allow_incomplete` | false: reject a size/count mismatch. |

Unless `--write-projected` is set, cutting changes which records are retained but
keeps their source coordinates. Subsequent default analysis projects those retained
rays to its own reference plane. If source-origin offsets are needed, configure them
consistently; already projected output is expressed in the reference frame and does
not need that source offset again. [Coordinate assumptions](geometry.md).

A full cut keeps `$ORIG_HISTORIES` from the original source. Rejected histories still
belong to the primary population. Positive history increments accumulate across
rejected records and are attached to the next retained record, including when the
first accepted record was originally a continuation. Length/energy of a cut history
then refer to **retained records**, not its pre-cut multiplicity.

## Concatenating independent sources

```sh
build/bin/phspMerger /data/run1 /data/run2 --output results/merged/beam
```

The older positional output syntax also works:

```sh
build/bin/phspMerger /data/run1 /data/run2 results/merged/beam
```

| CLI | JSON key | Default / meaning |
|---|---|---|
| `--drop-extras` | `drop_extras` | false: require compatible opaque extra schemas. |
| `--allow-incomplete` | `allow_incomplete` | false: reject size/count mismatches. |
| `--max-per-input N` | `max_records_per_input` | All records; limit each input to a prefix. |

Records concatenate in input order, including the final available record. The
merger sums `$ORIG_HISTORIES` for **independent runs**. Do not use it for overlapping
samples, two scoring surfaces of the same histories, or file shards whose primary
counts each describe the same run; their normalization requires a different policy.
A later nonempty input must begin with a known history boundary to prevent unrelated
groups being joined. Duplicate input paths and output aliases of an input are rejected.

Merging does not align coordinate frames, rotate directions, project positions or
reweight different machines. All stored positions remain per-record; descriptive
header fields are inherited from the first source, while the sidecar identifies all
inputs. Use compatible physical frames and source definitions. Geometric projection
for a comparison happens in analysis, not as an implicit merge operation.

## Extras and histories

By default, float extra types and non-history integer extra types must match in
number, type and order. A mismatch fails before writing. `--drop-extras` permits
different opaque schemas by removing those fields. Neither mode discards history
increments: output always reserves extra-long slot 0, type 1, for the decoded
increment. Other integer extras follow in their original relative order.

The output can therefore have a different binary record layout from an input while
preserving its values. Output stores coordinates/directions/weights explicitly.
The reference implementation's extra-field capacity still applies; if no extra-long
slot is available for a history increment, use `--drop-extras`.

Negative-zero start markers are recognized. Incomplete input/prefix groups are
recorded as partial, including boundaries **inside a merged output**. Chained cuts
and merges carry this provenance forward. Full-source primary normalization remains
disabled if any upstream source was incomplete or limited.

## Outputs and progress

```text
beam.IAEAheader
beam.IAEAphsp
beam.processing.json
beam.log
```

The processing JSON records inputs, counts, geometry/options, original histories,
source completeness and incomplete-history output-record ranges. **Keep the sidecar
with the pair**: when it is needed for history integrity, the header requires it and
later operations fail if it is missing. See [format details](data_format.md).

Data/header/sidecar are staged under `.partial` basenames and published after a
successful write, with the header last. Publication involves several renames, not
a single atomic transaction over all files. Matching outputs are replaced on re-run;
use a separate basename to preserve an earlier result. Failed operations can leave
`.partial` files for inspection. Source pairs are not modified.

The tools use sequential IAEA read/write and bounded per-record state. They emit
periodic counts, throughput and ETA using the same `--log-level`, `--log-file` and
`--progress-seconds` options as the other applications.

## JSON integration

See [examples/preprocessing.json](../examples/preprocessing.json). Ordered entries
in `preprocessing` use `operation: "merge"` or `operation: "cut"`; `@name` selects a
prior stage, and datasets can consume the resulting IAEA without intermediate manual
commands. [Pipeline reference](pipeline.md).
