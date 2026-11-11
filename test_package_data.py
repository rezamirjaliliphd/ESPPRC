"""Installed-wheel data and complete-solve replay checks."""
import math

import pytest

from espprc import solve_espprc
from espprc.datasets import available_instances, load_instance


@pytest.mark.parametrize("name", available_instances())
def test_bundled_reference_and_parallel_parity(name):
    graph, options, meta = load_instance(name)
    reference = solve_espprc(graph, **options)
    path = [reference["id_to_node"][i] for i in reference["path"]]
    assert path == meta["reference"]["path"]
    assert reference["cost"] == pytest.approx(meta["reference"]["cost"], abs=1e-9)
    assert reference["resources"] == pytest.approx(meta["reference"]["resources"], abs=1e-9)
    edges = [graph[u][v] for u, v in zip(path, path[1:])]
    resources = [sum(e["res_cost"][i] for e in edges) for i in range(len(options["max_res"]))]
    expected_cost = sum(e["weight"] for e in edges) + sum(
        pi * math.floor(r) for pi, r in zip(options["cg_cut_pi"], resources))
    assert reference["cost"] == pytest.approx(expected_cost, abs=1e-9)
    assert len(path) == len(set(path))
    assert all(lo - 1e-9 <= r <= hi + 1e-9 for r, lo, hi in
               zip(resources, options["min_res"], options["max_res"]))
    for extra in ({"enable_parallel": True, "num_threads": 2},
                  {"parallel_joining": True, "num_threads": 2}):
        assert solve_espprc(graph, **dict(options, **extra)) == reference


def test_loading_is_independent_and_unknown_names_are_rejected():
    graph, options, _ = load_instance("truck")
    graph.clear()
    options["max_res"][0] = 0
    again, original, _ = load_instance("truck")
    assert len(again) == 14
    assert original["max_res"][0] == 10
    with pytest.raises(ValueError, match="Unknown instance"):
        load_instance("../README")
