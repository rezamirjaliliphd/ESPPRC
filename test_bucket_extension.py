"""Exact-output regressions and an optional benchmark for bucket extensions."""
import random
import unittest

import networkx as nx
from espprc import solve_espprc


def small_graph(seed=0):
    rng = random.Random(seed)
    graph = nx.DiGraph()
    graph.add_nodes_from(range(7))
    for i in range(6):
        for j in range(1, 7):
            if i != j:
                graph.add_edge(i, j, weight=float(rng.randrange(-5, 6)),
                               res_cost=[float(1 + (i + j) % 3), 1., .5 * (j % 2)])
    return graph


def pricing_graph(customers=14, set_mode=False):
    rng = random.Random(17)
    graph = nx.DiGraph()
    if set_mode:
        graph.add_nodes_from(range(65))
    offset = 1000 if set_mode else 0
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


def layered_graph(parallel_arcs=False):
    """Many incomparable visited sets, with short and bounded complete paths."""
    graph = nx.MultiDiGraph() if parallel_arcs else nx.DiGraph()
    layers = ([0], range(1, 9), range(9, 17), range(17, 25), [25])
    graph.add_nodes_from(range(26))
    for stage, (left, right) in enumerate(zip(layers, layers[1:])):
        for u in left:
            for v in right:
                duration = (-1., 0., 2.)[(u + v) % 3] if stage == 1 else 1.
                graph.add_edge(u, v, weight=-1. - .125 * ((u + v) % 3),
                               res_cost=[duration, 1., .5 * (v % 2)])
                if parallel_arcs and stage == 2:
                    # A failed extension marks this head unreachable. Copies
                    # must reproduce the exact serial arc order and mutation.
                    graph.add_edge(u, v, weight=-1., res_cost=[100., 1., .5])
    return graph


