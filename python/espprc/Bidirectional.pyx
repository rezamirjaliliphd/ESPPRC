# distutils: language = c++
# cython: boundscheck=False, wraparound=False, initializedcheck=False, cdivision=True

from libcpp.vector cimport vector
from libcpp cimport bool
cimport cython

from .Bidirectional cimport BiDirectional

cdef inline vector[double] _vecd(object seq):
    cdef vector[double] v
    for x in seq:
        v.push_back(<double>x)
    return v

cdef inline vector[int] _veci(object seq):
    cdef vector[int] v
    for x in seq:
        v.push_back(<int>x)
    return v

def solve_espprc(
    G,
    min_res,
    max_res,
    cg_cut_pi,
    *,
    source,
    sink,
    get_all_path=False,
    elementary=True,
    two_cycle_elimination=False,
    k_cycle_elimination=0,
    bounds_pruning=False,
    direction="both",
    method="unprocessed",
    time_limit=None,
    threshold=None,
    find_critical_res=False,
    critical_res=0,
    use_time_windows=False,
    time_window_res_idx=0,
    use_bitwise=False,
    enable_parallel=False,
    num_threads=1
):
    """
    Solve an ESPPRC instance with the bidirectional labeling solver.

    Parameters
    ----------
    G : networkx.DiGraph
        Edge attributes required:
          - 'weight' : float
          - 'res'    : sequence[float] (len == len(max_res))
        Nodes may be any hashable; if not ints, we remap internally.
    min_res, max_res, cg_cut_pi : sequence[float]
    source, sink : node labels in G
    get_all_path : bool, default False
        Return all feasible paths found
    elementary : bool, default True
        Enforce elementary path constraint
    two_cycle_elimination : bool, default False
        Eliminate 2-cycles for non-elementary paths
    k_cycle_elimination : int, default 0
        K-cycle elimination (0=disabled, 2=2-cycle, 3=3-cycle, etc.)
        Literature suggests k=3 for optimal tradeoff
    bounds_pruning : bool, default False
        Use bounds pruning for label dominance
    direction : str, default "both"
        Search direction: "forward", "backward", or "both"
    method : str, default "unprocessed"
        Direction selection method: "unprocessed", "processed", "generated"
    time_limit : float, optional
        Time limit in seconds
    threshold : float, optional
        Stop when path cost <= threshold found
    find_critical_res : bool, default False
        Automatically find critical resource
    critical_res : int, default 0
        Index of critical resource
    use_time_windows : bool, default False
        Enable time window constraints
    time_window_res_idx : int, default 0
        Resource index for time (when time windows enabled)
    use_bitwise : bool, default False
        Use bitwise operations for elementary checking. Supported for |V| <= 64.
    enable_parallel : bool, default False
        Enable parallel label processing
    num_threads : int, default 1
        Number of threads for parallel processing

    Returns
    -------
    dict with keys:
      path               : list[int]        (node ids after remap)
      resources          : list[float]
      cost               : float
      all_feasible_paths : list[list[int]]
      id_to_node         : list[original_node_label]  (index == internal id)
    """
    try:
        import networkx as nx
    except Exception:
        raise RuntimeError("networkx is required")

    # Build node id map
    nodes = list(G.nodes())
    nV = len(nodes)
    if nV == 0:
        return {"path": [], "resources": [], "cost": 0.0,
                "all_feasible_paths": [], "id_to_node": []}

    id_of = {}
    id_to_node = []
    cdef size_t i = 0
    for i in range(len(nodes)):
        id_of[nodes[i]] = i
        id_to_node.append(nodes[i])

    if source not in id_of or sink not in id_of:
        raise ValueError("source/sink must be nodes of G")

    source_id = id_of[source]
    sink_id   = id_of[sink]

    # Count valid edges
    cdef int nE = 0
    for u, v, data in G.edges(data=True):
        if 'weight' in data and 'res' in data:
            nE += 1

    cdef vector[double] v_min = _vecd(min_res)
    cdef vector[double] v_max = _vecd(max_res)
    cdef vector[double] v_pi  = _vecd(cg_cut_pi)

    if v_max.size() != v_min.size():
        raise ValueError("len(max_res) must equal len(min_res)")

    # Construct solver
    cdef BiDirectional* solver = new BiDirectional(
        nV, nE, source_id, sink_id, v_max, v_min, v_pi)

    # Set parameters
    solver.setElementary(elementary)
    solver.setTwoCycleElimination(two_cycle_elimination)
    
    # K-cycle elimination
    if k_cycle_elimination > 0:
        solver.setKCycleElimination(k_cycle_elimination)
    
    solver.setBoundsPruning(bounds_pruning)
    solver.setMethod(method.encode('utf-8'))
    
    if direction.lower() in ["forward", "backward"]:
        solver.setDirection(direction.lower().encode('utf-8'))
    
    if time_limit is not None:
        solver.setTimeLimit(float(time_limit))
    
    if threshold is not None:
        solver.setThreshold(float(threshold))
    
    solver.setFindCriticalRes(find_critical_res)
    solver.setCriticalRes(critical_res)
    
    # Time windows
    if use_time_windows:
        solver.setTimeWindows(True, time_window_res_idx)
    
    # Bitwise elementary is only valid for the uint64 mask range.
    if use_bitwise and nV <= 64:
        solver.setBitwiseElementary(True, nV)
    
    # Parallelization
    if enable_parallel:
        solver.setParallel(True, num_threads)

    # Add nodes
    cdef vector[int] v_nodes = _veci(range(nV))
    solver.addNodes(v_nodes)

    # Add edges
    cdef vector[double] resvec
    for u, v, data in G.edges(data=True):
        if 'weight' not in data or 'res' not in data:
            continue
        ru = id_of[u]
        rv = id_of[v]
        w = float(data['weight'])
        r = data['res']
        if len(r) != v_max.size():
            del solver
            raise ValueError(f"edge ({u},{v}) has len(res)={len(r)} "
                             f"but len(max_res)={v_max.size()}")
        resvec.clear()
        for t in r:
            resvec.push_back(<double>t)
        solver.addEdge(ru, rv, w, resvec)

    # Solve
    solver.run()
    
    # Extract results
    cdef vector[int] p = solver.getPath()
    cdef vector[double] cons = solver.getConsumedResources()
    cdef double tot = solver.getTotalCost()
    
    path = [p[i] for i in range(p.size())]
    resources = [cons[i] for i in range(cons.size())]
    
    cdef vector[vector[int]] allp
    all_paths = []
    cdef vector[int] row
    if get_all_path:
        allp = solver.getAllFeasiblePaths()
        
        for i in range(allp.size()):
            row = allp[i]
            all_paths.append([row[j] for j in range(row.size())])

    result = {
        "path": path,
        "resources": resources,
        "cost": float(tot),
        "all_feasible_paths": all_paths,
        "id_to_node": id_to_node,
    }

    del solver
    return result


solve_espprc_enhanced = solve_espprc
