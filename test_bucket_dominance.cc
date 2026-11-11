// Differential regression tests for Search::runDominance bucket screening.
// Compile against the repository's src/cc/{search,labelling,ref_callback}.cc.
#include "src/cc/search.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using bidirectional::BWD;
using bidirectional::FWD;
using bidirectional::Directions;
using bidirectional::Params;
using bidirectional::Search;
using labelling::Label;

namespace {
std::uint64_t compared_candidates = 0;
std::uint64_t compared_labels = 0;
std::string context;

void require(bool ok, const std::string& message) {
  if (!ok) throw std::runtime_error(context + ": " + message);
}
bool same_double(double a, double b) {
  return a == b || (std::isnan(a) && std::isnan(b));
}
void verify_labels(const std::vector<Label>& actual, const std::vector<Label>& expected) {
  require(actual.size() == expected.size(), "survivor count differs");
  for (size_t i = 0; i < actual.size(); ++i) {
    const Label& a = actual[i];
    const Label& b = expected[i];
    require(same_double(a.weight, b.weight), "weight or order differs");
    require(a.vertex.lemon_id == b.vertex.lemon_id && a.vertex.user_id == b.vertex.user_id,
            "vertex differs");
    require(a.resource_consumption.size() == b.resource_consumption.size(), "resource dimensions differ");
    for (size_t r = 0; r < a.resource_consumption.size(); ++r)
      require(same_double(a.resource_consumption[r], b.resource_consumption[r]), "resources differ");
    require(a.partial_path == b.partial_path, "path or order differs");
    require(a.unreachable_mask == b.unreachable_mask, "unreachable mask differs");
    require(a.unreachable_nodes == b.unreachable_nodes, "unreachable set differs");
    require(a.params_ptr == b.params_ptr, "parameter pointer differs");
    require(same_double(a.phi, b.phi), "phi differs");
    ++compared_labels;
  }
}
void verify_buckets(const Search& search) {
  require(search.duration_buckets.size() == search.efficient_labels.size(),
          "wrong bucket node count");
  for (size_t node = 0; node < search.efficient_labels.size(); ++node) {
    const auto& labels = search.efficient_labels[node];
    std::vector<int> occurrences(labels.size(), 0);
    for (const auto& bucket : search.duration_buckets[node]) {
      require(!bucket.second.empty(), "empty bucket retained");
      size_t previous = 0;
      bool first = true;
      for (size_t pos : bucket.second) {
        require(pos < labels.size(), "stale position after deletion");
        require(search.durationBucketIndex(labels[pos]) == bucket.first,
                "label assigned to wrong interval");
        require(first || pos > previous, "bucket positions not in vector order");
        first = false;
        previous = pos;
        ++occurrences[pos];
      }
    }
    for (int count : occurrences) require(count == 1, "missing or duplicate bucket index");
  }
}

struct Harness {
  Params params;
  Search search;
  std::vector<Label> expected;
  int source = 0;
  int target = 1;
  bool large_ids = false;
  Harness(Directions direction, bool adaptive, int critical, int mode)
      : search(direction) {
    // mode 0: elementary masks; 1: elementary sets; 2: requested masks with
    // >64 nodes falls back to sets; 3: non-elementary; 4: two-cycle elimination.
    params.critical_res = critical;
    params.elementary = mode <= 2;
    params.use_bitwise_elementary = mode == 0 || mode == 2;
    params.max_nodes = mode == 2 ? 256 : 64;
    params.two_cycle_elimination = mode == 4;
    large_ids = mode == 2;
    source = large_ids ? 100 : 0;
    target = large_ids ? 101 : 1;
    search.efficient_labels.resize(3);
    search.initializeDurationBuckets(critical, 2048., 5., adaptive);
  }
  Label make(double cost, double consumed, double payload, double sri_a,
             double sri_b, int tag = 0, std::uint64_t extra_mask = 0) {
    std::vector<double> resources{payload, sri_a, sri_b, 0.};
    // Keep all noncritical dimensions meaningful even when critical moves.
    resources[3] = resources[params.critical_res];
    resources[params.critical_res] = search.direction == FWD ? consumed : 2048. - consumed;
    std::vector<int> path{source};
    if (tag) path.push_back((large_ids ? 110 : 10) + tag % 32);
    path.push_back(target);
    Label label(cost, {1, target}, resources, path, &params);
    label.phi = static_cast<double>(tag) / 8.;
    for (int bit = 0; bit < 16; ++bit) {
      if (extra_mask & (std::uint64_t{1} << bit))
        label.markNodeUnreachable((large_ids ? 150 : 45) + bit);
    }
    return label;
  }
  void seed(const Label& label) {
    search.pushEfficientLabel(1, label);
    expected.push_back(label);
  }
  bool candidate(const Label& label, bool insert = true) {
    bool reference_rejected = labelling::runDominanceEff(
        &expected, label, search.direction, params.elementary);
    bool actual_rejected = search.runDominance(1, label, params.elementary);
    ++compared_candidates;
    require(actual_rejected == reference_rejected, "candidate acceptance differs");
    // Check BEFORE insertion too: deletion and rejected candidates can shift indices.
    verify_labels(search.efficient_labels[1], expected);
    verify_buckets(search);
    if (insert && !reference_rejected) {
      seed(label);
      verify_labels(search.efficient_labels[1], expected);
      verify_buckets(search);
    }
    return reference_rejected;
  }
};

void randomized_streams() {
  for (Directions direction : {FWD, BWD}) {
    for (bool adaptive : {false, true}) {
      for (int critical : {0, 1}) {
        for (int mode = 0; mode < 5; ++mode) {
          for (int seed = 0; seed < 3; ++seed) {
            context = "stream direction=" + std::to_string(direction) +
                " adaptive=" + std::to_string(adaptive) + " critical=" +
                std::to_string(critical) + " mode=" + std::to_string(mode) +
                " seed=" + std::to_string(seed);
            Harness h(direction, adaptive, critical, mode);
            std::mt19937_64 rng(73631 + seed);
            // Real nondominated tradeoff labels: cost decreases while duration
            // increases. All 384 remain, forcing bucket filtering's large-list path.
            for (int i = 0; i < 384; ++i) {
              const auto label = h.make(800. - i, i, 0., 0., 0., 0);
              require(!h.candidate(label), "tradeoff seed was rejected");
            }
            require(h.expected.size() == 384, "large nondominated pool not reached");
            // Mix dominated candidates, incomparable labels, new dominators,
            // exact duplicates and equal numerical states with differing masks.
            for (int i = 0; i < 360; ++i) {
              const int kind = rng() % 8;
              Label label;
              if (kind == 0 && !h.expected.empty()) {
                label = h.expected[rng() % h.expected.size()];
              } else if (kind == 1 && !h.expected.empty()) {
                label = h.expected[rng() % h.expected.size()];
                label.markNodeUnreachable(h.large_ids ? 210 : 62);
                label.phi += 1.;
              } else {
                double consumed = (rng() % 500) + (rng() % 4) * .25;
                double cost = 800. - consumed + static_cast<int>(rng() % 81) - 40;
                double payload = rng() % 6;
                double sri_a = (rng() % 9) * .5;
                double sri_b = (rng() % 10) / 3.;
                label = h.make(cost, consumed, payload, sri_a, sri_b,
                               rng() % 16, rng() & 31);
              }
              h.candidate(label);
              if (adaptive && i % 47 == 0) {
                h.search.adaptDurationBuckets(1);
                verify_labels(h.search.efficient_labels[1], h.expected);
                verify_buckets(h.search);
              }
            }
          }
        }
      }
    }
  }
}

void deletion_then_rejection() {
  for (Directions direction : {FWD, BWD}) {
    for (bool adaptive : {false, true}) {
      context = "deletion before rejection";
      Harness h(direction, adaptive, 1, 0);
      // Deliberately seed an inconsistent efficient vector. The old routine
      // deletes the first record then discovers a dominator later. Preserve
      // that behavior and surviving order exactly, including at large sizes.
      h.seed(h.make(20., 20., 0., 0., 0.));
      for (int i = 0; i < 90; ++i)
        h.seed(h.make(-100. - i, 500. + i, 0., 0., 0.));
      h.seed(h.make(0., 0., 0., 0., 0.));
      const auto count = h.expected.size();
      require(h.candidate(h.make(10., 10., 0., 0., 0.)), "candidate should be rejected");
      require(h.expected.size() == count - 1, "earlier deletion was undone");
      h.candidate(h.make(-300., -1., 0., 0., 0.));
      require(h.expected.size() == 1, "all old labels should be removed");
    }
  }
}

void equal_resources_and_masks() {
  for (Directions direction : {FWD, BWD}) {
    for (int mode : {0, 1, 2}) {
      context = "equal numerical states and different masks";
      Harness h(direction, true, 1, mode);
      for (int i = 0; i < 300; ++i)
        h.seed(h.make(1000. - i, i, 2., .5, 1. / 3., 0, std::uint64_t{1} << (i % 8)));
      // Same cost and all resources do not dominate in the existing routine,
      // even when mask subset would hold. operator== also omits masks.
      Label candidate = h.expected[101];
      candidate.markNodeUnreachable(h.large_ids ? 220 : 63);
      require(!h.candidate(candidate), "equal numerical state was removed by mask ordering");
      candidate.partial_path.insert(candidate.partial_path.begin() + 1,
                                    h.large_ids ? 240 : 40);
      candidate.markNodeUnreachable(h.large_ids ? 240 : 40);
      require(!h.candidate(candidate), "equal numerical state with different path rejected");
    }
  }
}

void deadline_behavior() {
  for (Directions direction : {FWD, BWD}) {
    for (bool adaptive : {false, true}) {
      context = "already expired deadline";
      Harness h(direction, adaptive, 0, 0);
      h.params.setTimeLimit(0.);
      h.params.setStartTime(std::chrono::system_clock::now() - std::chrono::seconds(1));
      // Original scan checks no deadline on an empty list: preserve that.
      require(!h.candidate(h.make(10., 10., 0., 0., 0.)), "empty-list timeout differs");
      for (int i = 1; i < 300; ++i)
        h.seed(h.make(1000. - i, i, 0., 0., 0.));
      const auto count = h.expected.size();
      require(h.candidate(h.make(-10000., -1., 0., 0., 0.)), "expired candidate accepted");
      require(h.expected.size() == count, "expired scan deleted labels");
      // Candidate cannot dominate or be dominated numerically. Even if every
      // bucket can be skipped, a nonempty original scan still reports timeout.
      require(h.candidate(h.make(-10000., 10000., 0., 0., 0.)), "skipped buckets hid timeout");
    }
  }
}

void nonfinite_summary_values() {
  const double nan = std::numeric_limits<double>::quiet_NaN();
  for (Directions direction : {FWD, BWD}) {
    for (bool weight_nan : {false, true}) {
      for (int nan_position : {0, 31}) {
        context = "NaN summary weight=" + std::to_string(weight_nan) +
                  " position=" + std::to_string(nan_position);
        Harness h(direction, false, 1, 0);
        h.params.profile_dominance = true;
        for (int i = 0; i < 64; ++i) {
          Label old = h.make(10., 10., 0., 0., 0.);
          old.phi = i;
          if (i == nan_position) {
            if (weight_nan) old.weight = nan;
            else old.resource_consumption[2] = nan;
          }
          h.seed(old);
        }
        // Exact comparison treats greater-than-NaN as false. The exceptional
        // member can therefore be dominated even though every finite member
        // in the same bucket fails the cost/resource test.
        Label candidate = weight_nan ? h.make(20., 5., 0., 0., 0.)
                                     : h.make(5., 5., 0., 0., 1.);
        require(!h.candidate(candidate, false), "NaN member changed rejection");
        require(h.expected.size() == 63, "NaN member not deleted exactly");
        require(h.search.dominance_stats.bucket_checks > 0, "NaN test bypassed bucket filter");
      }
      context = "NaN candidate";
      Harness h(direction, false, 1, 0);
      for (int i = 0; i < 64; ++i) h.seed(h.make(10., 10., 0., 0., 0.));
      Label candidate = h.make(5., 5., 0., 0., 0.);
      if (weight_nan) candidate.weight = nan;
      else candidate.resource_consumption[2] = nan;
      require(!h.candidate(candidate, false), "NaN candidate changed rejection");
      require(h.expected.empty(), "NaN candidate survivor behavior differs");
    }
  }
}

void backward_subtraction_cancellation() {
  context = "backward stored critical extrema";
  Harness h(BWD, false, 1, 0);
  h.params.profile_dominance = true;
  const double horizon = 1e16;
  const double stored = 2.;
  const double lower = std::nextafter(stored, 0.);
  const double upper = std::nextafter(stored, 4.);
  require(horizon - stored == horizon - lower && horizon - stored == horizon - upper,
          "floating-point cancellation premise does not hold");
  h.search.initializeDurationBuckets(1, horizon, 5., false, true);
  for (int i = 0; i < 64; ++i) {
    Label old = h.make(10. + i, 10., 0., 0., 0.);
    old.resource_consumption[1] = stored;
    h.seed(old);
  }
  Label candidate = h.make(9., 10., 0., 0., 0.);
  candidate.resource_consumption[1] = lower;
  require(!h.candidate(candidate, false), "critical tradeoff falsely rejected");
  require(h.expected.size() == 64, "critical rounding deleted a label");
  require(h.search.dominance_stats.exact_checks == 0,
          "summary should use stored critical extrema without cancellation");
  candidate.resource_consumption[1] = upper;
  require(!h.candidate(candidate, false), "better critical candidate rejected");
  require(h.expected.empty(), "better stored critical value failed to delete labels");
}

void sparse_bucket_queries_and_switch() {
  for (Directions direction : {FWD, BWD}) {
    for (bool enabled : {false, true}) {
      context = "sparse query enabled=" + std::to_string(enabled);
      Harness h(direction, false, 1, 0);
      h.params.profile_dominance = true;
      h.search.initializeDurationBuckets(1, 2048., 5., false, enabled);
      // Exactly one label per bucket. Only bucket 20 can interact with the
      // candidate; 1 eligible label is below the 64/4 sparse-query threshold.
      for (int i = 0; i < 64; ++i)
        h.seed(h.make(1000. - 10. * i, 10. * i, 0., 0., 0.));
      require(!h.candidate(h.make(799.9, 200., 0., 0., 0.)), "sparse better candidate rejected");
      require(h.expected.size() == 64, "sparse candidate should replace one label");
      const auto& stats = h.search.dominance_stats;
      require(stats.queries == 1, "query counter differs");
      if (enabled) {
        require(stats.bucket_checks == 64, "all interval summaries must be screened");
        require(stats.label_pairs == 1 && stats.exact_checks == 1,
                "sparse filtering did not isolate the one eligible label");
        require(stats.bucket_directions_skipped == 127, "wrong directional skip count");
      } else {
        require(stats.bucket_checks == 0, "disabled switch still queried buckets");
        require(stats.label_pairs == 64 && stats.exact_checks == 127,
                "disabled switch does not retain original scan");
      }
    }
  }
}

void set_projection_collisions() {
  for (Directions direction : {FWD, BWD}) {
    context = "set projection collision";
    Harness h(direction, false, 1, 2);
    h.params.profile_dominance = true;
    for (int i = 0; i < 64; ++i) {
      Label old(10. + i, {1, h.target}, {0., direction == FWD ? 10. : 2038., 0., 0.},
                {h.source, 130, h.target}, &h.params);
      h.seed(old);
    }
    Label candidate(1., {1, h.target}, {0., direction == FWD ? 5. : 2043., 0., 0.},
                    {h.source, 194, h.target}, &h.params);
    require((130 & 63) == (194 & 63), "projection collision premise");
    require(!h.candidate(candidate, false), "projected masks were treated as exact masks");
    require(h.expected.size() == 64, "colliding projection deleted incompatible labels");
    require(h.search.dominance_stats.exact_checks == 64,
            "projection collision must survive screening for exact set checks");
    // Now make actual inclusion hold, retaining the collision. Exact set
    // inclusion must be accepted, with the original survivor order preserved.
    candidate.unreachable_nodes.erase(194);
    candidate.partial_path.erase(candidate.partial_path.begin() + 1);
    require(!h.candidate(candidate, false), "valid subset candidate rejected");
    require(h.expected.empty(), "actual subset failed to dominate");
  }
}

} // namespace

int main() {
  try {
    randomized_streams();
    deletion_then_rejection();
    equal_resources_and_masks();
    deadline_behavior();
    nonfinite_summary_values();
    backward_subtraction_cancellation();
    sparse_bucket_queries_and_switch();
    set_projection_collisions();
    std::cout << "Bucket dominance differential tests passed: " << compared_candidates
              << " candidate decisions, " << compared_labels
              << " exact surviving-label comparisons\n";
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
