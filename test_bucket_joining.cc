// Seeded-state regression tests for preserving every join side effect.
// Compile all core sources with -DESPPRC_TESTING.
#include "src/cc/bidirectional.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace bidirectional {
struct BucketJoiningTestAccess {
  static Params* params(BiDirectional& solver) { return solver.params_ptr_.get(); }
  static void initialize(BiDirectional& solver, double halfway) {
    solver.best_label_ = std::make_shared<labelling::Label>();
    solver.max_res_curr_ = solver.max_res;
    solver.min_res_curr_ = solver.min_res;
    const int critical = solver.params_ptr_->critical_res;
    solver.max_res_curr_[critical] = solver.min_res_curr_[critical] = halfway;
    for (Directions direction : {FWD, BWD}) {
      Search* search = solver.getSearchPtr(direction);
      search->efficient_labels.resize(solver.graph_ptr_->number_vertices);
      search->best_labels.resize(solver.graph_ptr_->number_vertices);
      search->initializeDurationBuckets(critical, solver.max_res[critical],
          solver.params_ptr_->bucket_width, solver.params_ptr_->adaptive_buckets,
          solver.params_ptr_->bucket_dominance);
    }
  }
  static void add(BiDirectional& solver, Directions direction, double weight,
                  const std::vector<double>& resources, const std::vector<int>& path,
                  const std::vector<int>& extra_unreachable) {
    Search* search = solver.getSearchPtr(direction);
    int node = solver.graph_ptr_->getNodeIdFromUserId(path.back());
    labelling::Label label(weight, {node, path.back()}, resources, path, solver.params_ptr_.get());
    for (int id : extra_unreachable) label.markNodeUnreachable(id);
    search->pushEfficientLabel(node, label);
    search->addVisitedVertex(node);
    if (!search->best_labels[node] || weight < search->best_labels[node]->weight)
      search->replaceBestLabel(node, label);
  }
  static void adapt(BiDirectional& solver) {
    for (Directions direction : {FWD, BWD}) {
      auto* search = solver.getSearchPtr(direction);
      for (int node : search->visited_vertices) search->adaptDurationBuckets(node);
    }
  }
  static void join(BiDirectional& solver) { solver.joinLabels(); }
  static labelling::Label best(BiDirectional& solver) { return *solver.best_label_; }
  static bool terminated(BiDirectional& solver) { return solver.terminated_early_w_st_path_; }
  static std::vector<labelling::Label> retained(BiDirectional& solver, Directions direction) {
    std::vector<labelling::Label> result;
    auto* search = solver.getSearchPtr(direction);
    for (int node : search->visited_vertices)
      result.insert(result.end(), search->efficient_labels[node].begin(),
                    search->efficient_labels[node].end());
    return result;
  }
};
}

