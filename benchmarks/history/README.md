# Historical measurements

`2026-10-combined-summary.json` preserves the October development comparison:
`pre_index` was the build before the duplicate-label index and `current` was the
then-current optimized build. Each reported median has nine samples. These are
historical observations on seven selected fixtures, not a performance promise
or a comparison with the earliest implementation. Neither old binary is needed
to run the current benchmarks. Use `../run.py` for new timings and keep its
`environment.json` with the CSV. Full outputs were checked for equality in the
original comparison. Timing alone never establishes correctness.
