"""Integration checks for membership-only duration buckets."""
import unittest
import random
import networkx as nx
from espprc import solve_espprc


class DurationBucketTests(unittest.TestCase):
    def graph(self):
        graph = nx.DiGraph()
        graph.add_nodes_from(range(7))
        for i in range(6):
            for j in range(1, 7):
                if i != j:
                    graph.add_edge(i, j, weight=float((i * 3 + j) % 9 - 5),
                                   res_cost=[float(1 + (i + j) % 3), 1., .5 * (j % 2)])
        return graph

    def test_width_does_not_change_search(self):
        graph = self.graph()
        for direction in ('forward', 'backward', 'both'):
            for critical in (0, 1):
                for bounds in (False, True):
                    for bitwise in (False, True):
                        kwargs = dict(source=0, sink=6, direction=direction,
                                      critical_res=critical, bounds_pruning=bounds,
                                      use_bitwise=bitwise, get_all_path=True)
                        baseline = solve_espprc(graph, [0.] * 3, [10., 6., 4.],
                                                [0., 0., 3.], **kwargs)
                        for width in (.25, 1., 2.5, 5., 100.):
                            with self.subTest(direction=direction, critical=critical,
                                              bounds=bounds, bitwise=bitwise, width=width):
                                actual = solve_espprc(graph, [0.] * 3, [10., 6., 4.],
                                                     [0., 0., 3.], bucket_width=width, **kwargs)
                                self.assertEqual(actual, baseline)

    def test_automatic_resource_selection(self):
        graph = self.graph()
        results = [solve_espprc(graph, [0.] * 3, [10., 6., 4.], [0., 0., 3.],
                               source=0, sink=6, find_critical_res=True,
                               get_all_path=True, bucket_width=width)
                   for width in (.5, 5., 50.)]
        self.assertEqual(results[0], results[1])
        self.assertEqual(results[0], results[2])

    def test_adaptive_and_fixed_results_match(self):
        graph = self.graph()
        for direction in ('forward', 'backward', 'both'):
            for width in (.5, 5., 50.):
                with self.subTest(direction=direction, width=width):
                    kwargs = dict(source=0, sink=6, direction=direction,
                                  get_all_path=True, bucket_width=width)
                    fixed = solve_espprc(graph, [0.] * 3, [10., 6., 4.],
                                         [0., 0., 3.], adaptive_buckets=False, **kwargs)
                    adaptive = solve_espprc(graph, [0.] * 3, [10., 6., 4.],
                                            [0., 0., 3.], adaptive_buckets=True, **kwargs)
                    self.assertEqual(fixed, adaptive)

    def test_large_label_pool_preserves_all_generated_paths(self):
        rng = random.Random(17)
        graph = nx.DiGraph()
        graph.add_nodes_from(range(16))
        demands = {i: rng.randint(1, 4) for i in range(1, 15)}
        for i in range(15):
            for j in range(1, 16):
                if i != j:
                    graph.add_edge(i, j, weight=float(rng.randint(-10, 5)),
                                   res_cost=[1., float(demands.get(j, 0)),
                                             .5 * (j % 3 == 0), .5 * (j % 3 == 1)])
        results = [solve_espprc(graph, [0.] * 4, [8., 20., 5., 5.], [0., 0., 3., 7.],
                               source=0, sink=15, get_all_path=True, adaptive_buckets=adaptive)
                   for adaptive in (False, True)]
        self.assertEqual(results[0], results[1])
        self.assertGreater(len(results[1]['all_feasible_paths']), 10000)

    def test_invalid_width(self):
        for width in (0., -1., float('inf'), float('nan')):
            with self.subTest(width=width), self.assertRaisesRegex(ValueError, 'bucket_width'):
                solve_espprc(self.graph(), [0.] * 3, [10., 6., 4.], [0.] * 3,
                             source=0, sink=6, bucket_width=width)

    def test_unrepresentable_index_raises_python_exception(self):
        with self.assertRaises(OverflowError):
            solve_espprc(self.graph(), [0.] * 3, [10., 6., 4.], [0.] * 3,
                         source=0, sink=6, bucket_width=1e-300)


if __name__ == '__main__':
    unittest.main()