class BucketExtensionTests(unittest.TestCase):
    def compare_parallel(self, graph, limits, pi, minimum=None,
                         threads=(2, 4, 8), **kwargs):
        if minimum is None:
            minimum = [0.] * len(limits)
        reference = solve_espprc(
            graph, minimum, limits, pi, enable_parallel=False,
            parallel_joining=False, profile_extension=True, **kwargs)
        serial_stats = reference.pop('extension_stats')
        parallel_names = ('parallel_batches', 'prepared_labels',
                          'prepared_extensions', 'discarded_labels')
        self.assertEqual(serial_stats['parallel_labels'], 0)
        for name in parallel_names:
            self.assertEqual(serial_stats[name], 0)
        measurements = []
        for count in threads:
            with self.subTest(threads=count):
                output = solve_espprc(
                    graph, minimum, limits, pi, enable_parallel=True,
                    num_threads=count, parallel_joining=False,
                    profile_extension=True, **kwargs)
                stats = output.pop('extension_stats')
                for name, value in stats.items():
                    self.assertGreaterEqual(value, 0, name)
                    if not name.endswith('_seconds'):
                        self.assertIsInstance(value, int, name)
                for name in parallel_names:
                    self.assertIn(name, stats)
                # Preserve every returned field and collected record in order;
                # sorting paths would hide changed heap or dominance decisions.
                self.assertEqual(output, reference)
                if count == 1:
                    self.assertEqual(stats['parallel_labels'], 0)
                    for name in parallel_names:
                        self.assertEqual(stats[name], 0)
                measurements.append(stats)
        return reference, measurements

    def compare(self, graph, limits, pi, minimum=None, **kwargs):
        if minimum is None:
            minimum = [0.] * len(limits)
        outputs = []
        counters = []
        for enabled in (False, True):
            output = solve_espprc(graph, minimum, limits, pi,
                                  bucket_extension=enabled,
                                  profile_extension=True, **kwargs)
            stats = output.pop('extension_stats')
            for name, value in stats.items():
                self.assertGreaterEqual(value, 0, name)
                if not name.endswith('_seconds'):
                    self.assertIsInstance(value, int, name)
            if not enabled:
                self.assertEqual(stats['bucket_rejections'], 0)
                self.assertEqual(stats['cache_hits'], 0)
                self.assertEqual(stats['cache_rebuilds'], 0)
            if not stats['parallel_labels']:
                self.assertEqual(stats['eligible_extensions'],
                                 stats['full_extensions'] + stats['bucket_rejections'])
            outputs.append(output)
            counters.append(stats)
        self.assertEqual(outputs[0], outputs[1])
        return outputs[1], counters

    def test_random_direction_resources_bounds_and_masks(self):
        for seed in range(3):
            graph = small_graph(seed)
            for direction in ('forward', 'backward', 'both'):
                for critical in (0, 1):
                    for bitwise in (False, True):
                        for adaptive in (False, True):
                            with self.subTest(seed=seed, direction=direction,
                                              critical=critical, bitwise=bitwise,
                                              adaptive=adaptive):
                                self.compare(graph, [10., 6., 4.], [0., 0., 3.],
                                             source=0, sink=6, direction=direction,
                                             critical_res=critical, use_bitwise=bitwise,
                                             adaptive_buckets=adaptive, bucket_width=.5,
                                             bounds_pruning=seed % 2 == 0,
                                             get_all_path=seed % 2 == 1)

    def test_stats_opt_in_default_and_empty_graph(self):
        graph = small_graph()
        options = dict(source=0, sink=6, get_all_path=True)
        default = solve_espprc(graph, [0.] * 3, [10., 6., 4.], [0., 0., 3.], **options)
        explicit = solve_espprc(graph, [0.] * 3, [10., 6., 4.], [0., 0., 3.],
                                bucket_extension=True, profile_extension=False, **options)
        measured = solve_espprc(graph, [0.] * 3, [10., 6., 4.], [0., 0., 3.],
                                profile_extension=True, **options)
        self.assertNotIn('extension_stats', default)
        self.assertNotIn('extension_stats', explicit)
        self.assertGreater(measured.pop('extension_stats')['labels_processed'], 0)
        self.assertEqual(default, explicit)
        self.assertEqual(default, measured)
        empty = solve_espprc(nx.DiGraph(), [0.], [1.], [0.],
                             source=0, sink=1, profile_extension=True)
        self.assertTrue(all(value == 0 for value in empty['extension_stats'].values()))

    def test_nonpositive_critical_arcs_and_soft_noncritical_min(self):
        graph = nx.DiGraph()
        for u, v, critical, other in [(0, 1, 0., 1.), (0, 2, 1., 2.),
                                      (1, 2, -1., -1.), (1, 3, 2., 1.),
                                      (2, 3, 0., 1.), (2, 4, 3., 2.),
                                      (3, 4, 1., 1.)]:
            graph.add_edge(u, v, weight=float(v - u - 3), res_cost=[critical, other, .5])
        for direction in ('forward', 'backward', 'both'):
            for lower in (3., 0., -2.):
                with self.subTest(direction=direction, noncritical_lower=lower):
                    self.compare(graph, [8., 8., 4.], [0., 0., 3.],
                                 minimum=[-2., lower, 0.], source=0, sink=4,
                                 direction=direction, get_all_path=True, bucket_width=.25)

    def test_adaptation_and_large_node_set_representation(self):
        for set_mode, adaptive in ((False, False), (False, True), (True, True)):
            with self.subTest(set_mode=set_mode, adaptive=adaptive):
                graph, source, sink = pricing_graph(set_mode=set_mode)
                result, (_, stats) = self.compare(
                    graph, [8., 20., 5., 5.], [0., 0., 3., 7.],
                    source=source, sink=sink, use_bitwise=not set_mode,
                    adaptive_buckets=adaptive, bucket_width=.5, get_all_path=True)
                self.assertGreater(len(result['all_feasible_paths']), 10000)
                self.assertGreater(stats['cache_rebuilds'], 0)
                self.assertGreater(stats['cache_hits'], 0)

    def assert_serial_fallback(self, graph, limits, pi, requested_threads, **kwargs):
        outputs = []
        counters = []
        for enabled in (False, True):
            result = solve_espprc(graph, [0.] * len(limits), limits, pi,
                                  enable_parallel=enabled,
                                  num_threads=requested_threads,
                                  bucket_extension=True, profile_extension=True,
                                  adaptive_buckets=False, bucket_width=.25, **kwargs)
            stats = result.pop('extension_stats')
            self.assertEqual(stats['parallel_labels'], 0)
            self.assertEqual(stats['eligible_extensions'],
                             stats['full_extensions'] + stats['bucket_rejections'])
            outputs.append(result)
            # Wall-clock timings naturally differ. Every work/cache counter
            # should match the optimized serial execution exactly.
            counters.append({key: value for key, value in stats.items()
                             if not key.endswith('_seconds')})
        self.assertEqual(outputs[0], outputs[1])
        self.assertEqual(counters[0], counters[1])
        return counters[0]

    def test_parallel_option_small_degree_uses_optimized_serial(self):
        # Every degree is below 32. The scheduling guard must retain the same
        # cached serial path for requests of 1/2/4/8 workers.
        graph = small_graph()
        seen_hits = seen_rejections = 0
        for direction in ('forward', 'backward', 'both'):
            for bitwise in (False, True):
                for collect in (False, True):
                    for requested in (1, 2, 4, 8):
                        with self.subTest(direction=direction, bitwise=bitwise,
                                          collect=collect, requested=requested):
                            stats = self.assert_serial_fallback(
                                graph, [10., 6., 4.], [0., 0., 3.], requested,
                                source=0, sink=6, direction=direction,
                                use_bitwise=bitwise, get_all_path=collect)
                            seen_hits += stats['cache_hits']
                            seen_rejections += stats['bucket_rejections']
        self.assertGreater(seen_hits, 0, 'fixture never reused an extension proof')
        self.assertGreater(seen_rejections, 0, 'fixture never rejected an infeasible extension')

    def test_one_requested_thread_uses_optimized_serial_at_wide_node(self):
        # The middle node has 32 incoming and 32 outgoing arcs. Sixteen labels
        # reach it in either direction, exercising cache reuse. The expensive
        # reverse arcs are always resource-infeasible; requests for one thread
        # must still retain cached rejection at this otherwise parallelizable node.
        graph = nx.DiGraph()
        source, middle, sink = 0, 17, 34
        graph.add_nodes_from(range(35))
        for i in range(1, 17):
            graph.add_edge(source, i, weight=-1., res_cost=[1., 1., .5 * (i % 2)])
            graph.add_edge(i, middle, weight=-1., res_cost=[1., 1., 0.])
            graph.add_edge(middle, i, weight=0., res_cost=[100., 0., 0.])
        for j in range(18, 34):
            graph.add_edge(middle, j, weight=-1., res_cost=[1., 1., .5 * (j % 2)])
            graph.add_edge(j, sink, weight=-1., res_cost=[1., 1., 0.])
            graph.add_edge(j, middle, weight=0., res_cost=[100., 0., 0.])
        self.assertEqual(graph.in_degree(middle), 32)
        self.assertEqual(graph.out_degree(middle), 32)
        seen_hits = seen_rejections = 0
        for direction in ('forward', 'backward', 'both'):
            for bitwise in (False, True):
                for collect in (False, True):
                    with self.subTest(direction=direction, bitwise=bitwise, collect=collect):
                        stats = self.assert_serial_fallback(
                            graph, [4., 8., 4.], [0., 0., 2.], 1,
                            source=source, sink=sink, direction=direction,
                            use_bitwise=bitwise, get_all_path=collect)
                        seen_hits += stats['cache_hits']
                        seen_rejections += stats['bucket_rejections']
        self.assertGreater(seen_hits, 0, 'wide fixture never reused an extension proof')
        self.assertGreater(seen_rejections, 0, 'wide fixture never used cached rejection')

    def test_parallel_arcs_keep_failure_side_effect_order(self):
        graph = nx.MultiDiGraph()
        for u, v, critical in [(0, 1, 1.), (0, 1, 100.),
                              (0, 2, 1.), (1, 2, 1.),
                              (1, 3, 1.), (1, 3, 100.), (2, 3, 1.)]:
            graph.add_edge(u, v, weight=-1., res_cost=[critical, .5])
        for direction in ('forward', 'backward', 'both'):
            with self.subTest(direction=direction):
                self.compare(graph, [4., 4.], [0., 3.], source=0, sink=3,
                             direction=direction, get_all_path=True, bucket_width=.25)

    def test_resource_auto_selection_and_non_elementary(self):
        self.compare(small_graph(), [10., 6., 4.], [0., 0., 3.],
                     source=0, sink=6, find_critical_res=True, get_all_path=True)
        for two_cycle in (False, True):
            self.compare(small_graph(), [10., 6., 4.], [0., 0., 3.],
                         source=0, sink=6, elementary=False,
                         two_cycle_elimination=two_cycle, get_all_path=True)

    def test_parallel_batches_pricing_pools_masks_and_set_fallback(self):
        for set_mode, adaptive, method in ((False, False, 'unprocessed'),
                                           (False, True, 'processed'),
                                           (True, True, 'generated')):
            graph, source, sink = pricing_graph(set_mode=set_mode)
            for collect in (False, True):
                with self.subTest(set_mode=set_mode, adaptive=adaptive,
                                  method=method, collect=collect):
                    result, _ = self.compare_parallel(
                        graph, [8., 20., 5., 5.], [0., 0., 3., 7.],
                        source=source, sink=sink, use_bitwise=not set_mode,
                        adaptive_buckets=adaptive, bucket_width=.5,
                        method=method, get_all_path=collect)
                    if collect:
                        self.assertTrue(result['all_feasible_paths'])

    def test_parallel_batches_wide_pricing_graph_with_short_paths(self):
        # Wide adjacency triggers batching, while a three-arc horizon keeps
        # this regression small. Native tests assert actual OpenMP batch use.
        graph, source, sink = pricing_graph(customers=34)
        self.assertGreaterEqual(graph.out_degree(source), 32)
        for collect in (False, True):
            with self.subTest(collect=collect):
                self.compare_parallel(
                    graph, [3., 8., 4., 4.], [0., 0., 3., 7.],
                    source=source, sink=sink, use_bitwise=True,
                    get_all_path=collect, bucket_width=.5)

    def test_parallel_batches_direction_policies_and_critical_resource(self):
        graph = layered_graph()
        configurations = (('forward', 'unprocessed', 0),
                          ('backward', 'unprocessed', 0),
                          ('both', 'unprocessed', 0),
                          ('both', 'generated', 1),
                          ('both', 'processed', 0))
        for direction, method, critical in configurations:
            for collect in (False, True):
                with self.subTest(direction=direction, method=method,
                                  critical=critical, collect=collect):
                    self.compare_parallel(
                        graph, [7., 4., 5.], [0., 0., 3.], minimum=[-2., 0., 0.],
                        source=0, sink=25, direction=direction, method=method,
                        critical_res=critical, get_all_path=collect,
                        use_bitwise=method != 'processed',
                        adaptive_buckets=method != 'unprocessed', bucket_width=.25)

    def test_parallel_batches_nonpositive_arcs_and_soft_resource_minima(self):
        graph = layered_graph()
        for direction in ('forward', 'backward', 'both'):
            for lower in (0., 3.):
                with self.subTest(direction=direction, noncritical_lower=lower):
                    self.compare_parallel(
                        graph, [7., 4., 5.], [0., 0., 3.],
                        minimum=[-2., lower, 0.], source=0, sink=25,
                        direction=direction, get_all_path=True,
                        bucket_extension=lower == 0., bucket_width=.25)

    def test_parallel_batches_duplicate_arc_failure_order(self):
        for direction in ('forward', 'backward', 'both'):
            for collect in (False, True):
                with self.subTest(direction=direction, collect=collect):
                    self.compare_parallel(
                        layered_graph(parallel_arcs=True), [7., 4., 5.], [0., 0., 3.],
                        minimum=[-2., 0., 0.], source=0, sink=25,
                        direction=direction, get_all_path=collect, bucket_width=.25)

    def test_parallel_batches_threshold_and_auto_resource_selection(self):
        graph = layered_graph()
        for collect in (False, True):
            for critical in (None, 0):
                with self.subTest(collect=collect, auto_critical=critical is None):
                    self.compare_parallel(
                        graph, [7., 4., 5.], [0., 0., 3.],
                        minimum=[-2., 0., 0.], source=0, sink=25,
                        find_critical_res=critical is None, critical_res=critical or 0,
                        get_all_path=collect, threshold=-3., bounds_pruning=True)

    def test_parallel_batches_non_elementary_two_cycle(self):
        graph = nx.DiGraph()
        for u, v, cost in ((0, 1, 0.), (1, 2, -3.), (2, 1, -3.),
                            (1, 3, 0.), (2, 3, 0.)):
            graph.add_edge(u, v, weight=cost, res_cost=[1., .5])
        for two_cycle in (False, True):
            with self.subTest(two_cycle=two_cycle):
                self.compare_parallel(
                    graph, [4., 4.], [0., 2.], source=0, sink=3,
                    elementary=False, two_cycle_elimination=two_cycle,
                    get_all_path=True, bucket_dominance=False)


def benchmark():
    """Compare extension filtering without creating report files."""
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
    graph, source, sink = pricing_graph(options.customers)
    reference = None
    for enabled in (False, True):
        kwargs = dict(source=source, sink=sink, bucket_extension=enabled,
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
                raise AssertionError('Pricing result or generated paths changed')
        measured = solve_espprc(graph, [0.] * 4, [8., 20., 5., 5.],
                                [0., 0., 3., 7.], profile_extension=True, **kwargs)
        stats = measured.pop('extension_stats')
        if measured != reference:
            raise AssertionError('Profiling changed pricing output')
        print(f"bucket_extension={enabled}: median={statistics.median(times):.6f}s, "
              f"full_extensions={stats['full_extensions']:,}, "
              f"bucket_rejections={stats['bucket_rejections']:,}, "
              f"cache_hits={stats['cache_hits']:,}, "
              f"generation_seconds={stats['generation_seconds']:.6f}, "
              f"cost={result['cost']}, paths={len(result['all_feasible_paths'])}", flush=True)
    print('Complete pricing outputs match.')


if __name__ == '__main__':
    import sys
    if '--benchmark' in sys.argv[1:]:
        benchmark()
    else:
        unittest.main()