namespace {
using bidirectional::BWD;
using bidirectional::FWD;
using bidirectional::BucketJoiningTestAccess;
using labelling::Label;
std::string context;
size_t scenarios = 0, compared_records = 0, observed_resource_shortcuts = 0;
void require(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(context + ": " + message);
}
bool same_number(double a, double b) { return a == b || (std::isnan(a) && std::isnan(b)); }
void compare_label(const Label& a, const Label& b) {
  require(a.vertex.lemon_id == b.vertex.lemon_id && a.vertex.user_id == b.vertex.user_id,
          "label vertex differs");
  require(same_number(a.weight, b.weight), "label cost differs");
  require(a.resource_consumption.size() == b.resource_consumption.size(), "resource count differs");
  for (size_t i = 0; i < a.resource_consumption.size(); ++i)
    require(same_number(a.resource_consumption[i], b.resource_consumption[i]), "resource arithmetic differs");
  require(a.partial_path == b.partial_path, "path/order differs");
  require(a.unreachable_mask == b.unreachable_mask, "merged mask differs");
  require(a.unreachable_nodes == b.unreachable_nodes, "merged unreachable set differs");
  require(same_number(a.phi, b.phi), "phi differs");
  ++compared_records;
}
struct Seed {
  double cost;
  std::vector<double> resources;
  std::vector<int> path;
  std::vector<int> unreachable = {};
};
struct Edge {
  int from, to;
  double cost;
  std::vector<double> resources;
};
struct Case {
  std::vector<double> maximum{10., 10., 10.}, minimum{0., 0., 0.}, pi{0., 0., 0.};
  std::vector<Seed> forward{{0., {2., 0., 0.}, {0, 1}}};
  std::vector<Seed> backward{{10., {8., 1., 0.}, {5, 2}}};
  std::vector<Edge> edges{{1, 2, 0., {1., 0., 0.}}};
  double halfway = 5., bucket_width = .5;
  size_t minimum_groups = 0;
  int critical = 0;
  bool elementary = true, two_cycle = false, bitwise = false, adaptive = false;
  bool check_frontier = false;
  double threshold = std::numeric_limits<double>::quiet_NaN();
  bidirectional::REFCallback* callback = nullptr;
};
struct Snapshot {
  Label best;
  std::vector<Label> collected;
  std::vector<std::vector<int>> paths;
  double base, extra;
  std::vector<double> stats;
  bool terminated;
};
Snapshot execute(const Case& c, bool enabled, bool collect, int parallel_threads = 0) {
  std::set<int> ids{0, 5};
  for (const auto& seed : c.forward) ids.insert(seed.path.begin(), seed.path.end());
  for (const auto& seed : c.backward) ids.insert(seed.path.begin(), seed.path.end());
  for (const auto& edge : c.edges) { ids.insert(edge.from); ids.insert(edge.to); }
  std::vector<int> nodes(ids.begin(), ids.end());
  bidirectional::BiDirectional solver(nodes.size(), c.edges.size(), 0, 5,
                                     c.maximum, c.minimum, c.pi);
  solver.addNodes(nodes);
  for (const auto& edge : c.edges) solver.addEdge(edge.from, edge.to, edge.cost, edge.resources);
  solver.setBucketJoining(enabled);
  solver.setParallelJoining(parallel_threads > 0, std::max(1, parallel_threads));
  solver.setProfileJoining(true);
  solver.setCollectPaths(collect);
  solver.setThreshold(c.threshold);
  auto* params = BucketJoiningTestAccess::params(solver);
  params->critical_res = c.critical;
  params->elementary = c.elementary;
  params->two_cycle_elimination = c.two_cycle;
  params->use_bitwise_elementary = c.bitwise;
  params->max_nodes = *ids.rbegin() >= 64 ? 256 : 64;
  params->bucket_width = c.bucket_width;
  params->adaptive_buckets = c.adaptive;
  params->ref_callback = c.callback;
  BucketJoiningTestAccess::initialize(solver, c.halfway);
  for (const auto& seed : c.forward)
    BucketJoiningTestAccess::add(solver, FWD, seed.cost, seed.resources, seed.path, seed.unreachable);
  for (const auto& seed : c.backward)
    BucketJoiningTestAccess::add(solver, BWD, seed.cost, seed.resources, seed.path, seed.unreachable);
  if (c.adaptive) BucketJoiningTestAccess::adapt(solver);
  const auto forward_before = c.check_frontier ? BucketJoiningTestAccess::retained(solver, FWD)
                                             : std::vector<Label>{};
  const auto backward_before = c.check_frontier ? BucketJoiningTestAccess::retained(solver, BWD)
                                              : std::vector<Label>{};
  BucketJoiningTestAccess::join(solver);
  if (c.check_frontier) {
    const auto forward_after = BucketJoiningTestAccess::retained(solver, FWD);
    const auto backward_after = BucketJoiningTestAccess::retained(solver, BWD);
    require(forward_before.size() == forward_after.size() &&
            backward_before.size() == backward_after.size(), "joining changed frontier size");
    for (size_t i = 0; i < forward_before.size(); ++i) compare_label(forward_before[i], forward_after[i]);
    for (size_t i = 0; i < backward_before.size(); ++i) compare_label(backward_before[i], backward_after[i]);
  }
  Snapshot result{BucketJoiningTestAccess::best(solver), solver.getAllFeasibleSolutions(),
                  solver.getAllFeasiblePaths(), solver.getBestBaseCost(), solver.getBestExtraCost(),
                  solver.getJoiningStats(), BucketJoiningTestAccess::terminated(solver)};
  params->ref_callback = nullptr;
  return result;
}
void compare_snapshot(const Snapshot& original, const Snapshot& accelerated) {
  compare_label(original.best, accelerated.best);
  require(original.paths == accelerated.paths, "collected full path list/order differs");
  require(original.collected.size() == accelerated.collected.size(), "collected record count differs");
  for (size_t i = 0; i < original.collected.size(); ++i)
    compare_label(original.collected[i], accelerated.collected[i]);
  require(same_number(original.base, accelerated.base) && same_number(original.extra, accelerated.extra),
          "best base/SRI cost decomposition differs");
  require(original.terminated == accelerated.terminated, "threshold termination differs");
}
Snapshot check(const Case& c, bool collect) {
  Snapshot original = execute(c, false, collect);
  Snapshot accelerated = execute(c, true, collect);
  compare_snapshot(original, accelerated);
  require(original.stats.size() == 10 && accelerated.stats.size() == 10, "unexpected joining stats shape");
  require(original.stats[0] == 0., "disabled mode still checked duration groups");
  observed_resource_shortcuts += static_cast<size_t>(accelerated.stats[5]);
  if (collect) require(accelerated.stats[5] == 0., "collected records used resource shortcuts");
  if (c.backward.size() >= 64)
    require(accelerated.stats[0] > 0., "large test pool did not exercise bucket group screening");
  require(accelerated.stats[0] >= c.minimum_groups, "fixture did not exercise required number of groups");
  if (!collect) require(original.paths.empty() && original.collected.empty(), "collection off retained output");
  ++scenarios;
  return original;
}

void phi_history_and_infeasible_records() {
  for (bool collect : {false, true}) {
    context = "infeasible low-phi predecessor";
    Case c;
    c.backward = {{10., {8., 12., 0.}, {5, 2}}, {-10., {9., 1., 0.}, {5, 2}}};
    const auto result = check(c, collect);
    require(result.best.vertex.lemon_id == -1,
            "fixture must retain infeasible phi0 history to reject feasible phi1 duplicate");
    if (collect) {
      require(result.collected.size() == 2, "infeasible and rejected records must both be collected");
      require(result.collected[0].resource_consumption[1] > c.maximum[1], "first record not infeasible");
    }
    context = "infeasible phi overwrite after feasible incumbent";
    c.backward = {{10., {9., 1., 0.}, {5, 2}}, {9., {8., 12., 0.}, {5, 2}},
                  {1., {9., 0., 0.}, {5, 2}}};
    const auto updated = check(c, collect);
    require(updated.best.weight == 10., "later low-cost phi1 path should be suppressed by infeasible phi0 history");
    if (collect) require(updated.collected.size() == 3, "phi suppression changed collected history");
  }
}

void equal_cost_resource_winner() {
  context = "equal cost resource winner";
  Case c;
  c.backward = {{10., {8., 5., 0.}, {5, 2}}, {10., {8., 1., 0.}, {5, 2}}};
  for (bool collect : {false, true}) {
    const auto result = check(c, collect);
    require(result.best.weight == 10. && result.best.resource_consumption[1] == 1.,
            "equal-cost resource improvement did not replace best");
  }
}

void masks_and_two_cycles() {
  for (bool bitwise : {false, true}) {
    for (bool collect : {false, true}) {
      context = "unreachable is not visited";
      Case c;
      c.bitwise = bitwise;
      c.forward[0].unreachable = {2, 5};
      require(check(c, collect).best.vertex.lemon_id != -1, "unreachable-only overlap rejected a valid join");
      context = "actual overlap";
      c.forward[0].path = {0, 3, 1};
      c.backward[0].path = {5, 3, 2};
      require(check(c, collect).best.vertex.lemon_id == -1, "actual overlap allowed");
      context = "large path ID fallback";
      c.forward[0].path = {0, 65, 1};
      c.backward[0].path = {5, 129, 2};
      require(check(c, collect).best.vertex.lemon_id != -1, "distinct IDs differing by64 falsely overlap");
      c.backward[0].path = {5, 65, 2};
      require(check(c, collect).best.vertex.lemon_id == -1, "large-ID actual overlap missed");
      context = "non-elementary two-cycle boundary";
      c.elementary = false;
      c.forward[0].path = {0, 2, 1};
      c.backward[0].path = {5, 2};
      c.two_cycle = false;
      require(check(c, collect).best.vertex.lemon_id != -1, "non-elementary join rejected");
      c.two_cycle = true;
      require(check(c, collect).best.vertex.lemon_id == -1, "boundary two-cycle allowed");
    }
  }
}

void sri_floating_order_and_critical_inversion() {
  for (bool collect : {false, true}) {
    context = "nonnegative SRI floor arithmetic order";
    Case c;
    c.forward[0].resources[2] = .2;
    c.backward[0].resources[2] = .1;
    c.edges[0].resources[2] = .7;
    c.maximum[2] = std::nextafter(1., 0.);
    c.pi[2] = 10.;
    const auto result = check(c, collect);
    require(result.best.vertex.lemon_id != -1, "regrouping resource arithmetic wrongly excluded feasible path");
    require(result.best.resource_consumption[2] == .1 + (.2 + .7), "SRI arithmetic order changed");
    require(result.extra == 0. && result.best.weight == 10., "SRI floor crossed integer boundary");
    context = "critical resource index1 and backward cancellation";
    c = Case{};
    c.critical = 1;
    c.maximum = {10., 1e16, 10.};
    c.halfway = 5.;
    c.forward = {{0., {0., 2., 0.}, {0, 1}}};
    c.backward = {{10., {1., 1e16 - 2., 0.}, {5, 2}}};
    c.edges = {{1, 2, 0., {0., 1., 0.}}};
    const auto inverted = check(c, collect);
    require(inverted.best.resource_consumption[1] == 5., "critical inversion changed");
  }
}

struct JoinCallback : bidirectional::REFCallback {
  mutable size_t calls = 0;
  std::vector<double> REF_join(const std::vector<double>& fwd, const std::vector<double>&,
      const int&, const int&, const std::vector<double>& edge) const override {
    ++calls;
    return {fwd[0] + edge[0], 1., .5};
  }
};
void callback_fallback() {
  for (bool collect : {false, true}) {
    context = "nonadditive join callback";
    Case c;
    c.forward[0].resources[1] = 100.;
    c.backward[0].resources[1] = 100.;
    c.edges[0].resources[1] = 100.;
    c.pi[2] = 3.;
    JoinCallback callback;
    c.callback = &callback;
    const auto before = execute(c, false, collect);
    const size_t original_calls = callback.calls;
    const auto after = execute(c, true, collect);
    require(original_calls > 0 && callback.calls == 2 * original_calls, "callback bypassed or extra callbacks invoked");
    compare_label(before.best, after.best);
    require(before.paths == after.paths, "callback collected output differs");
    require(before.best.vertex.lemon_id != -1 && before.best.resource_consumption[1] == 1.,
            "callback feasibility replaced by additive assumption");
    ++scenarios;
  }
}

void large_mixed_pools() {
  for (int seed = 0; seed < 12; ++seed) {
    for (bool adaptive : {false, true}) {
      for (bool collect : {false, true}) {
        context = "mixed seeded pools seed=" + std::to_string(seed) +
                  " adaptive=" + std::to_string(adaptive) + " collect=" + std::to_string(collect);
        Case c;
        c.adaptive = adaptive;
        c.bitwise = seed % 2 == 0;
        c.pi = {0., 0., 3.};
        c.forward.clear();
        c.backward.clear();
        std::mt19937 rng(seed);
        for (int i = 0; i < 8; ++i)
          c.forward.push_back({double(int(rng() % 31) - 20),
                               {double(rng() % 11) / 2., double(rng() % 8), double(rng() % 5) / 2.},
                               {0, 6 + i, 1}, {2}});
        for (int i = 0; i < 96; ++i)
          c.backward.push_back({double(int(rng() % 31) - 20),
                                {5. + double(rng() % 11) / 2., double(rng() % 16), double(rng() % 5) / 2.},
                                {5, 6 + int(rng() % 30), 2}});
        check(c, collect);
      }
    }
  }
}

void more_than_sixty_four_groups() {
  for (bool collect : {false, true}) {
    context = "ninety-six duration groups without a64bit group mask";
    Case c;
    c.bucket_width = .01;
    c.minimum_groups = 96;
    c.backward.clear();
    for (int i = 0; i < 96; ++i)
      c.backward.push_back({100. - i, {5. + .04 * i, i % 2 ? 12. : 1., 0.}, {5, 2}});
    const auto result = check(c, collect);
    require(result.best.vertex.lemon_id != -1, "large-group fixture must have a feasible winner");
    if (collect) require(result.collected.size() == 96, "group IDs above63 lost collected records");
  }
}

void nonfinite_resource_extrema() {
  const double nan = std::numeric_limits<double>::quiet_NaN();
  const double infinity = std::numeric_limits<double>::infinity();
  for (double exceptional : {nan, infinity, -infinity}) {
    for (bool collect : {false, true}) {
      for (int placement = 0; placement < 3; ++placement) {
        context = "nonfinite noncritical resource extrema placement=" + std::to_string(placement);
        Case c;
        const Seed invalid{10., {8., exceptional, 0.}, {5, 2}};
        const Seed valid{10., {8., 1., 0.}, {5, 2}};
        // One interval containing only the exception, or an exceptional record
        // before/after a finite record. NaNs must not poison extrema so that a
        // valid member gets skipped; infinities must stay conservative too.
        c.backward = placement == 0 ? std::vector<Seed>{invalid}
                   : placement == 1 ? std::vector<Seed>{invalid, valid}
                                    : std::vector<Seed>{valid, invalid};
        const auto result = check(c, collect);
        require((result.best.vertex.lemon_id != -1) == (placement != 0),
                "nonfinite extrema changed feasibility of finite witness");
        if (collect) require(result.collected.size() == c.backward.size(),
                             "nonfinite collected record silently removed");
      }
    }
  }
}

void fractional_lower_bound_witness() {
  for (bool collect : {false, true}) {
    context = "fractional exact lower/upper witness";
    Case c;
    c.forward[0].resources[2] = .2;
    c.backward[0].resources[2] = .7;
    c.edges[0].resources[2] = .1;
    c.minimum[2] = c.maximum[2] = 1.;
    c.pi[2] = 3.;
    require(.7 + (.2 + .1) == 1. && (.7 + .2) + .1 < 1., "rounding premise changed");
    const auto result = check(c, collect);
    require(result.best.vertex.lemon_id != -1 && result.best.resource_consumption[2] == 1.,
            "valid exact lower-bound witness was excluded");
    require(result.extra == 3., "fractional witness SRI floor changed");
  }
}

Case parallel_pool(bool large_ids = false) {
  Case c;
  c.forward.clear();
  c.backward.clear();
  c.check_frontier = true;
  c.pi = {0., 0., 20.};
  const int offset = large_ids ? 1000 : 0;
  // All base costs stay below every feasible SRI-adjusted incumbent, keeping
  // the complete pool eligible after ordered incumbent updates.
  for (int i = 0; i < 32; ++i)
    c.forward.push_back({double(i % 5) / 4., {2., double(i % 3), .5},
                         {0, offset + 6 + i % 16, 1}, {2}});
  for (int j = 0; j < 2112; ++j)
    c.backward.push_back({double(j % 7) / 4.,
                          {7. + double(j % 3), j % 11 == 0 ? 12. : double(j % 5),
                           1. + double(j % 3) / 2.},
                          {5, offset + 40 + j % 20, 2}});
  return c;
}

void compare_parallel(const Case& c, bool buckets, bool collect, bool must_batch = false) {
  const auto serial = execute(c, buckets, collect);
  require(serial.stats.size() == 10, "serial joining stats shape differs");
  require(serial.stats[7] == 0. && serial.stats[8] == 0. && serial.stats[9] == 0.,
          "disabled parallel joining still prepared batches");
  for (int threads : {2, 4, 8}) {
    const auto parallel = execute(c, buckets, collect, threads);
    compare_snapshot(serial, parallel);
    require(parallel.stats.size() == 10, "parallel joining stats shape differs");
    for (double count : parallel.stats) require(count >= 0., "negative joining counter");
    if (c.callback) {
      require(parallel.stats[7] == 0. && parallel.stats[8] == 0. && parallel.stats[9] == 0.,
              "custom callback did not fall back to serial joining");
    }
#ifdef _OPENMP
    else if (must_batch) {
      require(parallel.stats[7] > 0., "large fixture did not run parallel batches");
      require(parallel.stats[8] > 0., "parallel batches did not precheck pairs");
      require(parallel.stats[9] > 0., "parallel batches did not prepare merge records");
    }
#else
    require(parallel.stats[7] == 0. && parallel.stats[8] == 0. && parallel.stats[9] == 0.,
            "no-OpenMP build reported parallel joining work");
#endif
    ++scenarios;
  }
}

void parallel_dense_pools() {
  for (int mode = 0; mode < 4; ++mode) {
    for (bool collect : {false, true}) {
      context = "parallel dense pool mode=" + std::to_string(mode) +
                " collect=" + std::to_string(collect);
      Case c = parallel_pool(mode == 2);
      c.bitwise = mode == 1;
      c.adaptive = mode % 2 != 0;
      if (mode == 3) {
        c.elementary = false;
        c.two_cycle = true;
      }
      compare_parallel(c, mode != 3, collect, true);
    }
  }
}

void parallel_phi_history() {
  for (bool has_incumbent : {false, true}) {
    for (bool collect : {false, true}) {
      context = "parallel cross-batch phi history incumbent=" + std::to_string(has_incumbent);
      Case c = parallel_pool();
      c.pi = {0., 0., 0.};
      c.forward.assign(32, {0., {2., 0., 0.}, {0, 1}});
      c.backward.assign(2112, {1., {9., 0., 0.}, {5, 2}});
      c.backward[0] = has_incumbent ? Seed{10., {9., 1., 0.}, {5, 2}}
                                    : Seed{10., {8., 12., 0.}, {5, 2}};
      c.backward[1] = {9., {8., 12., 0.}, {5, 2}};
      const auto reference = execute(c, true, collect);
      require((reference.best.vertex.lemon_id != -1) == has_incumbent,
              "phi fixture lost its infeasible predecessor witness");
      if (has_incumbent) require(reference.best.weight == 10., "phi fixture expected retained incumbent10");
      if (collect) require(reference.collected.size() == 32 * 2112,
                           "phi fixture did not retain complete infeasible history");
      compare_parallel(c, true, collect, true);
    }
  }
}

void parallel_threshold_prefix() {
  for (bool collect : {false, true}) {
    context = "parallel threshold stops at exact ordered prefix";
    Case c = parallel_pool();
    c.pi = {0., 0., 0.};
    c.threshold = 84.;
    c.forward.clear();
    for (int i = 0; i < 32; ++i)
      c.forward.push_back({100. - i, {2., 0., 0.}, {0, 6 + i, 1}});
    c.backward.assign(2112, {0., {8., 1., 0.}, {5, 2}});
    const auto reference = execute(c, true, collect);
    require(reference.terminated && reference.best.weight == 84., "threshold fixture did not terminate");
    if (collect) require(reference.collected.size() == 16 * 2112,
                         "winning threshold record must not be appended to the serial prefix");
    compare_parallel(c, true, collect);
  }
}

void parallel_record_overflow_replays_serially() {
  for (bool collect : {false, true}) {
    context = "parallel 4096-record overflow preserves ordered serial replay";
    Case c = parallel_pool();
    c.pi = {0., 0., 0.};
    c.forward.clear();
    for (int i = 0; i < 32; ++i)
      c.forward.push_back({0., {2., 0., 0.}, {0, 6 + i, 1}});
    c.backward.assign(4097, {10., {9., 1., 0.}, {5, 2}});
    // The final prepared record lowers phi despite being infeasible. The first
    // unprepared record then offers a cheaper but phi-ineligible duplicate.
    // Neither speculative history nor the coordinator's serial replay may
    // publish records twice or lose this boundary update.
    c.backward[4095] = {9., {8., 12., 0.}, {5, 2}};
    c.backward[4096] = {1., {9., 0., 0.}, {5, 2}};
    const auto serial = execute(c, true, collect);
    require(serial.best.weight == 10., "overflow fixture lost its phi-suppressed cheaper duplicate");
    if (collect) require(serial.collected.size() == 32 * 4097,
                         "overflow fixture must collect every original record exactly once");
    for (int threads : {2, 4, 8}) {
      const auto parallel = execute(c, true, collect, threads);
      compare_snapshot(serial, parallel);
#ifdef _OPENMP
      require(parallel.stats[7] > 0., "overflow fixture did not run parallel batches");
      require(parallel.stats[8] == 31 * 4097,
              "each worker must encounter the first record beyond its cap");
      require(parallel.stats[9] == 31 * 4096,
              "overflow fixture did not hit the per-forward prepared-record cap");
#else
      require(parallel.stats[7] == 0. && parallel.stats[8] == 0. && parallel.stats[9] == 0.,
              "no-OpenMP overflow fixture must remain serial");
#endif
      ++scenarios;
    }
  }
}

struct OrderedJoinCallback : bidirectional::REFCallback {
  mutable std::vector<double> inputs;
  std::vector<double> REF_join(const std::vector<double>& fwd, const std::vector<double>& bwd,
      const int&, const int&, const std::vector<double>& edge) const override {
    inputs.insert(inputs.end(), {fwd[0], bwd[1], edge[0]});
    return {fwd[0] + edge[0], 1., .5};
  }
};

void parallel_callback_fallback() {
  for (bool collect : {false, true}) {
    context = "parallel request preserves serial callback invocation order";
    Case c = parallel_pool();
    for (size_t i = 0; i < c.forward.size(); ++i) {
      c.forward[i].cost = 0.;
      c.forward[i].resources = {2. + double(i) / 64., 100., 0.};
    }
    for (size_t i = 0; i < c.backward.size(); ++i) {
      c.backward[i].cost = 0.;
      c.backward[i].resources[1] = 100. + double(i % 7);
    }
    OrderedJoinCallback callback;
    c.callback = &callback;
    const auto serial = execute(c, true, collect);
    const auto expected_inputs = callback.inputs;
    require(!expected_inputs.empty(), "callback fixture did not call REF_join");
    for (int threads : {2, 4, 8}) {
      callback.inputs.clear();
      const auto parallel = execute(c, true, collect, threads);
      compare_snapshot(serial, parallel);
      require(callback.inputs == expected_inputs, "callback invocation count or order changed");
      require(parallel.stats[7] == 0. && parallel.stats[8] == 0. && parallel.stats[9] == 0.,
              "callback was invoked from speculative parallel joining");
      ++scenarios;
    }
  }
}

} // namespace

int main() {
  try {
    phi_history_and_infeasible_records();
    equal_cost_resource_winner();
    masks_and_two_cycles();
    sri_floating_order_and_critical_inversion();
    callback_fallback();
    large_mixed_pools();
    more_than_sixty_four_groups();
    nonfinite_resource_extrema();
    fractional_lower_bound_witness();
    parallel_dense_pools();
    parallel_phi_history();
    parallel_threshold_prefix();
    parallel_record_overflow_replays_serially();
    parallel_callback_fallback();
    require(observed_resource_shortcuts > 0, "no best-only resource shortcut exercised");
    std::cout << "Bucket joining seeded tests passed: " << scenarios << " comparisons, "
              << compared_records << " exact merged/best-label comparisons, "
              << observed_resource_shortcuts << " resource shortcuts\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
