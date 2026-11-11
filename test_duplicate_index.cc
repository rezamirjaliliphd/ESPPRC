// Differential checks for Search::containsEfficientLabel against std::find.
// Compile with src/cc/{search,labelling,ref_callback}.cc.
#include "src/cc/search.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
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
std::string context;
std::uint64_t queries = 0, compared_labels = 0;

void require(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(context + ": " + message);
}
bool same_number(double a, double b) { return a == b || (std::isnan(a) && std::isnan(b)); }
void same_labels(const std::vector<Label>& a, const std::vector<Label>& b) {
  require(a.size() == b.size(), "query changed retained label count");
  for (size_t i = 0; i < a.size(); ++i) {
    require(same_number(a[i].weight, b[i].weight), "retained cost/order changed");
    require(a[i].vertex.lemon_id == b[i].vertex.lemon_id &&
            a[i].vertex.user_id == b[i].vertex.user_id, "retained vertex changed");
    require(a[i].resource_consumption.size() == b[i].resource_consumption.size(),
            "retained resource dimension changed");
    for (size_t r = 0; r < a[i].resource_consumption.size(); ++r)
      require(same_number(a[i].resource_consumption[r], b[i].resource_consumption[r]),
              "retained resource changed");
    require(a[i].partial_path == b[i].partial_path, "retained path/order changed");
    require(a[i].unreachable_mask == b[i].unreachable_mask &&
            a[i].unreachable_nodes == b[i].unreachable_nodes, "retained unreachable state changed");
    require(a[i].params_ptr == b[i].params_ptr && same_number(a[i].phi, b[i].phi) &&
            a[i].extension_id == b[i].extension_id, "retained metadata changed");
    ++compared_labels;
  }
}

struct Harness {
  Params params;
  Search search;
  bool adaptive, bucket_dominance;
  Harness(Directions direction, bool adaptive_in = false, bool buckets = true)
      : search(direction), adaptive(adaptive_in), bucket_dominance(buckets) {
    params.critical_res = 0;
    params.elementary = true;
    params.max_nodes = 64;
    params.use_bitwise_elementary = true;
    params.bucket_dominance = buckets;
    search.efficient_labels.resize(3);
    search.initializeDurationBuckets(0, 2048., 5., adaptive, bucket_dominance);
  }
  Label make(double cost, double consumed, double payload = 0., double sri = 0., int node = 1) {
    return Label(cost, {node, node},
                 {search.direction == FWD ? consumed : 2048. - consumed, payload, sri},
                 {0, node}, &params);
  }
  void seed(const Label& label, int node = 1) { search.pushEfficientLabel(node, label); }
  bool query(const Label& candidate, int node = 1) {
    const auto before = search.efficient_labels[node];
    const bool expected = std::find(before.begin(), before.end(), candidate) != before.end();
    const bool actual = search.containsEfficientLabel(node, candidate);
    require(actual == expected, "duplicate lookup differs from std::find");
    same_labels(search.efficient_labels[node], before);
    ++queries;
    return actual;
  }
  bool dominate(const Label& candidate) {
    auto expected = search.efficient_labels[1];
    const bool rejection = labelling::runDominanceEff(&expected, candidate, search.direction, true);
    require(search.runDominance(1, candidate, true) == rejection, "dominance result changed");
    same_labels(search.efficient_labels[1], expected);
    return rejection;
  }
};

void growth_reallocation_repartition_and_reset() {
  for (Directions direction : {FWD, BWD}) {
    for (bool adaptive : {false, true}) {
      for (bool buckets : {false, true}) {
        context = "growth/reset direction=" + std::to_string(direction) +
                  " adaptive=" + std::to_string(adaptive) + " buckets=" + std::to_string(buckets);
        Harness h(direction, adaptive, buckets);
        require(!h.query(h.make(1., 1.)), "empty pool reports a duplicate");
        for (int i = 0; i < 384; ++i) {
          Label label = h.make(1000. - i, i / 4., i % 7, .5 * (i % 5));
          label.partial_path.insert(label.partial_path.begin() + 1, 10 + i % 17);
          h.seed(label);
          require(h.query(label), "appended label missing after growth/reallocation");
          require(h.query(h.search.efficient_labels[1][i / 2]), "older label missing after growth");
          require(!h.query(h.make(-9000. - i, i / 4.)), "absent label matched during growth");
          if (i == 31 || i == 63 || i == 255) {
            h.search.adaptDurationBuckets(1);
            require(h.query(label), "repartition lost a duplicate");
          }
        }
        // Reset with retained labels present: the lazy index must rebuild from
        // those labels, including when duration dominance itself is disabled.
        h.search.initializeDurationBuckets(0, 2048., 2.5, !adaptive, buckets);
        for (size_t i = 0; i < h.search.efficient_labels[1].size(); i += 17)
          require(h.query(h.search.efficient_labels[1][i]), "reset lost an existing duplicate");
        const Label next = h.make(-2000., 150., 1., .5);
        h.seed(next);
        require(h.query(next), "append after reset missing from index");
        for (int i = 0; i < 40; ++i) h.seed(h.make(i, i, 0., 0., 2), 2);
        require(h.query(h.make(20., 20., 0., 0., 2), 2), "second node lookup missing");
        require(!h.query(h.make(20., 20., 0., 0., 2), 1), "index mixed retained nodes");
      }
    }
  }
}

