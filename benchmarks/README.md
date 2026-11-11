# Pricing fixtures and benchmark protocol

Seven JSON inputs live in `python/espprc/data/` and ship in source distributions
and wheels. `espprc.datasets.load_instance` checks SHA-256, preserves insertion
order, and returns a fresh graph/options dictionary. No pickle, network or
other checkout is required.

| Name | Origin | Nodes | Arcs | Recorded cost |
|---|---|---:|---:|---:|
| drone20 | Frozen generated SVDRP pricing input, October 2026 | 22 | 420 | -75.1263157894737 |
| sri20 | Synthetic SRI graph, seed 17 | 22 | 421 | -72 |
| sri14 | Synthetic SRI graph, seed 17 | 16 | 211 | -66 |
| sri14_sets | Same active graph, extra isolated nodes and large labels | 81 | 211 | -66 |
| wide40 | Synthetic wide graph, shallow resource cap, seed 17 | 42 | 1641 | -38 |
| wide40_deep | Same graph with larger resource caps | 42 | 1641 | -59 |
| truck | Synthetic station graph, seed 20261029 | 14 | 157 | -205 |

These are regression/research inputs, not field data or a representative public
routing benchmark suite. The synthetic SRI recipe is `graph_case` in
`test_bucket_joining.py`. Truck resources are intermediate stops and travel time,
with limits 10 and 480; travel time is critical. The frozen graph and signed costs
suffice for the drone replay; the original master/generator is not required.

Stored references come from the tested development library and are parity
oracles, not independent proofs of optimality. Small exhaustive checks are in
`test_label_joining.py`, `test_sri_upper_bound.py` and the bucket regressions.

Run `python benchmarks/run.py --repeats 5 --threads 4 --output benchmark-results`.
All modes warm up first. Timing includes graph conversion and solving, excluding
fixture loading, imports and profiling. Mode order rotates; complete results must
match serial. No time limit or threshold is used. The CSV contains individual
samples, graph/output hashes and costs. `environment.json` identifies Python,
platform, extension path/hash and requested threads. No-OpenMP builds fall back
to serial. Historical medians are kept separately in `history/`.

For before/after comparisons use separate environments, identical fixtures,
CPU affinity, compiler settings and repetition count. Inspect variability;
there is intentionally no flaky timing threshold in the correctness suite.
