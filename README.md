# ESPPRC

**An open-source C++17/Python solver for the Elementary Shortest Path Problem with Resource Constraints (ESPPRC).**

The Elementary Shortest Path Problem with Resource Constraints is a fundamental subproblem in many routing and scheduling applications, particularly in column generation and branch-and-price algorithms.

While finding a shortest path is relatively straightforward, the problem becomes much harder when paths must satisfy resource limits, avoid repeated vertices, and account for additional costs introduced by the optimization model.

This repository provides a bidirectional labeling solver for ESPPRC, with a focus on computational efficiency, flexibility, and reproducibility.

The project builds on the C++ bidirectional labeling implementation developed by David Torres Sanchez for [cspy](https://github.com/torressa/cspy). It extends that implementation with adaptive resource buckets, more efficient label joining, subset-row inequality (SRI) cost handling, and optional parallel preparation of labeling operations.

The solver is written in **C++17**, with a **Cython interface** that makes it accessible from Python.

### What does the solver offer?

- **Bidirectional labeling:** Explores paths from both ends of the graph, joining compatible labels to construct solutions.
- **Adaptive resource buckets:** Organizes labels by resource consumption to improve dominance checks and extension screening.
- **Efficient label management:** Uses conservative dominance rules, duplicate-label indexing, and join screening to reduce unnecessary computations.
- **SRI-aware pricing:** Supports additional floor-based costs associated with subset-row inequalities in column generation.
- **Optional parallelization:** Uses OpenMP to prepare label extensions and joining operations when beneficial.
- **Python integration:** Works with NetworkX graphs through a Cython wrapper.
- **Reproducible experiments:** Includes regression tests, exhaustive small-instance verification, and seven bundled pricing instances.

Serial execution is the default. Parallelization can improve performance on some instances, although its overhead may outweigh the benefits on smaller problems.

## Watch the problem explained

[![ESPPRC: The cheapest route has constraints](docs/video/media/poster.png)](docs/video/media/espprc_showcase.mp4)

I also included a short animation illustrating why resource constraints make shortest-path problems challenging and how bidirectional labeling, dominance, and resource buckets help solve them.

[Watch the animation](docs/video/media/espprc_showcase.mp4)

The [animation source, captions, and rendering instructions](docs/video/README.md) are available for anyone interested in adapting the visualization.

## Installation

ESPPRC requires Python 3.9 or newer, a C++17-compatible compiler, and CMake 3.22 or newer.

Clone the repository and install the Python package:

```bash
git clone https://github.com/rezamirjaliliphd/ESPPRC.git
cd ESPPRC
python -m pip install ".[test]"
```

The build uses `scikit-build-core` and regenerates the Cython extension from the included `.pyx` and `.pxd` files.

CMake searches for LEMON, fmt, and spdlog. If they are unavailable locally, the build can download them automatically, so network access may be required for the initial installation.

For Conda users:

```bash
conda env create -f environment.yml
```

### Using system dependencies on Ubuntu/Debian

If you prefer to install the C++ dependencies yourself:

```bash
sudo apt-get install g++ cmake liblemon-dev libfmt-dev libspdlog-dev

python -m pip install ".[test]" \
  --config-settings=cmake.define.ESPPRC_VENDOR_DEPS=OFF
```

Custom dependency paths can be provided through `CMAKE_PREFIX_PATH`, `LEMON_INCLUDE_DIR`, and `LEMON_LIBRARY` using the corresponding `cmake.define` settings.

To build without OpenMP:

```bash
python -m pip install . \
  --config-settings=cmake.define.ESPPRC_ENABLE_OPENMP=OFF
```

Parallel options automatically fall back to serial execution when OpenMP is unavailable.

The project has been verified locally on Linux. The included CI workflow covers Linux Python versions and configurations with and without OpenMP. Windows and macOS have not been release-tested.

## Getting started

The Python interface accepts directed NetworkX graphs. Each edge must provide a cost (`weight`) and a vector of resource consumptions (`res_cost`).

Here's a simple example:

```python
import networkx as nx
from espprc import solve_espprc

graph = nx.DiGraph()

graph.add_edge(
    "start", "customer",
    weight=-3, res_cost=[2, 1]
)
graph.add_edge(
    "customer", "end",
    weight=1, res_cost=[2, 0]
)
graph.add_edge(
    "start", "end",
    weight=0, res_cost=[1, 0]
)

result = solve_espprc(
    graph,
    min_res=[0, 0],
    max_res=[5, 2],
    cg_cut_pi=[0, 0],
    source="start",
    sink="end"
)

path = [
    result["id_to_node"][i]
    for i in result["path"]
]

print(path, result["cost"])
# ['start', 'customer', 'end'], -2.0
```

The solver internally assigns integer IDs to vertices. The returned `id_to_node` mapping can be used to recover the original NetworkX labels.

An empty returned path means the search found no path. The associated cost should not be interpreted as an optimality or infeasibility certificate.

### Resource constraints and pricing costs

Resources are additive along each path.

For applications involving subset-row inequalities, the total path cost takes the form:

\[
c(P)=\sum_{e\in P}w_e+
\sum_r\pi_r\left\lfloor R_r(P)\right\rfloor
\]

where \(w_e\) denotes the edge cost, \(R_r(P)\) is the accumulated value of resource \(r\), and \(\pi_r\) is its supplied cost coefficient.

The SRI regression tests use nonnegative `cg_cut_pi` coefficients. These represent signed cost penalties rather than the original nonpositive dual values from the master problem.

**A note on exactness:** The validated pricing setting assumes nonnegative resource increments and an increasing, bounded critical resource. Exactness has not been established for arbitrary signed floor-cost coefficients or arbitrary resource-extension functions.

### Interface details

A few options are worth keeping in mind:

- `elementary=True` prevents repeated vertices. With `elementary=False`, K-cycle elimination provides a relaxation rather than a guarantee of elementary paths.
- `get_all_path=True` returns paths collected during the search, not necessarily every feasible path, since dominance may eliminate alternatives.
- `time_limit` and `threshold` can terminate the search early. The current interface does not return a certified bound or termination-status field. Omit these settings when a completed solve is required.
- `use_bitwise=True` supports graphs with at most 64 vertices. For larger graphs, use set-based mode.
- The legacy `use_time_windows` option does not load node-specific time windows through this wrapper and should not be treated as a verified customer-time-window implementation.

## Performance options

Several optimization features are enabled by default:

| Option | Purpose |
|---|---|
| `adaptive_buckets` | Adaptive resource-bucket management |
| `bucket_dominance` | Bucket-based dominance screening |
| `bucket_extension` | Screening during label extension |
| `bucket_joining` | Screening during bidirectional label joining |
| `bucket_width` | Initial critical-resource bucket width (default: 5) |

Parallel execution is optional:

- `enable_parallel=True` enables parallel preparation of extensions.
- `parallel_joining=True` enables parallel preparation of joins.
- `num_threads` controls the requested thread count.

Dominance and label insertion retain their serial ordering.

For performance analysis, `profile_dominance`, `profile_extension`, and `profile_joining` provide stage-level counters and timing information. Profiling should be disabled for runtime comparisons.

## Datasets and benchmarks

The repository includes seven portable, checksummed JSON pricing instances that can be loaded directly from Python.

```python
from espprc import solve_espprc
from espprc.datasets import (
    available_instances,
    load_instance
)

graph, options, metadata = load_instance("drone20")

result = solve_espprc(graph, **options)

print(available_instances())
print(result["cost"])
```

`drone20` is a frozen, generated research pricing input. The other six instances are synthetic test cases.

These instances are included in the Python wheel, so running the examples does not require a separate research-code checkout or temporary input files.

### Running benchmarks

```bash
python benchmarks/run.py \
  --repeats 5 \
  --threads 4 \
  --output benchmark-results
```

The benchmark runner compares four execution configurations: serial, parallel extension preparation, parallel joining preparation, and combined parallel preparation.

It verifies the resulting outputs, rotates the timing order to reduce measurement bias, and saves raw CSV timings along with environment information.

Historical development measurements are available in `benchmarks/history/`.

For additional details on the instances and experimental procedure, see [benchmarks/README.md](benchmarks/README.md).

## Testing and verification

The repository includes Python regression tests, native C++ tests, and exhaustive checks on small graphs.

Run the Python tests with:

```bash
python -m pytest -q
```

To build and run the native tests:

```bash
python -m pip install Cython

cmake -S . -B build-tests \
  -DESPPRC_BUILD_TESTS=ON \
  -DCMAKE_BUILD_TYPE=Release \
  -DPython3_EXECUTABLE="$(python -c 'import sys; print(sys.executable)')"

cmake --build build-tests --parallel 2

ctest --test-dir build-tests --output-on-failure
```

The same tests can be repeated in a fresh build directory with `-DESPPRC_ENABLE_OPENMP=OFF` to verify serial execution.

Native tests use explicit checks that remain active in Release builds, and Python assertion failures are propagated rather than silently ignored.

The exhaustive small-graph tests independently check optimality in their supported setting. The recorded research outputs provide additional regression and replay checks.

For more information on validating source builds and wheels, see [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md).

## Research background and acknowledgments

ESPPRC grew out of my research on exact optimization algorithms for vehicle and drone routing, where solving resource-constrained shortest-path problems efficiently is an important part of column generation.

The underlying bidirectional labeling solver and foundational label-dominance implementation originate from **cspy**, developed by **David Torres Sanchez**. The extensions in this repository build on that work; they do not imply authorship of the original algorithms or endorsement by the upstream authors.

**Relevant references**

1. Torres Sanchez, D. (2020). *cspy: A Python package with a collection of algorithms for the (Resource) Constrained Shortest Path problem*. Journal of Open Source Software, 5(49), 1655. [https://doi.org/10.21105/joss.01655](https://doi.org/10.21105/joss.01655)

2. Jepsen, M., Petersen, B., Spoorendonk, S., & Pisinger, D. (2008). *Subset-Row Inequalities Applied to the Vehicle-Routing Problem with Time Windows*. Operations Research, 56(2), 497–511. [https://doi.org/10.1287/opre.1070.0449](https://doi.org/10.1287/opre.1070.0449)

The SRI implementation supports pricing costs using supplied coefficients. It does not implement the master problem or a cut-separation algorithm.

The explanatory animation was created using [3Blue1Brown's ManimGL](https://github.com/3b1b/manim). Its machine-readable references are available in [docs/video/references.bib](docs/video/references.bib).

## License

The original cspy MIT license and copyright are retained in [licenses/cspy.txt](licenses/cspy.txt).

New contributions to this repository are also MIT licensed. Additional upstream and dependency notices are provided in [NOTICE](NOTICE) and [licenses/](licenses/).
