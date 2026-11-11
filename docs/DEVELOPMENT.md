# Development and distribution checks

Use CMake/scikit-build through `python -m pip install .`. The old `setup.py` is
not the authoritative path. CMake regenerates the maintained Cython wrapper in
the build directory so a stale generated C++ file cannot silently be compiled.

Install tools with `python -m pip install build Cython scikit-build-core` and
create artifacts with `python -m build`. Unpack the source archive into a fresh
directory, build its wheel, install into a separate environment, and run the
tests from that unpacked archive. This checks independence from local/untracked
files. The installed existing library need not be overwritten to validate a build.

The source archive must include every Python/native regression, native/Cython
sources, build logic, fixtures, benchmark script, documentation and notices.
The wheel must include the extension, `espprc.datasets`, all eight JSON files
(seven cases plus manifest) and license notices.

Python tests cover enumeration, SRI upper bounds, bucket parity, collection
order, masks/set fallback, parallel parity and datasets. Five native targets
cover buckets, dominance, duplicate indexing, extension and joining. Run both
layers with OpenMP enabled and disabled. Explicit native checks remain active
in Release builds. A smoke test of the legacy time-window flag does not establish
a supported customer-time-window API.

`.github/workflows/tests.yml` automates packaging and both test layers on Linux.
Local checks are recorded in `VALIDATION.md`; an unrun CI matrix is not described
as passing. No workflow publishes packages.

The local wheel is platform-specific, not a repaired manylinux wheel. Binary
publication requires testing native-dependency portability and the target
platform/Python matrix. This update does not publish to PyPI or automatically
replace an installed copy.
