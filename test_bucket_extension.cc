// Differential tests for conservative bucket-assisted resource extension.
// Compile all six core sources with -DESPPRC_TESTING.
#include "src/cc/bidirectional.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

using bidirectional::AdjVertex;
using bidirectional::BWD;
using bidirectional::Directions;
using bidirectional::FWD;
using bidirectional::Params;
using bidirectional::Search;
using labelling::Label;

namespace bidirectional {
struct BucketExtensionTestAccess {
  static Params& params(BiDirectional& solver) { return *solver.params_ptr_; }
  static Search& search(BiDirectional& solver, Directions direction) {
    return *solver.getSearchPtr(direction);
  }
  static void initialize(BiDirectional& solver) {
    solver.best_label_ = std::make_shared<Label>();
    solver.initResourceBounds();
    for (Directions direction : {FWD, BWD}) {
      solver.initSearch(direction);
      auto& state = search(solver, direction);
      state.current_label = std::make_shared<Label>();
      state.intermediate_label = std::make_shared<Label>();
      state.initializeDurationBuckets(solver.params_ptr_->critical_res,
          solver.max_res[solver.params_ptr_->critical_res], .25,
          solver.params_ptr_->adaptive_buckets, solver.params_ptr_->bucket_dominance);
    }
  }
  static void seed(BiDirectional& solver, Directions direction, double cost,
                   const std::vector<double>& resources, const std::vector<int>& path,
                   bool retain) {
    auto& state = search(solver, direction);
    const int node = solver.graph_ptr_->getNodeIdFromUserId(path.back());
    Label label(cost, {node, path.back()}, resources, path, solver.params_ptr_.get());
    if (retain) state.pushEfficientLabel(node, label);
    state.pushUnprocessedLabel(label);
    state.addVisitedVertex(node);
    if (!state.best_labels[node] || cost < state.best_labels[node]->weight)
      state.replaceBestLabel(node, label);
  }
  static void pop(BiDirectional& solver, Directions direction) {
    auto& state = search(solver, direction);
    state.replaceCurrentLabel(labelling::getNextLabel(state.unprocessed_labels.get(), direction));
  }
  static void move(BiDirectional& solver, Directions direction) { solver.move(direction); }
  static bool currentPrepared(BiDirectional& solver, Directions direction) {
    const auto& cache = solver.extension_prefetch_[direction == FWD ? 0 : 1];
    return cache.pending.count(search(solver, direction).current_label->extension_id) != 0;
  }
  static void tighten(BiDirectional& solver, Directions direction) {
    const int critical = solver.params_ptr_->critical_res;
    if (direction == FWD) solver.max_res_curr_[critical] = 3.;
    else solver.min_res_curr_[critical] = 7.;
  }
  static const std::vector<double>& maximum(BiDirectional& solver) { return solver.max_res_curr_; }
  static const std::vector<double>& minimum(BiDirectional& solver) { return solver.min_res_curr_; }
  static const Label& best(BiDirectional& solver) { return *solver.best_label_; }
  static auto scalars(BiDirectional& solver) {
    return std::make_tuple(solver.iteration_, solver.primal_st_bound_,
        solver.best_base_cost_, solver.best_extra_cost_, solver.terminated_early_w_st_path_,
        solver.time_limit_reached_);
  }
};
}

