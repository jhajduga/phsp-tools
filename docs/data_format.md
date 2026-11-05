# Data format, histories and source integrity

[Back to README](../README.md)

## IAEA input

Applications accept an unpacked basename or `.IAEAheader` / `.IAEAphsp` path.
Both files must share a basename. The reference IAEA reader decodes stored and
constant coordinates/directions according to `RECORD_CONTENTS` and `RECORD_CONSTANT`.

| Header field | Use |
|---|---|
| `$RECORD_CONTENTS` | Stored components and extra-field counts. |
| `$RECORD_CONSTANT` | Values omitted from records, including constant Z if declared. |
| `$RECORD_LENGTH` | Declared record bytes, checked against the field layout. |
| `$CHECKSUM` | Declared data-file size in bytes; this is not a CRC. |
| `$PARTICLES` | Declared stored-record count. |
| `$ORIG_HISTORIES` | Original primary histories, possibly including histories without scored particles. |
| `$BYTE_ORDER` | Byte order; mismatch fails without automatic endian conversion. |

Geometric header ranges describe the scoring surface. Stored per-record Z takes
precedence over a statistical range; constant Z is materialized from the header.
See [reference geometry](geometry.md).

## ROOT TTree `phsp`

Each row is one scored particle record, not necessarily an independent particle/history.

| Branch | ROOT type / unit | Meaning |
|---|---|---|
| `record_index` | Long64_t, from 0 | Record index in the input IAEA file. |
| `particle_type` | Int_t | IAEA: 1 photon, 2 electron, 3 positron, 4 neutron, 5 proton. |
| `energy_MeV` | Float_t / MeV | Nonnegative kinetic energy. |
| `weight` | Float_t | Statistical weight. |
| `pos_x_cm`, `pos_y_cm`, `pos_z_cm` | Float_t / cm | Original scoring position. |
| `dir_u`, `dir_v`, `dir_w` | Float_t | Direction cosines; dir_w sign decoded from signed IAEA particle type. |
| `dir_theta_rad`, `dir_phi_rad` | Float_t / rad | Derived angles; analysis plots use degrees. |
| `radius_xy_cm` | Float_t / cm | Original sqrt(x²+y²). |
| `history_increment` | Int_t | Decoded IAEA n_stat increment. |
| `is_new_history` | Bool_t | Whether history_increment>0. |
| `history_id` | Long64_t | Cumulative sum of positive increments. |
| `history_start_known` | Bool_t | Whether a recognized history start has occurred. |
| `history_particle_index` | Long64_t, from 0 | Stored-record order within the group. |
| `history_integrity_known` | Bool_t | False in a group flagged incomplete by cut/merge provenance. |
| `extraF`, `extraI` | Float_t / Int_t arrays | Optional extras, when `--extras` is enabled. |

Analysis expects this scalar schema. It does not infer PDG-to-IAEA mappings or
convert unknown source units to MeV/cm. Projection changes analysis coordinates
only; the input tree remains unchanged.

## Reconstructing histories

n_stat=0 continues the current history. A positive value begins a history and gives
the increment from the previous scored history. A plain signed-energy start means
increment 1; an IAEA extra-long of type 1 carries the explicit increment. Increments
larger than 1 reveal histories with no scored particles.

```text
history_increment:       1  0  3  1  0
history_id:              1  1  4  5  5
history_particle_index:  0  1  0  0  1
```

Histories 2 and 3 have no records here. IDs are local to the file, not original track
IDs or universal identifiers shared across sources. The converter recognizes a
negative-zero energy history marker with signbit and stores nonnegative energy.
A single reader assigns increments/IDs before parallel work. Ordered block writing
preserves source record order.

An unmarked initial group has ID=0 and an unknown beginning. Without any recognized
starts, exact history grouping is unavailable. Adjacent rows alone do not establish
independent histories.

## `histories/histories` in analysis.root

One row describes one observed group, with:

- `history_id`, `first_record_index`, `last_record_index`;
- `n_particles` and species counts;
- `sum_energy_MeV`, `sum_weighted_energy_MeV`, `sum_weights`, `max_energy_MeV`;
- photon/electron/positron/neutron/proton energy sums;
- `complete`, eligibility for complete-history statistics.

Unknown initial groups, groups with invalid E/W, truncated final groups and groups
marked incomplete by processing provenance are ineligible. History histograms and
means use complete=true only. An analysis prefix checks the following record to
recognize a boundary. A conversion prefix conservatively leaves the last group's
ending unknown. Stride>1 omits group members and disables exact history analysis.

Scored-history aggregates retain all source species and original records within
the input prefix, independently of plane/type selection. “Length” means scored-record
multiplicity. It is not track length or cascade size; energy sums are not energy deposition.

## Conversion metadata and partial input

A JSON sidecar and the ROOT TObjString `phsp_metadata` contain `schema_version=2`,
settings, counts, the original IAEA header, extras layout and conversion provenance.
The analysis report embeds this as `conversion_metadata`.

Size/count mismatch fails by default. Explicitly process complete available records:

```sh
build/bin/phspToRootMT /data/incomplete.IAEAheader \
  --allow-incomplete --extras --output-dir results/incomplete
```

The converter uses actual bytes and verified record length, skips trailing fragments,
preserves the original header and reports `input_integrity`:

| Field | Meaning |
|---|---|
| `file_size_check_result` | IAEA check result; −3 means size mismatch. |
| `header_checksum_bytes`, `actual_size_bytes` | Declared and actual bytes. |
| `record_length_bytes` | Verified bytes per record. |
| `header_particle_records`, `available_complete_records` | Declared and physically available counts. |
| `trailing_bytes` | Bytes after the final complete record. |
| `header_consistent` | Whether sizes/counts/layout agree. |

`history.source_complete` requires consistent input, reading the whole source and
complete inherited processing provenance. Reaching the end of a truncated file
does not establish completeness. Original-history normalization requires a complete
unsampled source; limits/uncertain provenance disable it.

`allow_incomplete` never overrides a wrong layout or byte order. Conversion does not
modify source IAEA files. ROOT output is published from a `.partial` file after writing.

## Cut/merge provenance

Preparation writes an IAEA pair and `<base>.processing.json`, with source paths,
counts, options, `source_complete` and inclusive `incomplete_history_ranges` in
**output-record indices**. These ranges preserve partial groups even at an internal
merge boundary. Conversion maps them to `history_integrity_known`; analysis excludes
the affected groups from complete-history statistics.

The output header carries `PHSP_TOOLS_SOURCE_COMPLETE=0/1`. When range metadata is
needed, `PHSP_TOOLS_PROCESSING_METADATA_REQUIRED=1` makes a missing sidecar an error.
Keep all three files together. A self-consistent output byte count does not erase
incomplete upstream provenance. [Preparation semantics](processing.md).

## Older ROOT files

An older converter stored n_stat in `history_id/I` and could reorder blocks. Such a
file still supports particle analysis, but exact histories require reconversion to
the current schema. Do not group old n_stat values as unique IDs or assume source
order. Analysis reports lacking analysis-plane metadata also need regeneration for
current comparisons. Older ordered schema-v2 files without `history_integrity_known`
remain supported using their conversion completeness metadata.
