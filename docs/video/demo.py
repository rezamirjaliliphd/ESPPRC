"""The video's graph and independently checked solution. No rendering imports."""
import json
from pathlib import Path

import networkx as nx
from espprc import solve_espprc

POSITIONS = {'S': (-2.8, 0), 'A': (-1.35, 1.1), 'B': (-1.35, -1.1),
             'C': (1.1, 1.1), 'D': (1.1, -1.1), 'T': (2.8, 0)}
# (tail, head, cost, duration); entering a customer consumes one payload unit.
EDGES = [('S', 'A', 1, 4), ('A', 'C', 1, 4), ('C', 'T', 1, 4),
         ('S', 'B', 2, 2), ('B', 'D', 2, 2), ('D', 'T', 2, 2),
         ('A', 'D', 4, 4), ('B', 'C', 1, 3), ('C', 'D', 1, 1)]

def example():
    graph = nx.DiGraph()
    graph.add_nodes_from(POSITIONS)
    for u, v, cost, duration in EDGES:
        graph.add_edge(u, v, weight=cost, res_cost=[duration, int(v not in ('S', 'T'))])
    options = dict(min_res=[0, 0], max_res=[8, 2], cg_cut_pi=[0, 0],
                   source='S', sink='T', elementary=True)
    return graph, options

def verify():
    graph, options = example()
    paths = []
    for path in nx.all_simple_paths(graph, 'S', 'T'):
        edges = [graph[u][v] for u, v in zip(path, path[1:])]
        resources = [sum(e['res_cost'][r] for e in edges) for r in range(2)]
        paths.append(dict(path=path, cost=sum(e['weight'] for e in edges),
                          resources=resources,
                          feasible=all(x <= b for x, b in zip(resources, options['max_res']))))
    feasible = [p for p in paths if p['feasible']]
    reference = min(feasible, key=lambda p: p['cost'])
    result = solve_espprc(graph, **options)
    decoded = [result['id_to_node'][i] for i in result['path']]
    if decoded != reference['path'] or result['cost'] != reference['cost'] or result['resources'] != reference['resources']:
        raise AssertionError((result, reference))
    cheapest = min(paths, key=lambda p: p['cost'])
    if reference != dict(path=['S', 'B', 'D', 'T'], cost=6, resources=[6, 2], feasible=True):
        raise AssertionError(reference)
    if cheapest['path'] != ['S', 'A', 'C', 'T'] or cheapest['resources'][0] != 12 or cheapest['feasible']:
        raise AssertionError(cheapest)
    return dict(best=reference, cheapest=cheapest, enumerated_paths=paths,
                solver_version=__import__('espprc').__version__)

if __name__ == '__main__':
    print(json.dumps(verify(), indent=2))
