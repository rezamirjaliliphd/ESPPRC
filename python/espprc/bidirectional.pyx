# distutils: language = c++
# cython: boundscheck=False, wraparound=False, initializedcheck=False, cdivision=True

from libcpp.vector cimport vector
cimport cython

from .bidirectional cimport BiDirectional

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

def solve_espprc(G,
                 min_res,
                 max_res,
                 cg_cut_pi,
                 *,
                 source,
                 sink,
                 get_all_path=False):
    """
    Minimal wrapper around C++ BiDirectional.

    Parameters
    ----------
    G : networkx.DiGraph
        Edge attributes required:
          - 'weight' : float
          - 'res'    : sequence[float] (len == len(max_res))
        Nodes may be any hashable; if not ints, we remap internally.
    min_res, max_res, cg_cut_pi : sequence[float]
    source, sink : node labels in G

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
        import networkx as nx  # noqa
    except Exception:
        raise RuntimeError("networkx is required")

    # --- build node id map (0..n-1) ---
    nodes = list(G.nodes())
    nV = len(nodes)
    if nV == 0:
        return {"path": [], "resources": [], "cost": 0.0,
                "all_feasible_paths": [], "id_to_node": []}

    # map any label -> contiguous int
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

    # count valid edges (must carry weight & res)
    cdef int nE = 0
    for u, v, data in G.edges(data=True):
        if 'weight' in data and 'res' in data:
            nE += 1

    cdef vector[double] v_min = _vecd(min_res)
    cdef vector[double] v_max = _vecd(max_res)
    cdef vector[double] v_pi  = _vecd(cg_cut_pi)

    if v_max.size() != v_min.size():
        raise ValueError("len(max_res) must equal len(min_res)")

    # construct solver
    cdef BiDirectional* solver = new BiDirectional(
        nV, nE, source_id, sink_id, v_max, v_min, v_pi)

    # add nodes
    cdef vector[int] v_nodes = _veci(range(nV))
    solver.addNodes(v_nodes)

    # add edges
    cdef vector[double] resvec
    for u, v, data in G.edges(data=True):
        if 'weight' not in data or 'res' not in data:
            continue
        ru = id_of[u]; rv = id_of[v]
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

    # solve
    solver.run()
    cdef size_t j = 0
    # outputs
    cdef vector[int] p = solver.getPath()
    cdef vector[double] cons = solver.getConsumedResources()
    cdef double tot = solver.getTotalCost()
    # deep copy all feasible paths to Python
    
    cdef vector[int] row
    path = [p[i] for i in range(p.size())]
    resources = [cons[i] for i in range(cons.size())]
    cdef vector[vector[int]] allp
    all_paths = []
    if get_all_path:
    # deep copy all feasible paths to Python
        allp =  solver.getAllFeasiblePaths() 
        for i in range(allp.size()):
            row.clear()
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
