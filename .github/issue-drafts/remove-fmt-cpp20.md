# Remove the fmt dependency using C++20 standard formatting

## Goal

Replace the external fmt dependency with C++20 standard-library formatting on a
dedicated branch, for example `feature/remove-fmt`. Retain the C++20 language baseline.

Reducing dependencies is the motivation; no performance improvement is assumed.

## Current state

- `CMakeLists.txt` requires C++20 and finds fmt as a required package.
- `core/CMakeLists.txt` links `fmt::fmt` publicly.
- `core/include/phsp/logging.hpp` exposes `fmt::format_string` and uses `fmt::format`.
- Formatting is also used in `core/src/logging.cpp`, `conversion.cpp` and `processing.cpp`.
- The Conda environment and documentation list fmt as a dependency.

`std::format` and `std::format_string` belong to C++20 and cover the formatting APIs
used here, subject to verification against the supported standard libraries. Keep
the existing logger's synchronized file and console sinks.

## Proposed implementation stages

1. **Establish toolchain support.** Check compiler and standard-library support for
   `<format>` in C++20 mode with compile/link probes and feature-test macros.
   Document the tested compiler and standard-library versions; accepting the language
   flag alone does not demonstrate library support.
2. **Check ROOT integration.** Validate the supported ROOT/Cling and RDataFrame JIT
   paths with the chosen toolchain. Preserve the existing standard configuration
   and document any concrete compatibility limitations.
3. **Replace formatting.** Migrate the public logger templates and remaining fmt
   calls to standard equivalents. Preserve format-string validation, numeric padding,
   precision, exceptions, log levels, UTC timestamps and progress output. Preserve
   synchronization across file and console writes.
4. **Remove the dependency.** Once replacements work, remove fmt includes, CMake
   discovery/linking, the Conda dependency and outdated build instructions. Check
   installed public-header consumers as well as the six applications.
5. **Update build guidance.** Adjust CI and documented compiler/library requirements
   if the validated `<format>` implementation requires it, keeping C++20 as the standard.

## Acceptance criteria

- [ ] Changes are developed on a separate branch and reviewed before merging.
- [ ] `CMAKE_CXX_STANDARD` remains 20.
- [ ] Compiler, standard library and ROOT/Cling compatibility are demonstrated.
- [ ] A clean supported environment builds all six applications without external fmt.
- [ ] Existing conversion, analysis, comparison, history, pipeline and logging tests pass.
- [ ] Runtime RDataFrame JIT and rendering paths are exercised, not only compilation.
- [ ] Logger formatting and concurrent output behaviour are verified.
- [ ] Installed public headers no longer require fmt.
- [ ] Documentation and CI reflect the tested minimum requirements.

## Out of scope

Language-standard upgrades, logger sink redesign, physics changes, reference-plane
conventions, ROOT data schemas and CLI/JSON behaviour are outside this issue's scope.
Do not change the published v0.3.0 tag.

## References

- [WG21 text-formatting proposal](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2019/p0645r10.html)
- [ROOT standard and interpreter compatibility](https://root.cern.ch/manual/integrate_root_into_my_cmake_project/)
