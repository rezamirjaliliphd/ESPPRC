# distutils: language = c++
# cython: boundscheck=False, wraparound=False, initializedcheck=False, cdivision=True

from libcpp.vector cimport vector
from libcpp cimport bool
cimport cython
from math import isfinite

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
    num_threads=1,
    bucket_width=5.0,
    adaptive_buckets=True,
    bucket_dominance=True,
    profile_dominance=False,
    bucket_extension=True,
    profile_extension=False,
    bucket_joining=True,
    profile_joining=False,
    parallel_joining=False
):
    """
    Solve an ESPPRC instance with the bidirectional labeling solver.

    Parameters
    ----------
    G : networkx.DiGraph
        Edge attributes required:
          - 'weight' : float
          - 'res_cost' : sequence[float] (len == len(max_res))
        Nodes may be any hashable; if not ints, we remap internally.
    min_res, max_res, cg_cut_pi : sequence[float]
    source, sink : node labels in G
    get_all_path : bool, default False
        Return paths retained by the search; dominance can discard other feasible paths
    elementary : bool, default True
        Enforce elementary path constraint
    two_cycle_elimination : bool, default False
        Eliminate 2-cycles for non-elementary paths
    k_cycle_elimination : int, default 0
        K-cycle elimination (0=disabled, 2=2-cycle, 3=3-cycle, etc.)
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
        Legacy flag; this wrapper does not load node-specific time windows
    time_window_res_idx : int, default 0
        Resource index for time (when time windows enabled)
    use_bitwise : bool, default False
        Use bitwise operations for elementary checking. Supported for |V| <= 64.
    enable_parallel : bool, default False
        Prepare extensions for batches of queued labels using num_threads.
        Dominance and insertion retain serial search order. Independent of
        parallel_joining. Small queues or node degrees, callbacks and builds without OpenMP
        use the optimized serial extension path.
    num_threads : int, default 1
        Number of threads for enabled parallel extension/joining stages.
    bucket_width : float, default 5.0
        Initial width of critical-resource intervals; remains fixed when
        adaptive_buckets=False.
    adaptive_buckets : bool, default True
        Periodically repartition intervals using label density and partial cost.
        Extension order and joining remain unchanged.
    bucket_dominance : bool, default True
        Screen dominance comparisons using conservative bucket summaries.
        False retains the original scan for performance/correctness comparisons.
    profile_dominance : bool, default False
        Include dominance_stats (comparison counters and elapsed seconds).
        Timing includes dominance and deletion/index maintenance, but not insertion.

    bucket_extension : bool, default True
        Reuse adjacency and reject critical-resource-infeasible bucket/arc
        transitions. Also applies when prepared parallel extensions are used.
        Callbacks retain ordinary resource extension.
    profile_extension : bool, default False
        Include extension_stats. generation_seconds measures adjacency,
        screening and label construction, excluding dominance/heap updates;
        total_seconds includes the complete extension stage. Arc counters
        describe actual processing in serial search order. parallel_labels
        counts consumed preparations; prepared_labels/prepared_extensions
        include speculative work, and discarded_labels counts unused jobs.
    bucket_joining : bool, default True
        Screen joins using adaptive backward-bucket cost, resource and visited
        node summaries. Preserve original pair order and collected paths.
        Resource shortcuts apply only without path collection or REF callbacks.
    profile_joining : bool, default False
        Include joining_stats. seconds includes index construction and joining;
        label_pairs counts pairs reached after group/block filtering, including
        speculative checks in parallel mode. parallel_batches, parallel_pairs,
        and speculative_records report the parallel work performed.
    parallel_joining : bool, default False
        Batch join checks across forward labels using num_threads. Independent
        of enable_parallel (extension); commits preserve serial result order.
        Small frontiers, callbacks and builds without OpenMP use serial joining.

    Returns
    -------
    dict with keys:
      path               : list[int]        (node ids after remap)
      resources          : list[float]
      cost               : float
      all_feasible_paths : list[list[int]]
      id_to_node         : list[original_node_label]  (index == internal id)
      dominance_stats    : dict, only when profile_dominance=True
      extension_stats    : dict, only when profile_extension=True
      joining_stats      : dict, only when profile_joining=True
    """
    try:
        import networkx as nx
    except Exception:
        raise RuntimeError("networkx is required")

    # Build node id map
    nodes = list(G.nodes())
    nV = len(nodes)
    if nV == 0:
        result = {"path": [], "resources": [], "cost": 0.0,
                  "all_feasible_paths": [], "id_to_node": []}
        if profile_dominance:
            result["dominance_stats"] = {
                "queries": 0, "label_pairs": 0, "exact_checks": 0,
                "bucket_checks": 0, "bucket_directions_skipped": 0, "seconds": 0.,
            }
        if profile_extension:
            result["extension_stats"] = {
                "labels_processed": 0, "arcs_considered": 0, "eligible_extensions": 0,
                "full_extensions": 0, "bucket_rejections": 0, "infeasible_extensions": 0,
                "cache_hits": 0, "cache_rebuilds": 0, "parallel_labels": 0,
                "generation_seconds": 0., "total_seconds": 0.,
                "parallel_batches": 0, "prepared_labels": 0,
                "prepared_extensions": 0, "discarded_labels": 0,
            }
        if profile_joining:
            result["joining_stats"] = {
                "group_checks": 0, "groups_skipped": 0, "blocks_skipped": 0,
                "label_pairs": 0, "full_merges": 0, "resource_shortcuts": 0,
                "seconds": 0., "parallel_batches": 0, "parallel_pairs": 0,
                "speculative_records": 0,
            }
        return result

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
        if 'weight' in data and 'res_cost' in data:
            nE += 1

    cdef vector[double] v_min = _vecd(min_res)
    cdef vector[double] v_max = _vecd(max_res)
    cdef vector[double] v_pi  = _vecd(cg_cut_pi)

    if v_max.size() != v_min.size():
        raise ValueError("len(max_res) must equal len(min_res)")
    if v_max.size() == 0:
        raise ValueError("at least one resource bound is required")
    if v_pi.size() not in (0, v_max.size()):
        raise ValueError("len(cg_cut_pi) must be 0 or equal len(max_res)")
    if critical_res < 0 or critical_res >= v_max.size():
        raise ValueError("critical_res must be a valid resource index")
    if use_time_windows and (time_window_res_idx < 0 or time_window_res_idx >= v_max.size()):
        raise ValueError("time_window_res_idx must be a valid resource index")
    for i in range(v_max.size()):
        if not isfinite(v_min[i]) or not isfinite(v_max[i]):
            raise ValueError("resource bounds must be finite numbers")
        if v_min[i] > v_max[i]:
            raise ValueError(f"min_res[{i}] cannot be greater than max_res[{i}]")

    # Construct solver
    if not isfinite(bucket_width) or bucket_width <= 0:
        raise ValueError("bucket_width must be finite and positive")
    cdef BiDirectional* solver = new BiDirectional(
        nV, nE, source_id, sink_id, v_max, v_min, v_pi)

    # Set parameters
    solver.setCollectPaths(get_all_path)
    solver.setBucketWidth(bucket_width)
    solver.setAdaptiveBuckets(adaptive_buckets)
    solver.setBucketDominance(bucket_dominance)
    solver.setProfileDominance(profile_dominance)
    solver.setBucketExtension(bucket_extension)
    solver.setProfileExtension(profile_extension)
    solver.setBucketJoining(bucket_joining)
    solver.setProfileJoining(profile_joining)
    solver.setParallelJoining(parallel_joining, num_threads)
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
    if use_bitwise:
        if nV > 64:
            del solver
            raise ValueError("use_bitwise=True supports at most 64 nodes")
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
        if 'weight' not in data or 'res_cost' not in data:
            continue
        ru = id_of[u]
        rv = id_of[v]
        w = float(data['weight'])
        r = data['res_cost']
        if len(r) != v_max.size():
            del solver
            raise ValueError(f"edge ({u},{v}) has len(res_cost)={len(r)} "
                             f"but len(max_res)={v_max.size()}")
        resvec.clear()
        for t in r:
            resvec.push_back(<double>t)
        solver.addEdge(ru, rv, w, resvec)

    # Solve
    try:
        solver.run()
    except:
        del solver
        raise
    
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

    cdef vector[double] stats
    if profile_dominance:
        stats = solver.getDominanceStats()
        result["dominance_stats"] = {
            "queries": int(stats[0]), "label_pairs": int(stats[1]),
            "exact_checks": int(stats[2]), "bucket_checks": int(stats[3]),
            "bucket_directions_skipped": int(stats[4]), "seconds": stats[5],
        }
    if profile_extension:
        stats = solver.getExtensionStats()
        result["extension_stats"] = {
            "labels_processed": int(stats[0]), "arcs_considered": int(stats[1]),
            "eligible_extensions": int(stats[2]), "full_extensions": int(stats[3]),
            "bucket_rejections": int(stats[4]), "infeasible_extensions": int(stats[5]),
            "cache_hits": int(stats[6]), "cache_rebuilds": int(stats[7]),
            "parallel_labels": int(stats[8]),
            "generation_seconds": stats[9], "total_seconds": stats[10],
            "parallel_batches": int(stats[11]), "prepared_labels": int(stats[12]),
            "prepared_extensions": int(stats[13]), "discarded_labels": int(stats[14]),
        }
    if profile_joining:
        stats = solver.getJoiningStats()
        result["joining_stats"] = {
            "group_checks": int(stats[0]), "groups_skipped": int(stats[1]),
            "blocks_skipped": int(stats[2]), "label_pairs": int(stats[3]),
            "full_merges": int(stats[4]), "resource_shortcuts": int(stats[5]),
            "seconds": stats[6],
            "parallel_batches": int(stats[7]), "parallel_pairs": int(stats[8]),
            "speculative_records": int(stats[9]),
        }
    del solver
    return result


solve_espprc_enhanced = solve_espprc
