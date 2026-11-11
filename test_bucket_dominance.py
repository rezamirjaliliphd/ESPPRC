"""Regression checks for optional bucket filtering of label dominance.

Run with: python -m unittest test_bucket_dominance
These compare the complete original pricing result, including generated paths;
comparison counters and elapsed time are checked separately.
"""
import random
import unittest

import networkx as nx
from espprc import solve_espprc


class BucketDominanceTests(unittest.TestCase):
    @staticmethod
    def small_graph():
        graph = nx.DiGraph()
        graph.add_nodes_from(range(7))
        for i in range(6):
            for j in range(1, 7):
                if i != j:
                    graph.add_edge(i, j, weight=float((i * 3 + j) % 9 - 5),
                                   res_cost=[float(1 + (i + j) % 3), 1., .5 * (j % 2)])
        return graph

    @staticmethod
    def large_graph(set_fallback=False, customers=14):
        rng = random.Random(17)
        graph = nx.DiGraph()
        # The wrapper remaps node IDs, so simply renaming 0 to 100 would not
        # exercise >64 fallback. Add 65 disconnected nodes FIRST to make every
        # active node's internal ID exceed 64 as well as increase the node count.
        # The Python API explicitly rejects bitwise=True above 64 nodes; callers
        # use the ordinary set representation for this case.
        if set_fallback:
            graph.add_nodes_from(range(65))
        offset = 1000 if set_fallback else 0
        graph.add_nodes_from(offset + i for i in range(customers + 2))
        demands = {i: rng.randint(1, 4) for i in range(1, customers + 1)}
        for i in range(customers + 1):
            for j in range(1, customers + 2):
                if i != j:
                    graph.add_edge(offset + i, offset + j,
                                   weight=float(rng.randint(-10, 5)),
                                   res_cost=[1., float(demands.get(j, 0)),
                                             .5 * (j % 3 == 0), .5 * (j % 3 == 1)])
        return graph, offset, offset + customers + 1

    def compare(self, graph, limits, pi, **kwargs):
        results = []
        statistics = []
        for enabled in (False, True):
            result = solve_espprc(graph, [0.] * len(limits), limits, pi,
                                  bucket_dominance=enabled,
                                  profile_dominance=True, **kwargs)
            stats = result.pop('dominance_stats')
            self.assertGreaterEqual(stats['queries'], 1)
            self.assertGreaterEqual(stats['seconds'], 0.)
            for key in ('queries', 'label_pairs', 'exact_checks',
                        'bucket_checks', 'bucket_directions_skipped'):
                self.assertIsInstance(stats[key], int)
                self.assertGreaterEqual(stats[key], 0)
            if not enabled:
                self.assertEqual(stats['bucket_checks'], 0)
                self.assertEqual(stats['bucket_directions_skipped'], 0)
            results.append(result)
            statistics.append(stats)
        self.assertEqual(results[0], results[1])
        return results[1], statistics

    def test_direction_resource_sri_and_collection_parity(self):
        graph = self.small_graph()
        for direction in ('forward', 'backward', 'both'):
            for critical in (0, 1):
                for bitwise in (False, True):
                    for adaptive in (False, True):
                        for collect in (False, True):
                            with self.subTest(direction=direction, critical=critical,
                                              bitwise=bitwise, adaptive=adaptive,
                                              collect=collect):
                                result, _ = self.compare(
                                    graph, [10., 6., 4.], [0., 0., 3.],
                                    source=0, sink=6, direction=direction,
                                    critical_res=critical, use_bitwise=bitwise,
                                    adaptive_buckets=adaptive, bucket_width=1.,
                                    get_all_path=collect, bounds_pruning=True)
                                if not collect:
                                    self.assertEqual(result['all_feasible_paths'], [])

    def test_statistics_opt_in_and_default_filter(self):
        graph = self.small_graph()
        options = dict(source=0, sink=6, get_all_path=True)
        plain = solve_espprc(graph, [0.] * 3, [10., 6., 4.], [0., 0., 3.], **options)
        explicit = solve_espprc(graph, [0.] * 3, [10., 6., 4.], [0., 0., 3.],
                                bucket_dominance=True, profile_dominance=False, **options)
        profiled = solve_espprc(graph, [0.] * 3, [10., 6., 4.], [0., 0., 3.],
                                profile_dominance=True, **options)
        self.assertNotIn('dominance_stats', plain)
        self.assertNotIn('dominance_stats', explicit)
        self.assertIn('dominance_stats', profiled)
        profiled.pop('dominance_stats')
        self.assertEqual(plain, explicit)
        self.assertEqual(plain, profiled)

    def test_empty_graph_profile(self):
        plain = solve_espprc(nx.DiGraph(), [], [], [], source=0, sink=1)
        profiled = solve_espprc(nx.DiGraph(), [], [], [], source=0, sink=1,
                                profile_dominance=True)
        stats = profiled.pop('dominance_stats')
        self.assertTrue(all(value == 0 for value in stats.values()))
        self.assertEqual(plain, profiled)

    def test_automatic_resource_selection(self):
        for adaptive in (False, True):
            with self.subTest(adaptive=adaptive):
                self.compare(self.small_graph(), [10., 6., 4.], [0., 0., 3.],
                             source=0, sink=6, get_all_path=True,
                             adaptive_buckets=adaptive, find_critical_res=True)

    def test_large_pool_uses_filter_without_changing_paths(self):
        # This fixed instance produces tens of thousands of collected paths and
        # node pools exceeding the 32-label threshold for summary filtering.
        graph, source, sink = self.large_graph()
        for adaptive in (False, True):
            with self.subTest(adaptive=adaptive):
                result, (original, filtered) = self.compare(
                    graph, [8., 20., 5., 5.], [0., 0., 3., 7.],
                    source=source, sink=sink, get_all_path=True,
                    use_bitwise=True, adaptive_buckets=adaptive, bucket_width=1.)
                self.assertGreater(len(result['all_feasible_paths']), 10000)
                self.assertGreater(filtered['bucket_checks'], 0)
                self.assertGreater(filtered['bucket_directions_skipped'], 0)
                self.assertLess(filtered['exact_checks'], original['exact_checks'])

    def test_large_pool_set_fallback(self):
        graph, source, sink = self.large_graph(set_fallback=True)
        result, (_, filtered) = self.compare(
            graph, [8., 20., 5., 5.], [0., 0., 3., 7.],
            source=source, sink=sink, get_all_path=True,
            use_bitwise=False, adaptive_buckets=True, bucket_width=1.)
        self.assertGreater(result['path'][0], 64)
        self.assertGreater(len(result['id_to_node']), 64)
        self.assertGreater(len(result['all_feasible_paths']), 10000)
        self.assertGreater(filtered['bucket_checks'], 0)

    def test_non_elementary_and_two_cycle_parity(self):
        for two_cycle in (False, True):
            with self.subTest(two_cycle=two_cycle):
                self.compare(self.small_graph(), [10., 6., 4.], [0., 0., 3.],
                             source=0, sink=6, elementary=False,
                             two_cycle_elimination=two_cycle, get_all_path=True)


