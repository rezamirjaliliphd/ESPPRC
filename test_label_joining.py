"""Joining regressions; run with python -m unittest test_label_joining."""
import math
import random
import unittest

import networkx as nx
from espprc import solve_espprc


def path_value(graph, path, pi):
    edges = [graph[u][v] for u, v in zip(path, path[1:])]
    resources = [sum(edge['res_cost'][r] for edge in edges) for r in range(len(pi))]
    cost = sum(edge['weight'] for edge in edges)
    cost += sum(p * math.floor(r) for p, r in zip(pi, resources))
    return cost, resources


class LabelJoiningTests(unittest.TestCase):
    def test_small_graphs_against_enumeration(self):
        # Nonnegative costs avoid relying on unrelated negative-arc bound rules.
        for seed in range(30):
            rng = random.Random(seed)
            graph = nx.DiGraph()
            graph.add_nodes_from(range(8))
            for i in range(7):
                for j in range(1, 8):
                    if i != j and (j == i + 1 or rng.random() < .4):
                        graph.add_edge(i, j, weight=rng.randrange(8),
                                       res_cost=[1., float(rng.randrange(1, 5)),
                                                 .5 * (j % 3 == 0)])
            limits = [6., 14., 4.]
            pi = [0., 0., float(seed % 4)]
            feasible = []
            for path in nx.all_simple_paths(graph, 0, 7):
                value, resources = path_value(graph, path, pi)
                if all(r <= limit for r, limit in zip(resources, limits)):
                    feasible.append(value)
            for collect in (False, True):
                for bitwise in (False, True):
                    with self.subTest(seed=seed, collect=collect, bitwise=bitwise):
                        result = solve_espprc(
                            graph, [0.] * 3, limits, pi, source=0, sink=7,
                            get_all_path=collect, use_bitwise=bitwise,
                            bounds_pruning=False)
                        if not feasible:
                            self.assertEqual(result['path'], [])
                            continue
                        path = [result['id_to_node'][i] for i in result['path']]
                        value, resources = path_value(graph, path, pi)
                        self.assertEqual(len(path), len(set(path)))
                        self.assertTrue(all(r <= b for r, b in zip(resources, limits)))
                        self.assertEqual(value, min(feasible))
                        self.assertEqual(result['cost'], value)
                        self.assertEqual(result['resources'], resources)
                        if not collect:
                            self.assertEqual(result['all_feasible_paths'], [])

    def test_large_node_ids_use_overlap_fallback(self):
        # The two halves can share node 64; truncating a 64-bit mask would miss it.
        graph = nx.DiGraph()
        graph.add_nodes_from(range(66))
        for u, v, cost in [(0, 64, 0), (64, 1, -5), (1, 64, -5),
                           (64, 65, 0), (1, 65, 2), (0, 2, 0), (2, 65, 1)]:
            graph.add_edge(u, v, weight=cost, res_cost=[1., .5])
        for collect in (False, True):
            result = solve_espprc(graph, [0., 0.], [5., 3.], [0., 2.],
                                 source=0, sink=65, get_all_path=collect)
            feasible = [path_value(graph, p, [0., 2.])[0]
                        for p in nx.all_simple_paths(graph, 0, 65)]
            self.assertEqual(result['cost'], min(feasible))
            self.assertEqual(len(result['path']), len(set(result['path'])))

    def test_non_elementary_two_cycle_mode(self):
        graph = nx.DiGraph()
        for u, v, w in [(0, 1, 0), (1, 2, -3), (2, 1, -3), (1, 3, 0), (2, 3, 0)]:
            graph.add_edge(u, v, weight=w, res_cost=[1.])
        # Enumerate resource-feasible walks, optionally excluding 2-cycles.
        for two_cycle in (False, True):
            costs = []
            def extend(path, cost):
                if path[-1] == 3:
                    costs.append(cost)
                    return
                if len(path) == 5:
                    return
                for v in graph.successors(path[-1]):
                    if two_cycle and len(path) >= 2 and v == path[-2]:
                        continue
                    extend(path + [v], cost + graph[path[-1]][v]['weight'])
            extend([0], 0.)
            for collect in (False, True):
                result = solve_espprc(graph, [0.], [4.], [0.], source=0, sink=3,
                                     elementary=False, two_cycle_elimination=two_cycle,
                                     get_all_path=collect)
                self.assertEqual(result['cost'], min(costs))


def benchmark():
    """Deterministic synthetic pricing cases; includes graph-to-C++ conversion."""
    import statistics
    import time
    for customers in (14, 20):
        rng = random.Random(17)
        graph = nx.DiGraph()
        graph.add_nodes_from(range(customers + 2))
        demands = {i: rng.randint(1, 4) for i in range(1, customers + 1)}
        for i in range(customers + 1):
            for j in range(1, customers + 2):
                if i != j:
                    graph.add_edge(i, j, weight=float(rng.randint(-10, 5)),
                                   res_cost=[1., float(demands.get(j, 0)),
                                             .5 * (j % 3 == 0), .5 * (j % 3 == 1)])
        for collect in (False, True):
            times = []
            for _ in range(3):
                start = time.perf_counter()
                result = solve_espprc(
                    graph, [0.] * 4, [8., 20., 5., 5.], [0., 0., 3., 7.],
                    source=0, sink=customers + 1, get_all_path=collect)
                times.append(time.perf_counter() - start)
            print(f"customers={customers}, all_paths={collect}, "
                  f"median={statistics.median(times):.6f}s, cost={result['cost']}, "
                  f"collected={len(result['all_feasible_paths'])}", flush=True)


if __name__ == '__main__':
    import sys
    if sys.argv[1:] == ['--benchmark']:
        benchmark()
    else:
        unittest.main()
