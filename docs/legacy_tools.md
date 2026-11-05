# Integration of earlier tools

[Back to README](../README.md)

The following Jakub Hajduga repositories supplied the original workflows:

| Repository | Reviewed revision | Integrated application |
|---|---|---|
| [phspCutter](https://github.com/jhajduga/phspCutter) | [`7895ddb3696315615f9157c70067361f630755fb`](https://github.com/jhajduga/phspCutter/tree/7895ddb3696315615f9157c70067361f630755fb) | `app/phspCutter.cpp` + shared `phsp::cut`. |
| [phspMerger](https://github.com/jhajduga/phspMerger) | [`a501c83bcd02e6a9b8642cbe5a6b4512c712dbe7`](https://github.com/jhajduga/phspMerger/tree/a501c83bcd02e6a9b8642cbe5a6b4512c712dbe7) | `app/phspMerger.cpp` + shared `phsp::merge`. |

The functionality is integrated into the existing C++ build rather than maintained
as separate projects. Python interfaces are not included; JSON pipelines invoke the
same public API as the standalone executables.

## Preserved behaviour

The cutter's default aperture is −7..7 cm in X/Y at Z=100 cm, accepting +Z motion
and retaining the source positions. Merger input order is preserved; both explicit
`--output` and older last-positional-output forms are accepted.

## Corrections and extensions

Both tools now read the final record, validate sizes/layouts, log periodic progress,
retain history boundaries and write processing provenance. Cutter preserves the
original primary-history denominator instead of replacing it with accepted-record
count. Merger requires compatible extra schemas instead of padding unknown fields;
`--drop-extras` is an explicit alternative that still retains history increments.

Cut bounds, target Z, known origin offset and projected-coordinate output are
configurable. Prefix/incomplete runs preserve partial groups even at internal merge
boundaries. Synthetic tests exercise these behaviours and merge → cut → convert →
analyze through JSON. [CLI and scientific semantics](processing.md).

## Notices

The cutter's MIT licence, copyright 2025 Jakub Hajduga, is retained at
[LICENSE.phspCutter](../core/vendor/legacy/LICENSE.phspCutter). The reviewed merger
README identifies MIT, but that checkout has no standalone licence file. Its
functionality was reimplemented against this project's existing IAEA wrapper; no
Python UI or additional copy of its IAEA library was imported. Existing IAEA notices
remain preserved. [Third-party details](third_party.md).