namespace {
std::string context;
std::uint64_t queries = 0, arcs_verified = 0, fast_rejections = 0;
std::uint64_t solver_moves = 0, consumed_preparations = 0;
void require(bool ok, const std::string& message) {
  if (!ok) throw std::runtime_error(context + ": " + message);
}
bool same_double(double a, double b) {
  return a == b || (std::isnan(a) && std::isnan(b));
}
void same_label(const Label& a, const Label& b, bool same_params = true) {
  require(same_double(a.weight, b.weight), "weight differs");
  require(a.vertex.lemon_id == b.vertex.lemon_id && a.vertex.user_id == b.vertex.user_id,
          "vertex differs");
  require(a.resource_consumption.size() == b.resource_consumption.size(), "resource count differs");
  for (size_t r = 0; r < a.resource_consumption.size(); ++r)
    require(same_double(a.resource_consumption[r], b.resource_consumption[r]), "resource value differs");
  require(a.partial_path == b.partial_path, "path differs");
  require(a.unreachable_mask == b.unreachable_mask, "parent/child unreachable mask differs");
  require(a.unreachable_nodes == b.unreachable_nodes, "parent/child unreachable set differs");
  if (same_params) require(a.params_ptr == b.params_ptr, "parameter pointer differs");
  require(same_double(a.phi, b.phi), "phi differs");
}
bool structural_gate(const Label& label, const AdjVertex& edge) {
  return (!label.params_ptr->elementary || !label.isNodeUnreachable(edge.vertex.user_id)) &&
         (label.partial_path.size() <= 1 || label.checkPathExtension(edge.vertex.user_id));
}

struct Harness {
  Params params;
  Search search;
  int offset;
  double horizon;
  Harness(Directions direction, int critical, bool adaptive, int mode,
          double width = 5., double max_resource = 100.)
      : search(direction), offset(mode == 2 ? 100 : 0), horizon(max_resource) {
    params.critical_res = critical;
    params.elementary = mode <= 2;
    params.use_bitwise_elementary = mode == 0 || mode == 2;
    params.max_nodes = mode == 2 ? 256 : 64;
    params.two_cycle_elimination = mode == 4;
    params.bucket_extension = true;
    params.profile_extension = true;
    search.efficient_labels.resize(3);
    search.initializeDurationBuckets(critical, max_resource, width, adaptive);
  }
  Label label(double stored, double payload = 0., double sri = 0.) {
    std::vector<double> resources{payload, sri, 0., 0.};
    resources[3] = resources[params.critical_res];
    resources[params.critical_res] = stored;
    return Label(-3., {1, offset + 1}, resources,
                 {offset, offset + 5, offset + 1}, &params, 2.5);
  }
  AdjVertex edge(double increment, int target = 2, double other = 0.) {
    std::vector<double> resources(4, 0.);
    resources[(params.critical_res + 1) % 4] = other;
    resources[params.critical_res] = increment;
    return AdjVertex({2, offset + target}, -2.5, resources);
  }
};

void compare_query(Harness& h, const Label& initial,
                   const std::vector<AdjVertex>& edges,
                   const std::vector<double>& maximum,
                   const std::vector<double>& minimum,
                   bool expect_no_flags = false) {
  const auto flags = h.search.bucketExtensionMask(initial, edges, maximum, minimum);
  require(flags.size() == edges.size(), "wrong flag count");
  ++queries;
  Label original = initial, accelerated = initial;
  for (size_t i = 0; i < edges.size(); ++i) {
    require(flags[i] <= 1, "invalid feasibility flag");
    if (expect_no_flags) require(flags[i] == 0, "fallback unexpectedly filtered an edge");
    // Independently verify the proof before structural checks: every flag must
    // imply failure of the ordinary resource extension, for this exact label.
    Label proof_parent = initial;
    const Label proof_child = proof_parent.extend(edges[i], h.search.direction, maximum, minimum);
    if (flags[i]) {
      require(proof_child.vertex.lemon_id == -1, "feasible extension classified infeasible");
      ++fast_rejections;
    }
    const bool base_gate = structural_gate(original, edges[i]);
    const bool fast_gate = structural_gate(accelerated, edges[i]);
    require(base_gate == fast_gate, "structural gating changed after earlier failure");
    if (base_gate) {
      const Label expected = original.extend(edges[i], h.search.direction, maximum, minimum);
      const Label actual = accelerated.extend(edges[i], h.search.direction, maximum, minimum, flags[i]);
      same_label(actual, expected);
    }
    // A fast failure must mark the same destination unreachable, even when
    // a later edge repeats that destination with a feasible resource value.
    same_label(accelerated, original);
    // Parallel workers operate on independent copies. Failure-side effects
    // must stay local, just as in the original parallel extension branch.
    Label original_copy = initial, accelerated_copy = initial;
    if (structural_gate(initial, edges[i])) {
      const Label expected = original_copy.extend(edges[i], h.search.direction, maximum, minimum);
      const Label actual = accelerated_copy.extend(edges[i], h.search.direction, maximum, minimum, flags[i]);
      same_label(actual, expected);
      same_label(accelerated_copy, original_copy);
    }
    ++arcs_verified;
  }
}

void random_states_and_moving_bounds() {
  for (Directions direction : {FWD, BWD}) {
    for (int critical : {0, 1, 3}) {
      for (bool adaptive : {false, true}) {
        for (int mode = 0; mode < 5; ++mode) {
          context = "random direction=" + std::to_string(direction) +
                    " critical=" + std::to_string(critical) +
                    " adaptive=" + std::to_string(adaptive) + " mode=" + std::to_string(mode);
          Harness h(direction, critical, adaptive, mode, .5);
          std::mt19937 rng(173);
          std::vector<AdjVertex> edges;
          for (double increment : {-100., -2., -.25, 0., .25, 1., 2., 5., 40., 100.})
            edges.push_back(h.edge(increment, 2 + edges.size(), int(rng() % 15) - 7));
          // Repeat destinations, include current node and predecessor, then a
          // zero-cost arc: structural rules must remain in their original order.
          edges.push_back(h.edge(1000., 20));
          edges.push_back(h.edge(0., 20));
          edges.push_back(h.edge(1000., 1));
          edges.push_back(h.edge(1000., 5));
          for (int i = 0; i < 520; ++i) {
            const double stored = (rng() % 400) * .25;
            Label label = h.label(stored, (rng() % 10) * .5, (rng() % 6) * .5);
            if (i % 5 == 0) label.markNodeUnreachable(h.offset + 7);
            // Retained pools adapt during insertion; test queued/current copies
            // against the current partition without relying on their membership.
            if (i < 320) h.search.pushEfficientLabel(1, label);
            if (adaptive && i % 71 == 0) h.search.adaptDurationBuckets(1);
            std::vector<double> maximum(4, 15.), minimum(4, 0.);
            maximum[critical] = 15. + rng() % 85;
            minimum[critical] = rng() % 12;
            const size_t other = (critical + 1) % 4;
            minimum[other] = i % 3 == 0 ? 5. : i % 3 == 1 ? -3. : 0.;
            compare_query(h, label, edges, maximum, minimum);
            if (i % 13 == 0) {
              // Same node/bucket/edges, but tighter moving halfway bounds.
              maximum[critical] = std::max(minimum[critical], maximum[critical] - 10.);
              compare_query(h, label, edges, maximum, minimum);
            }
          }
        }
      }
    }
  }
}

void explicit_nonpositive_backward_and_soft_bounds() {
  for (Directions direction : {FWD, BWD}) {
    for (int mode : {0, 1, 2, 3, 4}) {
      context = "nonpositive critical and soft minimum";
      Harness h(direction, 1, false, mode, .125, 10.);
      std::vector<AdjVertex> edges{h.edge(-3., 2), h.edge(0., 3), h.edge(.5, 4),
                                   h.edge(1., 6), h.edge(1.5, 7)};
      std::vector<double> maximum{10., 10., 10., 10.}, minimum{0., 0., 0., 0.};
      for (double value : {0., .5, 1., std::nextafter(1., 0.), std::nextafter(1., 2.), 10.})
        compare_query(h, h.label(value), edges, maximum, minimum);
      if (direction == BWD) {
        Label one = h.label(1.);
        const auto zero_result = one.extend(edges[1], BWD, maximum, minimum);
        require(zero_result.vertex.lemon_id != -1 && zero_result.resource_consumption[1] == 0.,
                "baseline zero backward edge must subtract one");
        Label half = h.label(.5);
        require(half.extend(edges[1], BWD, maximum, minimum).vertex.lemon_id == -1,
                "baseline half-unit backward state must reject zero edge");
      }
      Label current = h.label(5.);
      // Native cache contract: adjacency stays immutable until reinitialize.
      h.search.initializeDurationBuckets(1, 10., .125, false);
      edges = {h.edge(0., 2, 1.), h.edge(0., 3, -1.), h.edge(0., 4, -6.)};
      minimum[2] = 5.; // Positive noncritical minimum is ignored in soft check.
      compare_query(h, current, edges, maximum, minimum);
      require(current.extend(edges[0], direction, maximum, minimum).vertex.lemon_id != -1,
              "positive noncritical soft minimum unexpectedly enforced");
      minimum[2] = 0.;
      compare_query(h, current, edges, maximum, minimum);
      minimum[2] = -5.;
      compare_query(h, current, edges, maximum, minimum);
    }
  }
}

void ghost_heap_label_and_repartition() {
  for (Directions direction : {FWD, BWD}) {
    context = "heap label deleted from retained pool and adaptive repartition";
    Harness h(direction, 1, true, 0, 5., 1000.);
    const auto stored = [&](double consumed) { return direction == FWD ? consumed : 1000. - consumed; };
    for (int i = 0; i < 512; ++i) {
      auto label = h.label(stored(i));
      label.weight = 1000. - i;
      h.search.pushEfficientLabel(1, label);
    }
    const Label queued = h.search.efficient_labels[1][400];
    h.search.pushUnprocessedLabel(queued);
    std::vector<AdjVertex> edges{h.edge(-100., 2), h.edge(0., 3), h.edge(1., 4),
                                 h.edge(50., 6), h.edge(500., 7)};
    std::vector<double> maximum(4, 1000.), minimum(4, 0.);
    compare_query(h, queued, edges, maximum, minimum);
    Label dominating = h.label(stored(0.));
    dominating.weight = -1.;
    require(!h.search.runDominance(1, dominating, true), "dominating label rejected");
    require(h.search.efficient_labels[1].empty(), "queued label not removed from retained pool");
    h.search.pushEfficientLabel(1, dominating);
    h.search.adaptDurationBuckets(1);
    compare_query(h, queued, edges, maximum, minimum);
    const Label popped = labelling::getNextLabel(h.search.unprocessed_labels.get(), direction);
    same_label(popped, queued);
    maximum[1] = 620.;
    minimum[1] = 390.;
    compare_query(h, popped, edges, maximum, minimum);
    // Empty/repopulated intervals and a new set of finer boundaries must not
    // reuse a cache entry from the previous partition with the same index.
    for (int i = 0; i < 512; ++i) {
      auto label = h.label(stored(i / 4.));
      label.weight = i;
      h.search.pushEfficientLabel(1, label);
    }
    h.search.adaptDurationBuckets(1);
    compare_query(h, queued, edges, maximum, minimum);
  }
}

struct ResetCallback : bidirectional::REFCallback {
  mutable size_t calls = 0;
  bool reject = false;
  std::vector<double> REF_fwd(const std::vector<double>& r, const int&, const int&,
      const std::vector<double>&, const std::vector<int>&, const double&) const override {
    ++calls;
    return std::vector<double>(r.size(), reject ? 1000. : 1.);
  }
  std::vector<double> REF_bwd(const std::vector<double>& r, const int&, const int&,
      const std::vector<double>& e, const std::vector<int>& p, const double& w) const override {
    return REF_fwd(r, 0, 0, e, p, w);
  }
};
void callback_fallback() {
  for (Directions direction : {FWD, BWD}) {
    context = "callback exact fallback";
    Harness h(direction, 1, false, 0, .25);
    ResetCallback callback;
    h.params.ref_callback = &callback;
    std::vector<AdjVertex> edges{h.edge(10000., 2), h.edge(0., 3), h.edge(-10000., 4)};
    std::vector<double> maximum(4, 10.), minimum(4, 0.);
    for (bool reject : {false, true}) {
      callback.reject = reject;
      const auto before = callback.calls;
      compare_query(h, h.label(5.), edges, maximum, minimum, true);
      require(callback.calls > before, "custom REF was bypassed");
    }
    h.params.ref_callback = nullptr;
  }
}

void endpoint_rounding_and_cache_hits() {
  for (Directions direction : {FWD, BWD}) {
    context = "fixed boundary and repeated cache queries";
    Harness h(direction, 1, false, 0, 5., 100.);
    std::vector<AdjVertex> edges{h.edge(0., 2), h.edge(5., 3), h.edge(10., 4), h.edge(1000., 6)};
    std::vector<double> maximum(4, 100.), minimum(4, 0.);
    maximum[1] = 10.;
    for (double stored : {std::nextafter(5., 0.), 5., std::nextafter(5., 10.),
                          std::nextafter(10., 0.), 10., std::nextafter(10., 20.)}) {
      const Label label = h.label(stored);
      compare_query(h, label, edges, maximum, minimum);
      compare_query(h, label, edges, maximum, minimum);
    }
    require(h.search.extension_stats.cache_rebuilds > 0, "cache was never built");
    require(h.search.extension_stats.cache_hits > 0, "repeated query did not hit cache");
  }
  context = "backward critical subtraction cancellation";
  Harness h(BWD, 1, false, 0, 5., 1e16);
  std::vector<AdjVertex> edges{h.edge(0., 2), h.edge(1., 3), h.edge(2., 4), h.edge(3., 6)};
  std::vector<double> maximum(4, 10.), minimum(4, 0.);
  maximum[1] = 2.;
  for (double stored : {std::nextafter(2., 0.), 2., std::nextafter(2., 4.)})
    compare_query(h, h.label(stored), edges, maximum, minimum);
}

using Solver = bidirectional::BiDirectional;
using Access = bidirectional::BucketExtensionTestAccess;

void same_sequence(const std::vector<Label>& a, const std::vector<Label>& b) {
  require(a.size() == b.size(), "label sequence length differs");
  for (size_t i = 0; i < a.size(); ++i) same_label(a[i], b[i], false);
}

void same_search(Search& a, Search& b) {
  require(std::tie(a.stop, a.bound_exceeded, a.unprocessed_count, a.processed_count, a.generated_count) ==
          std::tie(b.stop, b.bound_exceeded, b.unprocessed_count, b.processed_count, b.generated_count),
          "search counters or stopping state differ");
  require(a.visited_vertices == b.visited_vertices, "visited search vertices differ");
  require(*a.lower_bound_weight == *b.lower_bound_weight, "pricing lower bounds differ");
  require(a.duration_buckets == b.duration_buckets, "retained bucket membership differs");
  same_label(*a.current_label, *b.current_label, false);
  same_label(*a.intermediate_label, *b.intermediate_label, false);
  same_sequence(*a.unprocessed_labels, *b.unprocessed_labels);
  require(a.efficient_labels.size() == b.efficient_labels.size(), "retained node count differs");
  for (size_t node = 0; node < a.efficient_labels.size(); ++node) {
    same_sequence(a.efficient_labels[node], b.efficient_labels[node]);
    require(bool(a.best_labels[node]) == bool(b.best_labels[node]), "best-label presence differs");
    if (a.best_labels[node]) same_label(*a.best_labels[node], *b.best_labels[node], false);
  }
  const auto& x = a.extension_stats;
  const auto& y = b.extension_stats;
  require(std::tie(x.labels_processed, x.arcs_considered, x.eligible_extensions,
                   x.full_extensions, x.bucket_rejections, x.infeasible_extensions,
                   x.cache_hits, x.cache_rebuilds) ==
          std::tie(y.labels_processed, y.arcs_considered, y.eligible_extensions,
                   y.full_extensions, y.bucket_rejections, y.infeasible_extensions,
                   y.cache_hits, y.cache_rebuilds), "logical extension/cache counters differ");
}

void same_solver(Solver& serial, Solver& parallel) {
  require(Access::maximum(serial) == Access::maximum(parallel) &&
          Access::minimum(serial) == Access::minimum(parallel), "moving resource bounds differ");
  require(Access::scalars(serial) == Access::scalars(parallel), "solver bound/termination state differs");
  same_label(Access::best(serial), Access::best(parallel), false);
  same_sequence(serial.getAllFeasibleSolutions(), parallel.getAllFeasibleSolutions());
  require(serial.getAllFeasiblePaths() == parallel.getAllFeasiblePaths(), "collected path order differs");
  for (Directions direction : {FWD, BWD})
    same_search(Access::search(serial, direction), Access::search(parallel, direction));
}

std::unique_ptr<Solver> seeded_solver(Directions direction, int critical, int mode,
                                    bool parallel, int threads,
                                    bidirectional::REFCallback* callback = nullptr) {
  const int offset = mode == 2 ? 1000 : 0;
  std::vector<int> nodes;
  for (int i = 0; i < 64; ++i) nodes.push_back(offset + i);
  auto solver = std::make_unique<Solver>(64, 33, offset, offset + 63,
      std::vector<double>{10., 10., 10.}, std::vector<double>{0., 0., 0.},
      std::vector<double>{0., 0., 3.});
  solver->addNodes(nodes);
  // LEMON traverses these arcs in reverse insertion order. At actual use of a
  // prefetched label, tightening makes the first arc to24 infeasible while the
  // second remains numerically feasible. The real parent's mutation must block it.
  std::vector<std::tuple<int, double, double>> arcs{
      {24, 2., .25}, {24, .5, .25}, {25, 1., .25}, {26, 0., .25},
      {27, -1., 100.}, {28, 20., .25}, {28, .5, .25}, {29, .5, .25},
      {30, 1.5, .25}, {31, 2.5, .25}, {32, 1., .25}, {33, .75, .25},
      {direction == FWD ? 63 : 0, 1., .25}};
  for (int target = 34; target < 44; ++target) {
    // These pairs raise the node degree beyond the scheduling guard. Each
    // first arc becomes infeasible only after tightening the halfway bound;
    // the later short arc must then respect its parent's unreachable marker.
    arcs.emplace_back(target, 6., .25);
    arcs.emplace_back(target, .5, .25);
  }
  require(arcs.size() == 33, "seeded node must exceed the 32-arc batch guard");
  for (auto it = arcs.rbegin(); it != arcs.rend(); ++it) {
    const auto [target, duration, payload] = *it;
    std::vector<double> resources{0., 0., .5};
    resources[critical] = duration;
    resources[1 - critical] = payload;
    solver->addEdge(offset + (direction == FWD ? 1 : target),
                    offset + (direction == FWD ? target : 1), -.25, resources);
  }
  auto& params = Access::params(*solver);
  params.critical_res = critical;
  params.enable_parallel = parallel;
  params.num_threads = threads;
  params.profile_extension = true;
  params.use_bitwise_elementary = mode != 1;
  params.max_nodes = mode == 2 ? 2048 : 64;
  params.adaptive_buckets = mode != 0;
  params.ref_callback = callback;
  Access::initialize(*solver);
  for (int i = 0; i < 80; ++i) {
    std::vector<double> resources{1., 1., .5};
    resources[critical] = direction == FWD ? 2. : 8.;
    Access::seed(*solver, direction, -1. - .125 * (i % 3), resources,
        {offset + (direction == FWD ? 0 : 63), offset + 2 + i % 16,
         offset + 18 + i / 16, offset + 1}, i % 7 != 0);
  }
  auto& state = Access::search(*solver, direction);
  require(state.unprocessed_labels->size() == 80, "fixture failed to seed a wide queue");
  size_t retained = 0;
  for (const auto& labels : state.efficient_labels) retained += labels.size();
  require(retained < state.unprocessed_labels->size(), "fixture lacks stale heap labels");
  Access::pop(*solver, direction);
  return solver;
}

void speculative_moves_preserve_state_and_tightened_bounds() {
  for (Directions direction : {FWD, BWD}) {
    for (int critical : {0, 1}) {
      for (int mode : {0, 1, 2}) {
        for (int threads : {2, 4, 8}) {
          context = "seeded speculative moves direction=" + std::to_string(direction) +
                    " critical=" + std::to_string(critical) + " mode=" + std::to_string(mode) +
                    " threads=" + std::to_string(threads);
          auto serial = seeded_solver(direction, critical, mode, false, threads);
          auto parallel = seeded_solver(direction, critical, mode, true, threads);
          same_solver(*serial, *parallel);
          bool tightened = false;
          for (int step = 0; step < 96 && !Access::search(*serial, direction).stop; ++step) {
            bool tighten_now = false;
#ifdef _OPENMP
            tighten_now = step > 0 && !tightened && Access::currentPrepared(*parallel, direction) &&
                Access::search(*parallel, direction).current_label->vertex.user_id == (mode == 2 ? 1001 : 1);
#else
            tighten_now = step > 0 && !tightened &&
                Access::search(*parallel, direction).current_label->vertex.user_id == (mode == 2 ? 1001 : 1);
#endif
            if (tighten_now) {
              Access::tighten(*serial, direction);
              Access::tighten(*parallel, direction);
              tightened = true;
            }
            auto serial_parent = Access::search(*serial, direction).current_label;
            auto parallel_parent = Access::search(*parallel, direction).current_label;
            Access::move(*serial, direction);
            Access::move(*parallel, direction);
            same_label(*serial_parent, *parallel_parent, false);
            if (tighten_now) {
              require(parallel_parent->isNodeUnreachable(mode == 2 ? 1024 : 24),
                      "tightened failure did not mark real parent before its repeated arc");
              for (int target = 34; target < 44; ++target)
                require(parallel_parent->isNodeUnreachable((mode == 2 ? 1000 : 0) + target),
                        "wide-node repeated arc missed its newly infeasible predecessor");
            }
            same_solver(*serial, *parallel);
            ++solver_moves;
          }
          require(tightened, "fixture never tightened a future prepared label");
          const auto& stats = Access::search(*parallel, direction).extension_stats;
#ifdef _OPENMP
          require(stats.parallel_batches > 0 && stats.prepared_labels >= 64 && stats.parallel_labels > 1,
                  "seeded fixture did not consume actual speculative preparations");
#else
          require(stats.parallel_batches == 0 && stats.prepared_labels == 0 && stats.parallel_labels == 0,
                  "no-OpenMP build reported speculative worker execution");
#endif
          consumed_preparations += stats.parallel_labels;
        }
      }
    }
  }
}

void speculative_callback_falls_back_without_extra_calls() {
  for (Directions direction : {FWD, BWD}) {
    context = "speculative extension request preserves exact callback fallback";
    ResetCallback serial_callback, parallel_callback;
    auto serial = seeded_solver(direction, 0, 1, false, 4, &serial_callback);
    auto parallel = seeded_solver(direction, 0, 1, true, 4, &parallel_callback);
    for (int step = 0; step < 12 && !Access::search(*serial, direction).stop; ++step) {
      auto serial_parent = Access::search(*serial, direction).current_label;
      auto parallel_parent = Access::search(*parallel, direction).current_label;
      Access::move(*serial, direction);
      Access::move(*parallel, direction);
      same_label(*serial_parent, *parallel_parent, false);
      same_solver(*serial, *parallel);
      require(serial_callback.calls == parallel_callback.calls, "speculation made extra callback calls");
      ++solver_moves;
    }
    require(serial_callback.calls > 0, "callback fixture did not extend a label");
    const auto& stats = Access::search(*parallel, direction).extension_stats;
    require(stats.parallel_batches == 0 && stats.prepared_labels == 0 && stats.parallel_labels == 0,
            "callback fixture used speculative workers");
  }
}
} // namespace

int main() {
  try {
    random_states_and_moving_bounds();
    explicit_nonpositive_backward_and_soft_bounds();
    ghost_heap_label_and_repartition();
    callback_fallback();
    endpoint_rounding_and_cache_hits();
    speculative_moves_preserve_state_and_tightened_bounds();
    speculative_callback_falls_back_without_extra_calls();
    require(fast_rejections > 0, "no fast resource rejection exercised");
    std::cout << "Bucket extension differential tests passed: " << queries << " queries, "
              << arcs_verified << " arc comparisons, " << fast_rejections
              << " proven infeasible flags, " << solver_moves << " solver-state comparisons, "
              << consumed_preparations << " consumed preparations\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