def benchmark():
    """Optional reproducible comparison; prints results without writing reports."""
    import argparse
    import statistics
    import time

    parser = argparse.ArgumentParser(description=benchmark.__doc__)
    parser.add_argument('--benchmark', action='store_true')
    parser.add_argument('--customers', type=int, default=20)
    parser.add_argument('--repeats', type=int, default=3)
    parser.add_argument('--bitwise', action='store_true')
    parser.add_argument('--all-paths', action='store_true')
    options = parser.parse_args()
    if options.customers < 1 or options.repeats < 1:
        parser.error('customers and repeats must be positive')
    graph, source, sink = BucketDominanceTests.large_graph(customers=options.customers)
    reference = None
    for enabled in (False, True):
        kwargs = dict(source=source, sink=sink, bucket_dominance=enabled,
                      get_all_path=options.all_paths, use_bitwise=options.bitwise)
        times = []
        for _ in range(options.repeats):
            start = time.perf_counter()
            result = solve_espprc(graph, [0.] * 4, [8., 20., 5., 5.],
                                  [0., 0., 3., 7.], **kwargs)
            times.append(time.perf_counter() - start)
            if reference is None:
                reference = result
            if result != reference:
                raise AssertionError('Pricing result or generated-path list changed')
        # Obtain counters separately, so profiling overhead is excluded from
        # the reported median solve time.
        profiled = solve_espprc(graph, [0.] * 4, [8., 20., 5., 5.],
                                [0., 0., 3., 7.], profile_dominance=True, **kwargs)
        stats = profiled.pop('dominance_stats')
        if profiled != reference:
            raise AssertionError('Profiling changed pricing output')
        print(f"bucket_dominance={enabled}: median={statistics.median(times):.6f}s, "
              f"exact_checks={stats['exact_checks']:,}, "
              f"bucket_checks={stats['bucket_checks']:,}, "
              f"dominance_seconds={stats['seconds']:.6f}, "
              f"cost={result['cost']}, paths={len(result['all_feasible_paths'])}",
              flush=True)
    print('Complete pricing outputs match.')


if __name__ == '__main__':
    import sys
    if '--benchmark' in sys.argv[1:]:
        benchmark()
    else:
        unittest.main()
