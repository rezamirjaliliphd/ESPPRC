# Quick Start Guide - ESPPRC Enhanced Features

## Installation

```bash
pip install -e .
```

## Basic Usage

```python
import networkx as nx
from espprc import solve_espprc_enhanced

# Create your graph
G = nx.DiGraph()
G.add_edge(0, 1, weight=2.0, res=[1.0, 2.0])
G.add_edge(1, 2, weight=3.0, res=[2.0, 1.0])

# Define resource limits
min_res = [0.0, 0.0]
max_res = [10.0, 10.0]
cg_cut_pi = [0.0, 0.0]

# Solve
result = solve_espprc_enhanced(
    G, min_res, max_res, cg_cut_pi,
    source=0,
    sink=2
)

print(f"Path: {result['path']}")
print(f"Cost: {result['cost']}")
print(f"Resources: {result['resources']}")
```

## Feature Reference

### Elementary Paths (Default)
```python
result = solve_espprc_enhanced(
    G, min_res, max_res, cg_cut_pi,
    source=0, sink=n-1,
    elementary=True  # Default
)
```

### K-Cycle Elimination
**Recommended: k=3 for best performance/quality tradeoff**
```python
result = solve_espprc_enhanced(
    G, min_res, max_res, cg_cut_pi,
    source=0, sink=n-1,
    elementary=False,
    k_cycle_elimination=3  # Prevents cycles up to length 3
)
```

### Time Windows
Assumes first resource is time, and vertices have `tw_early` and `tw_late` attributes:
```python
result = solve_espprc_enhanced(
    G, min_res, max_res, cg_cut_pi,
    source=0, sink=n-1,
    use_time_windows=True,
    time_window_res_idx=0  # Which resource represents time
)
```

### Bitwise Elementary (Fast for Small Graphs)
**Auto-enabled for graphs with ≤64 nodes when elementary=True**
```python
result = solve_espprc_enhanced(
    G, min_res, max_res, cg_cut_pi,
    source=0, sink=n-1,
    elementary=True,
    use_bitwise=True  # Explicit enable
)
```

### Bounds Pruning
```python
result = solve_espprc_enhanced(
    G, min_res, max_res, cg_cut_pi,
    source=0, sink=n-1,
    bounds_pruning=True  # Use shortest path bounds
)
```

### Time Limits and Thresholds
```python
result = solve_espprc_enhanced(
    G, min_res, max_res, cg_cut_pi,
    source=0, sink=n-1,
    time_limit=60.0,    # Stop after 60 seconds
    threshold=100.0     # Stop if path with cost ≤100 found
)
```

### Search Direction
```python
result = solve_espprc_enhanced(
    G, min_res, max_res, cg_cut_pi,
    source=0, sink=n-1,
    direction="both"  # Options: "forward", "backward", "both"
)
```

### Get All Feasible Paths
```python
result = solve_espprc_enhanced(
    G, min_res, max_res, cg_cut_pi,
    source=0, sink=n-1,
    get_all_path=True
)

print(f"Number of feasible paths: {len(result['all_feasible_paths'])}")
for path in result['all_feasible_paths']:
    print(f"  Path: {path}")
```

## Parameter Quick Reference

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `G` | NetworkX DiGraph | Required | Graph with 'weight' and 'res' edge attributes |
| `min_res` | list[float] | Required | Minimum resource consumption |
| `max_res` | list[float] | Required | Maximum resource consumption |
| `cg_cut_pi` | list[float] | Required | Chvátal-Gomory multipliers (can be zeros) |
| `source` | node | Required | Source node |
| `sink` | node | Required | Sink node |
| `elementary` | bool | True | Enforce elementary paths (no repeated nodes) |
| `two_cycle_elimination` | bool | False | Eliminate 2-cycles |
| `k_cycle_elimination` | int | 0 | Eliminate k-cycles (0=off, 2,3,...) |
| `use_time_windows` | bool | False | Enable time window constraints |
| `time_window_res_idx` | int | 0 | Resource index for time |
| `use_bitwise` | bool | False | Use bitwise elementary (auto for small graphs) |
| `bounds_pruning` | bool | False | Use bounds pruning |
| `direction` | str | "both" | Search direction ("forward"/"backward"/"both") |
| `method` | str | "unprocessed" | Direction selection method |
| `time_limit` | float | None | Time limit in seconds |
| `threshold` | float | None | Cost threshold to stop search |
| `get_all_path` | bool | False | Return all feasible paths |
| `enable_parallel` | bool | False | Enable parallel processing |
| `num_threads` | int | 1 | Number of threads |

## Performance Tips

### For Small Graphs (≤64 nodes)
```python
result = solve_espprc_enhanced(
    G, min_res, max_res, cg_cut_pi,
    source=0, sink=n-1,
    elementary=True,
    use_bitwise=True  # 2-3x speedup!
)
```

### For Non-Elementary Paths
```python
result = solve_espprc_enhanced(
    G, min_res, max_res, cg_cut_pi,
    source=0, sink=n-1,
    elementary=False,
    k_cycle_elimination=3  # Optimal tradeoff
)
```

### For Dense Graphs
```python
result = solve_espprc_enhanced(
    G, min_res, max_res, cg_cut_pi,
    source=0, sink=n-1,
    bounds_pruning=True,  # More aggressive pruning
    time_limit=300.0      # Set time limit
)
```

### Maximum Performance Combo
```python
result = solve_espprc_enhanced(
    G, min_res, max_res, cg_cut_pi,
    source=0, sink=n-1,
    elementary=True,
    use_bitwise=True,      # For small graphs
    bounds_pruning=True,    # Aggressive pruning
    k_cycle_elimination=3   # If elementary=False
)
```

## Common Issues

### Issue: Time windows not working
**Solution:** Make sure vertices have `tw_early` and `tw_late` attributes, and specify correct `time_window_res_idx`

### Issue: Slow for large graphs
**Solution:** 
- Set `time_limit` to prevent excessive runtime
- Try `bounds_pruning=True`
- Consider `k_cycle_elimination=3` with `elementary=False`

### Issue: No path found
**Solution:**
- Check resource limits (min_res, max_res) are feasible
- Verify time windows (if enabled) are achievable
- Try relaxing `elementary` constraint

## Examples

See `test_enhanced_features.py` for comprehensive examples of:
- K-cycle elimination comparison
- Time window constraints
- Bitwise optimization benchmarks
- Combined feature usage
