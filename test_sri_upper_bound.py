"""Regression for an incumbent that omitted nonnegative SRI floor costs.

Run: python -m unittest test_sri_upper_bound
"""
import math
import unittest

import networkx as nx

from espprc import solve_espprc


class SRIUpperBoundTests(unittest.TestCase):
    def test_complete_cost_controls_joining_bound(self):
        # A's smaller additive cost must not exclude the better full-cost B.
        # All resources are nonnegative; halves avoid floating floor ambiguity.
        for a_base, b_base in ((0, 5), (-10, -5)):
            graph = nx.DiGraph()
            graph.add_nodes_from(range(5))
            for u, v, weight, sri in (
                (0, 1, a_base, .5), (1, 2, 0, .5), (2, 4, 0, 0),
                (0, 3, b_base, 0), (3, 4, 0, 0),
            ):
                graph.add_edge(u, v, weight=weight, res_cost=[1., sri])
            for coefficient in (0., 10.):
                enumerated = {}
                for path in nx.all_simple_paths(graph, 0, 4):
                    edges = [graph[u][v] for u, v in zip(path, path[1:])]
                    enumerated[tuple(path)] = (
                        sum(e['weight'] for e in edges)
                        + coefficient * math.floor(sum(e['res_cost'][1] for e in edges))
                    )
                for bounds in (False, True):
                    for bitwise in (False, True):
                        with self.subTest(base=a_base, coefficient=coefficient,
                                          bounds=bounds, bitwise=bitwise):
                            result = solve_espprc(
                                graph, [0., 0.], [3., 2.], [0., coefficient],
                                source=0, sink=4, direction='both', elementary=True,
                                bounds_pruning=bounds, use_bitwise=bitwise,
                                get_all_path=False,
                            )
                            path = tuple(result['path'])
                            self.assertIn(path, enumerated)
                            self.assertAlmostEqual(result['cost'], enumerated[path])
                            self.assertAlmostEqual(result['cost'], min(enumerated.values()))


if __name__ == '__main__':
    unittest.main()
