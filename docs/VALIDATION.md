# Validation — 2026-11-11

Version: `0.3.0.dev0` (development source update, not a published release).

## Scope

The existing optimized `src/cc` files were preserved byte for byte. This update
packages that solver with its regressions, seven portable pricing fixtures,
benchmark runner, MIT/upstream notices and install/test documentation. It changes
build/packaging and import/test failure reporting, not labeling arithmetic or
option defaults. The maintained Cython wrapper was regenerated for consistency.

Native source fingerprint (sorted filename/SHA-256 lines, then SHA-256):
`6f9df12fe9216c60b56ecaf14cb1a7de3470346cb8dca61ecb6efe12e21d9948`.

## Completed checks

| Check | Result |
|---|---|
| OpenMP CMake build, Python suite | 63 passed (22.37 s) |
| OpenMP native CTest | 5/5 passed (32.06 s) |
| No-OpenMP CMake build, Python suite | 63 passed (21.25 s) |
| No-OpenMP native CTest | 5/5 passed (30.54 s) |
| Wheel built from unpacked source archive, fresh environment | 63 passed, 780 subtests passed (22.11 s) |
| Packaged benchmark, all seven cases × four modes × three repetitions | 84 measured solves; complete outputs agree within each case |
| Source/wheel contents | Native/Python tests in source; all seven fixtures, manifest and notices in wheel |

Builds used Linux x86-64, Python 3.12.4 and GCC. The fresh runtime environment
used NetworkX 3.7, NumPy 2.5.4 and pytest 9.2.0. The wheel build exercised the
configured native-dependency downloads, including the checksummed LEMON archive,
without depending on the local preinstalled native headers. Its dynamic links
were ordinary system C/C++/OpenMP libraries, with no temporary-directory link.
It remains a platform-specific wheel, not a manylinux portability certification.

The Python suite includes independent small-graph enumeration and the original
SRI-upper-bound counterexample. Large stored fixture references are replay
oracles from the development solver, not independent proofs of optimality.
The benchmark was a functional/parity check, with some concurrent test work;
its timings are not presented as a controlled new speedup measurement.

The GitHub Actions matrix is configured for Python 3.9/3.12, OpenMP on/off, and
source-archive-to-wheel testing. That remote matrix has not been run here.
No Windows/macOS verification or binary publication is claimed.

## Usage

Run `python -m pip install ".[test]"`, then `python -m pytest -q`.
See `README.md` for CTest and `benchmarks/README.md` for reproducible benchmarks.
The source archive and Linux CPython 3.12 wheel are in the local `dist/` directory.
Existing installed environments and the SVDRP solver packages were not modified.
