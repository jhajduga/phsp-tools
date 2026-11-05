# Build and troubleshooting

[Back to README](../README.md)

## Dependencies

| Component | Requirement |
|---|---|
| Compiler | C++20, standard threads and stop tokens |
| CMake | 3.20 or newer |
| ROOT | 6.34 or newer: RIO, Tree, TreePlayer, Hist, Graf, Gpad, ROOTDataFrame, Matrix |
| fmt | CMake package `fmt` |
| JSON | CMake package `nlohmann_json` |
| Ninja | Required by the supplied presets; another generator may be selected manually |

The reference IAEA reader is included in `core/vendor/iaea`. Abseil and loguru are
not required. Project queues and logging use standard C++ synchronization.

## Existing ROOT installation

Activate your ROOT environment, then:

```sh
cmake --preset default
cmake --build --preset default
ctest --preset default
```

For dependencies in a nonstandard prefix:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/path/to/dependencies
cmake --build build -j 4
ctest --test-dir build --output-on-failure
```

## Conda, Linux x86_64

```sh
conda env create -f environment.yml
conda activate phsp-tools
export CPLUS_INCLUDE_PATH="$CONDA_PREFIX/x86_64-conda-linux-gnu/sysroot/usr/include${CPLUS_INCLUDE_PATH:+:$CPLUS_INCLUDE_PATH}"
cmake --preset conda
cmake --build --preset conda
ctest --preset conda
```

The preset selects the active environment's compiler and sysroot include directory.
This avoids mixing a system compiler's headers with a different libstdc++ used by ROOT.
Preset environment settings apply to CMake/build/test processes; the export above
also covers directly launched CLI programs and ROOT macros.

For other platforms use the default preset with a compiler matching your ROOT installation.
Local validation uses Linux x86_64, ROOT 6.34.04 and GCC 13.3.

## Binaries and installation

All six applications are in `build/bin/`. Each supports `--help`.

```sh
cmake --install build --prefix /path/to/install
```

Installation includes binaries, the core library, public headers and retained IAEA/legacy notices.
ROOT/fmt and their runtime environment remain external dependencies. Source paths
are not required to run an installed application.

## Tests

Tests create small synthetic IAEA/ROOT fixtures in the build tree. They cover weights,
ordering, negative-zero history markers, limits, incomplete sources, known comparison
distances, live logging, reference-plane geometry, per-species reports, cutter/merger
history preservation and JSON preparation pipelines. No real PHSP files are downloaded
or modified. Test artifacts can be removed with the build directory.

## Troubleshooting

| Symptom | Check |
|---|---|
| Cling cannot find `assert.h` | Activate the correct environment and use its existing sysroot include directory as above. |
| Undefined `std::...` symbols while linking | Reconfigure in a fresh build directory with the compiler/runtime matching ROOT. |
| IAEA `res=-3` | Header size/count mismatch; inspect the diagnostic and use `--allow-incomplete` only to process the available complete records. |
| Missing history analysis | Check the report reason: old schema, stride, missing markers or disabled history pass. |
| Different comparison bins | Use identical energy/XY bounds and bin counts. |
| Different analysis planes | Regenerate both analyses with the same target; default is 100 cm. |
| No rays reach the default target | Check source Z, +Z convention and a known origin offset; choose an appropriate explicit target. |
| A few seconds at 0% | ROOT initialization/JIT may still be running; heartbeat messages identify the active stage. |
| Large overflow | Increase histogram ranges before interpreting clipped distributions. |
| Missing required `.processing.json` | Copy it with the processed IAEA pair; it carries internal incomplete-history boundaries. |

Invalid options and failures return a nonzero process exit code.
