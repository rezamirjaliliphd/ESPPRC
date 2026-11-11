# Maintained implementation

The active API is `python/espprc/Bidirectional.pyx`, backed by
`src/cc/{bidirectional,search,labelling,digraph,preprocessing,ref_callback}.cc`.
Older lowercase/enhanced wrapper files are historical and are not compiled by
the supported CMake build.

The existing verified development work includes:

- An infinite initial incumbent bound to avoid an additive-only path value
  pruning a better full-cost path when SRI penalties are positive.
- Adaptive critical-resource buckets, conservative dominance summaries,
  extension screening, and bucket/block screening of forward/backward joins.
- Duplicate-label indexing preserving the original equality comparison.
- Optional OpenMP extension/join preparation with ordered serial insertion and
  serial fallback for unsupported or small work and no-OpenMP builds.

The November packaging work preserves solver arithmetic and option defaults.
It adds portable fixtures, replay tools, failure propagation in legacy tests,
Cython regeneration, OpenMP-on/off checks and MIT/upstream notices. Import
failures now surface the underlying extension error rather than an empty API.

See [docs/VALIDATION.md](docs/VALIDATION.md) for actual validation results.
Historical timings are observations on listed fixtures, not universal speedups.
