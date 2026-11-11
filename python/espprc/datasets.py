"""Portable pricing fixtures; no pickle, external repository, or download needed."""
import hashlib
import json
from importlib import resources

import networkx as nx


def available_instances():
    """Return the names of the bundled regression/benchmark instances."""
    return tuple(sorted(_manifest()))


def _manifest():
    return json.loads(resources.files("espprc").joinpath("data", "manifest.json").read_text())


def load_instance(name):
    """Return a fresh ``(graph, solve_options, metadata)`` for a bundled name.

    ``metadata['reference']`` records a development-solver replay, not an
    independent proof of optimality. Graph insertion order is preserved because
    the solver's internal node IDs and tie-breaking depend on it.
    """
    manifest = _manifest()
    if name not in manifest:
        raise ValueError(f"Unknown instance {name!r}; choose from {tuple(sorted(manifest))}")
    entry = manifest[name]
    raw = resources.files("espprc").joinpath("data", entry["file"]).read_bytes()
    if hashlib.sha256(raw).hexdigest() != entry["sha256"]:
        raise ValueError(f"Fixture checksum mismatch: {name}")
    data = json.loads(raw)
    if data["schema_version"] != 1:
        raise ValueError("Unsupported fixture schema")
    graph = nx.DiGraph()
    graph.add_nodes_from(data["nodes"])
    for tail, head, weight, consumption in data["edges"]:
        graph.add_edge(tail, head, weight=weight, res_cost=consumption)
    metadata = {key: data[key] for key in ("name", "provenance", "reference")}
    metadata["sha256"] = entry["sha256"]
    return graph, data["options"], metadata
