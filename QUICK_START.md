# Quick start

Install from this checkout into your chosen Python environment:

```bash
python -m pip install ".[test]"
python -m pytest -q
```

Solve a bundled pricing fixture:

```python
from espprc import solve_espprc
from espprc.datasets import load_instance
graph, options, metadata = load_instance("truck")
result = solve_espprc(graph, **options)
print([result["id_to_node"][i] for i in result["path"]], result["cost"])
```

Run the benchmark with `python benchmarks/run.py --repeats 5 --threads 4`.
See [README.md](README.md) for prerequisites, assumptions and option semantics.
This source-install workflow does not assume a published PyPI release.
