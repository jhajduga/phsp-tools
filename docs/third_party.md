# Third-party components and notices

[Back to README](../README.md)

Project-owned code is distributed under the [MIT License](../LICENSE).
The bundled components below retain their own notices and terms.

## IAEA reference implementation

`core/vendor/iaea/` contains the reference phase-space reader, writer and structures.
Source headers retain International Atomic Energy Agency copyright (2006), permission
to use/copy/modify/distribute and its conditions/disclaimer. The retained notice is
[core/vendor/iaea/LICENSE](../core/vendor/iaea/LICENSE).

Local changes recognize negative-zero history markers using signbit through the C++
`<cmath>` header, fix character
array deletion with delete[], return the correct reused source-handle ID, and remove
unconditional informational stdout messages in favour of wrapper logging. Wrappers
and provenance are implemented in `core/src/conversion.cpp` and `processing.cpp`.

## Earlier cutter and merger

The applications integrate the functionality of Jakub Hajduga's
[phspCutter](https://github.com/jhajduga/phspCutter) and
[phspMerger](https://github.com/jhajduga/phspMerger), rewritten for this core and JSON
pipeline. The cutter's MIT notice is retained in
[core/vendor/legacy/LICENSE.phspCutter](../core/vendor/legacy/LICENSE.phspCutter).
See [integration and revision notes](legacy_tools.md).

## Separately installed libraries

ROOT, fmt and nlohmann/json are discovered by CMake and are not vendored. Their own
licence notices accompany their distributions. The project no longer contains
loguru/LogSvc copies or requires Abseil for queue synchronization.
