"""Reproducible full-solve timings; correctness is checked before reporting time."""
import argparse
import csv
import gc
import hashlib
import json
import platform
import statistics
import sys
import time
from pathlib import Path

import espprc
from espprc.datasets import available_instances, load_instance


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cases", nargs="+", choices=available_instances(),
                        default=list(available_instances()))
    parser.add_argument("--repeats", type=int, default=5)
    parser.add_argument("--threads", type=int, default=4)
    parser.add_argument("--output", type=Path, default=Path("benchmark-results"))
    args = parser.parse_args()
    if args.repeats < 1 or args.threads < 1:
        parser.error("repeats and threads must be positive")
    args.output.mkdir(parents=True, exist_ok=True)
    modes = {
        "serial": dict(enable_parallel=False, parallel_joining=False, num_threads=1),
        "extension": dict(enable_parallel=True, parallel_joining=False, num_threads=args.threads),
        "joining": dict(enable_parallel=False, parallel_joining=True, num_threads=args.threads),
        "both": dict(enable_parallel=True, parallel_joining=True, num_threads=args.threads),
    }
    records = []
    for case in args.cases:
        graph, options, meta = load_instance(case)
        reference = espprc.solve_espprc(graph, **dict(options, **modes["serial"]))
        digest = hashlib.sha256(json.dumps(reference, sort_keys=True).encode()).hexdigest()
        # Warm up all modes. There is no time-limit/threshold early termination.
        for mode in modes.values():
            if espprc.solve_espprc(graph, **dict(options, **mode)) != reference:
                raise RuntimeError(f"Output mismatch in warmup: {case}")
        for repeat in range(args.repeats):
            order = list(modes)
            offset = repeat % len(order)
            order = order[offset:] + order[:offset]
            if repeat % 2:
                order.reverse()
            for mode in order:
                gc.collect()
                start = time.perf_counter()
                result = espprc.solve_espprc(graph, **dict(options, **modes[mode]))
                seconds = time.perf_counter() - start
                if result != reference:
                    raise RuntimeError(f"Output mismatch: {case}, {mode}, repeat {repeat}")
                records.append(dict(case=case, mode=mode, repeat=repeat,
                                    seconds=seconds, cost=result["cost"],
                                    output_sha256=digest, fixture_sha256=meta["sha256"]))
        print(case, {m: round(statistics.median(r["seconds"] for r in records
                     if r["case"] == case and r["mode"] == m), 6) for m in modes}, flush=True)
    with (args.output / "timings.csv").open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(records[0]))
        writer.writeheader()
        writer.writerows(records)
    module_path = Path(espprc.Bidirectional.__file__)
    info = dict(python=sys.version, platform=platform.platform(),
                package_version=espprc.__version__, extension=str(module_path),
                extension_sha256=hashlib.sha256(module_path.read_bytes()).hexdigest(),
                repeats=args.repeats, requested_threads=args.threads,
                note="Thread flags fall back to serial in a build without OpenMP. "
                     "Times include graph conversion and solve, exclude loading and profiling.")
    (args.output / "environment.json").write_text(json.dumps(info, indent=2) + "\n")


if __name__ == "__main__":
    main()