void hashed_field_collisions_and_ignored_metadata() {
  for (Directions direction : {FWD, BWD}) {
    context = "same hashed fields with distinct resources and ignored metadata";
    Harness h(direction);
    // Every member has exactly the same proposed hash fields. The original
    // equality predicate must distinguish all resource vectors in this chain.
    for (int i = 0; i < 256; ++i) h.seed(h.make(7., 10., i, .5 * (i % 5)));
    for (int i : {0, 31, 127, 255})
      require(h.query(h.make(7., 10., i, .5 * (i % 5))), "resource collision hid a duplicate");
    require(!h.query(h.make(7., 10., -1., .5)), "hash collision bypassed resource equality");
    Label candidate = h.search.efficient_labels[1][127];
    Params other_params;
    candidate.params_ptr = &other_params;
    candidate.vertex.user_id = 999; // operator== compares only vertex.lemon_id.
    candidate.phi = 123.;
    candidate.extension_id = 987654321;
    candidate.unreachable_mask = ~candidate.unreachable_mask;
    candidate.unreachable_nodes.insert(194);
    require(h.query(candidate), "ignored metadata leaked into duplicate key");
    candidate.vertex.lemon_id = 2;
    require(!h.query(candidate), "different internal vertex falsely matched");
    candidate.vertex.lemon_id = 1;
    candidate.partial_path.insert(candidate.partial_path.begin() + 1, 9);
    require(!h.query(candidate), "different path falsely matched");
  }
}

void signed_zero_and_nonfinite_values() {
  for (Directions direction : {FWD, BWD}) {
    context = "signed zero and nonfinite equality";
    Harness h(direction);
    for (int i = 0; i < 40; ++i) h.seed(h.make(100. + i, i));
    Label zero = h.make(-0., 10., -0., 0.);
    h.seed(zero);
    zero.weight = 0.;
    zero.resource_consumption[1] = 0.;
    require(h.query(zero), "signed-zero equality changed after hashing");
    zero.weight = -0.;
    zero.resource_consumption[2] = -0.;
    require(h.query(zero), "negative-zero query failed");
    Label nan_cost = h.make(std::numeric_limits<double>::quiet_NaN(), 10.);
    h.seed(nan_cost);
    require(!h.query(nan_cost), "NaN weight became reflexively equal");
    Label nan_resource = h.make(-25., 10., std::numeric_limits<double>::quiet_NaN());
    h.seed(nan_resource);
    require(!h.query(nan_resource), "NaN resource became reflexively equal");
    nan_resource.resource_consumption[1] = 0.;
    require(!h.query(nan_resource), "NaN stored resource matched a finite query");
    Label infinity = h.make(std::numeric_limits<double>::infinity(), 10.,
                            std::numeric_limits<double>::infinity());
    h.seed(infinity);
    require(h.query(infinity), "infinite equality changed");
    require(h.query(zero), "nonfinite insertions damaged an existing key");
  }
}

void safe_resource_prefix_asymmetry() {
  for (Directions direction : {FWD, BWD}) {
    context = "retained-shorter resource-prefix equality";
    Harness h(direction);
    for (int i = 0; i < 64; ++i) {
      Label label = h.make(7., i);
      label.resource_consumption.resize(1);
      h.seed(label);
    }
    Label longer = h.search.efficient_labels[1][31];
    longer.resource_consumption.push_back(999.);
    longer.resource_consumption.push_back(-100.);
    // std::find evaluates retained==query. Never reverse this comparison:
    // the legacy predicate intentionally scans the retained vector's length.
    require(h.query(longer), "resource-vector length was incorrectly included in the key");
    longer.resource_consumption[0] = direction == FWD ? 300. : 1748.;
    require(!h.query(longer), "different retained resource prefix matched");
  }
}

void deletion_before_rejection_and_reinsertion() {
  for (Directions direction : {FWD, BWD}) {
    for (bool adaptive : {false, true}) {
      for (bool buckets : {false, true}) {
        context = "erase/reject/reinsert direction=" + std::to_string(direction) +
                  " adaptive=" + std::to_string(adaptive) + " buckets=" + std::to_string(buckets);
        Harness h(direction, adaptive, buckets);
        const Label deleted = h.make(20., 20.);
        h.seed(deleted);
        for (int i = 0; i < 96; ++i) h.seed(h.make(-100. - i, 500. + i));
        const Label dominator = h.make(0., 0.);
        h.seed(dominator);
        require(h.query(deleted) && h.query(dominator), "failed to build initial index");
        require(h.dominate(h.make(10., 10.)), "fixture failed to reject after erasing first member");
        require(!h.query(deleted), "erased label remains in duplicate index");
        require(h.query(dominator), "surviving label shifted out of duplicate index");
        for (size_t i = 0; i < h.search.efficient_labels[1].size(); i += 11)
          require(h.query(h.search.efficient_labels[1][i]), "shifted survivor missing");
        h.seed(deleted);
        require(h.query(deleted), "reinserted key still considered erased");
        require(!h.dominate(h.make(-10000., -1.)), "global dominator rejected");
        require(h.search.efficient_labels[1].empty(), "global dominance did not empty pool");
        require(!h.query(deleted) && !h.query(dominator), "empty pool retained stale keys");
        for (int i = 0; i < 65; ++i) h.seed(h.make(-500. - i, i));
        require(h.query(h.make(-564., 64.)), "regrown index missing its final label");
        require(!h.query(deleted), "old key resurrected when empty pool regrew");
      }
    }
  }
}
} // namespace

int main() {
  try {
    growth_reallocation_repartition_and_reset();
    hashed_field_collisions_and_ignored_metadata();
    signed_zero_and_nonfinite_values();
    safe_resource_prefix_asymmetry();
    deletion_before_rejection_and_reinsertion();
    std::cout << "Duplicate index differential tests passed: " << queries
              << " lookups, " << compared_labels << " exact retained-label comparisons\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
