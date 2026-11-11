"""Full-output regressions for optional bucket-assisted label joining."""
import math
import random
import unittest

import networkx as nx
from espprc import solve_espprc


def graph_case(customers=14, seed=17, large_ids=False):
    rng = random.Random(seed)
    graph = nx.DiGraph()
    if large_ids:
        graph.add_nodes_from(range(65))
    offset = 1000 if large_ids else 0
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


class BucketJoiningTests(unittest.TestCase):
    def compare_parallel(self, graph, limits, pi, minimum=None,
                         threads=(2, 4, 8), **kwargs):
        if minimum is None:
            minimum = [0.] * len(limits)
        reference = solve_espprc(
            graph, minimum, limits, pi, parallel_joining=False,
            enable_parallel=False, profile_joining=True, **kwargs)
        serial_stats = reference.pop('joining_stats')
        for name in ('parallel_batches', 'parallel_pairs', 'speculative_records'):
            self.assertEqual(serial_stats[name], 0)
        for count in threads:
            with self.subTest(threads=count):
                result = solve_espprc(
                    graph, minimum, limits, pi, parallel_joining=True,
                    enable_parallel=False, num_threads=count,
                    profile_joining=True, **kwargs)
                measured = result.pop('joining_stats')
                self.assertEqual(result, reference)
                for name, value in measured.items():
                    self.assertGreaterEqual(value, 0, name)
                if count == 1 or kwargs.get('direction', 'both') != 'both':
                    for name in ('parallel_batches', 'parallel_pairs', 'speculative_records'):
                        self.assertEqual(measured[name], 0)
        return reference

    def compare(self, graph, limits, pi, minimum=None, **kwargs):
        if minimum is None:
            minimum = [0.] * len(limits)
        results = []
        stats = []
        for enabled in (False, True):
            result = solve_espprc(graph, minimum, limits, pi,
                                  bucket_joining=enabled, profile_joining=True, **kwargs)
            measured = result.pop('joining_stats')
            self.assertIn('group_checks', measured)
            for key, value in measured.items():
                self.assertGreaterEqual(value, 0, key)
            if not enabled:
                self.assertEqual(measured['group_checks'], 0)
            results.append(result)
            stats.append(measured)
        # Intentionally compare complete lists, including duplicate/infeasible
        # records and their order. Normalizing to sets would hide regressions.
        self.assertEqual(results[0], results[1])
        return results[1], stats

    def test_random_option_combinations(self):
        for seed in range(12):
            graph, source, sink = graph_case(6, seed)
            for collect in (False, True):
                for bitwise in (False, True):
                    with self.subTest(seed=seed, collect=collect, bitwise=bitwise):
                        self.compare(graph, [6., 12., 4., 4.], [0., 0., 3., 7.],
                                     source=source, sink=sink, get_all_path=collect,
                                     use_bitwise=bitwise, critical_res=seed % 2,
                                     adaptive_buckets=seed % 3 != 0,
                                     bucket_dominance=seed % 4 != 0,
                                     bucket_extension=seed % 5 != 0,
                                     bounds_pruning=seed % 2 == 0,
                                     bucket_width=.5 if seed % 2 else 5.)

    def test_large_collected_history_and_set_fallback(self):
        for large_ids, adaptive in ((False, False), (False, True), (True, True)):
            with self.subTest(large_ids=large_ids, adaptive=adaptive):
                graph, source, sink = graph_case(large_ids=large_ids)
                result, (_, measured) = self.compare(graph, [8., 20., 5., 5.], [0., 0., 3., 7.],
                                         source=source, sink=sink, get_all_path=True,
                                         use_bitwise=not large_ids, adaptive_buckets=adaptive,
                                         bucket_width=.5)
                self.assertGreater(len(result['all_feasible_paths']), 10000)
                self.assertGreater(measured['group_checks'], 0)
                if large_ids:
                    self.assertGreater(result['path'][0], 64)

    def test_profile_opt_in_default_and_single_directions(self):
        graph, source, sink = graph_case(5)
        kwargs = dict(source=source, sink=sink, get_all_path=True)
        default = solve_espprc(graph, [0.] * 4, [6., 12., 4., 4.], [0., 0., 3., 7.], **kwargs)
        explicit = solve_espprc(graph, [0.] * 4, [6., 12., 4., 4.], [0., 0., 3., 7.],
                                bucket_joining=True, profile_joining=False, **kwargs)
        profiled = solve_espprc(graph, [0.] * 4, [6., 12., 4., 4.], [0., 0., 3., 7.],
                                profile_joining=True, **kwargs)
        self.assertNotIn('joining_stats', default)
        self.assertNotIn('joining_stats', explicit)
        profiled.pop('joining_stats')
        self.assertEqual(default, explicit)
        self.assertEqual(default, profiled)
        for direction in ('forward', 'backward'):
            self.compare(graph, [6., 12., 4., 4.], [0., 0., 3., 7.], direction=direction, **kwargs)

    def test_non_elementary_and_two_cycle_parity(self):
        graph = nx.DiGraph()
        for u, v, weight in [(0, 1, 0.), (1, 2, -3.), (2, 1, -3.),
                              (1, 3, 0.), (2, 3, 0.)]:
            graph.add_edge(u, v, weight=weight, res_cost=[1., .5])
        for two_cycle in (False, True):
            for collect in (False, True):
                with self.subTest(two_cycle=two_cycle, collect=collect):
                    self.compare(graph, [4., 4.], [0., 2.], source=0, sink=3,
                                 elementary=False, two_cycle_elimination=two_cycle,
                                 get_all_path=collect, bucket_width=.25)

    def test_small_nonnegative_graph_against_enumeration(self):
        for seed in range(8):
            rng = random.Random(seed)
            graph = nx.DiGraph()
            graph.add_nodes_from(range(7))
            for i in range(6):
                for j in range(i + 1, 7):
                    if j == i + 1 or rng.random() < .5:
                        graph.add_edge(i, j, weight=float(rng.randrange(5)),
                                       res_cost=[1., float(rng.randrange(1, 4)), .5 * (j % 2)])
            limits, pi = [6., 12., 4.], [0., 0., 3.]
            feasible = []
            for path in nx.all_simple_paths(graph, 0, 6):
                edges = [graph[u][v] for u, v in zip(path, path[1:])]
                resources = [sum(edge['res_cost'][r] for edge in edges) for r in range(3)]
                if all(r <= maximum for r, maximum in zip(resources, limits)):
                    feasible.append(sum(edge['weight'] for edge in edges) +
                                    sum(p * math.floor(r) for p, r in zip(pi, resources)))
            for collect in (False, True):
                with self.subTest(seed=seed, collect=collect):
                    result, _ = self.compare(graph, limits, pi, source=0, sink=6,
                                             get_all_path=collect, bounds_pruning=False)
                    if feasible:
                        self.assertEqual(result['cost'], min(feasible))
                    else:
                        self.assertEqual(result['path'], [])

    def test_fractional_resource_bound_and_parallel_edges(self):
        graph = nx.MultiDiGraph()
        for u, v, critical, cut in [(0, 1, 1., .2), (1, 2, 1., .7),
                                    (1, 2, 2., .7), (2, 3, 1., .1),
                                    (0, 2, 2., .1), (1, 3, 2., .5)]:
            graph.add_edge(u, v, weight=-1., res_cost=[critical, cut])
        for upper in (math.nextafter(1., 0.), 1., math.nextafter(1., 2.)):
            for collect in (False, True):
                with self.subTest(upper=upper, collect=collect):
                    self.compare(graph, [5., upper], [0., 10.], source=0, sink=3,
                                 get_all_path=collect, bucket_width=.25)

    def test_parallel_joining_large_history_and_set_fallback(self):
        for large_ids, adaptive, buckets in ((False, False, True),
                                             (False, True, False),
                                             (True, True, True)):
            graph, source, sink = graph_case(large_ids=large_ids)
            for collect in (False, True):
                with self.subTest(large_ids=large_ids, adaptive=adaptive,
                                  buckets=buckets, collect=collect):
                    result = self.compare_parallel(
                        graph, [8., 20., 5., 5.], [0., 0., 3., 7.],
                        source=source, sink=sink, get_all_path=collect,
                        use_bitwise=not large_ids, adaptive_buckets=adaptive,
                        bucket_joining=buckets, bucket_width=.5)
                    if collect:
                        self.assertGreater(len(result['all_feasible_paths']), 10000)

    def test_parallel_joining_default_and_one_thread(self):
        graph, source, sink = graph_case(6)
        kwargs = dict(source=source, sink=sink, get_all_path=True)
        default = solve_espprc(graph, [0.] * 4, [6., 12., 4., 4.], [0., 0., 3., 7.], **kwargs)
        disabled = solve_espprc(
            graph, [0.] * 4, [6., 12., 4., 4.], [0., 0., 3., 7.],
            parallel_joining=False, profile_joining=False, **kwargs)
        self.assertNotIn('joining_stats', default)
        self.assertEqual(default, disabled)
        self.compare_parallel(graph, [6., 12., 4., 4.], [0., 0., 3., 7.],
                              threads=(1,), **kwargs)

    def test_parallel_joining_directions_critical_and_threshold(self):
        graph, source, sink = graph_case(6)
        for direction in ('forward', 'backward', 'both'):
            for critical in (0, 1):
                for collect in (False, True):
                    with self.subTest(direction=direction, critical=critical, collect=collect):
                        self.compare_parallel(
                            graph, [6., 12., 4., 4.], [0., 0., 3., 7.],
                            source=source, sink=sink, get_all_path=collect,
                            direction=direction, critical_res=critical,
                            threshold=-10., bounds_pruning=True,
                            bucket_dominance=critical == 0,
                            bucket_extension=critical == 1)

    def test_parallel_joining_non_elementary_and_duplicate_edges(self):
        graph = nx.MultiDiGraph()
        for u, v, weight, resource in [(0, 1, 0., 1.), (1, 2, -3., 1.),
                                       (1, 2, -3., 2.), (2, 1, -3., 1.),
                                       (1, 3, 0., 1.), (2, 3, 0., 1.)]:
            graph.add_edge(u, v, weight=weight, res_cost=[resource, .5])
        for two_cycle in (False, True):
            for collect in (False, True):
                with self.subTest(two_cycle=two_cycle, collect=collect):
                    self.compare_parallel(
                        graph, [4., 4.], [0., 2.], source=0, sink=3,
                        elementary=False, two_cycle_elimination=two_cycle,
                        get_all_path=collect, bucket_width=.25)


def benchmark():
    """Print a reproducible comparison without creating report files."""
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
    graph, source, sink = graph_case(options.customers)
    reference = None
    for enabled in (False, True):
        kwargs = dict(source=source, sink=sink, bucket_joining=enabled,
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
                raise AssertionError('Pricing result or complete collected history changed')
        profiled = solve_espprc(graph, [0.] * 4, [8., 20., 5., 5.],
                                [0., 0., 3., 7.], profile_joining=True, **kwargs)
        stats = profiled.pop('joining_stats')
        if profiled != reference:
            raise AssertionError('Profiling changed pricing output')
        print(f"bucket_joining={enabled}: median={statistics.median(times):.6f}s, "
              f"cost={result['cost']}, paths={len(result['all_feasible_paths'])}, "
              f"joining_stats={stats}", flush=True)
    print('Complete pricing outputs match.')


if __name__ == '__main__':
    import sys
    if '--benchmark' in sys.argv[1:]:
        benchmark()
    else:
        unittest.main()
