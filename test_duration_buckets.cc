// Native membership tests, including erasure/reallocation and backward encoding.
#include "src/cc/search.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

using bidirectional::Search;
using labelling::Label;

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

Label label(double duration, double cost, bidirectional::Params* params, int node = 1) {
  std::vector<double> resources(2, 0.);
  resources[params->critical_res] = duration;
  return Label(cost, {node, node}, resources, {0, node}, params);
}

void verify(const Search& search) {
  require(search.duration_buckets.size() == search.efficient_labels.size(), "node count");
  for (size_t node = 0; node < search.efficient_labels.size(); ++node) {
    const auto& labels = search.efficient_labels[node];
    std::vector<int> seen(labels.size(), 0);
    for (const auto& bucket : search.duration_buckets[node]) {
      require(!bucket.second.empty(), "empty bucket retained");
      for (const auto index : bucket.second) {
        require(index < labels.size(), "stale label position");
        require(search.durationBucketIndex(labels[index]) == bucket.first, "wrong interval");
        ++seen[index];
      }
    }
    for (int count : seen) require(count == 1, "label missing or duplicated");
  }
}

void test_intervals_and_directions() {
  bidirectional::Params params;
  params.critical_res = 1;
  Search forward(bidirectional::FWD), backward(bidirectional::BWD);
  for (Search* search : {&forward, &backward}) {
    search->efficient_labels.resize(3);
    search->initializeDurationBuckets(1, 20., 5.);
  }
  require(forward.durationBucketIndex(label(0., 0., &params)) == 0, "source interval");
  require(forward.durationBucketIndex(label(std::nextafter(5., 0.), 0., &params)) == 0,
          "below boundary");
  require(forward.durationBucketIndex(label(5., 0., &params)) == 1, "boundary");
  require(forward.durationBucketIndex(label(-.1, 0., &params)) == -1, "negative interval");
  require(backward.durationBucketIndex(label(20., 0., &params)) == 0, "sink interval");
  require(backward.durationBucketIndex(label(15., 0., &params)) == 1, "backward duration");
  require(backward.durationBucketIndex(label(10., 0., &params)) == 2, "full bound inversion");
  for (const double duration : {0., 4., 5., 11.}) {
    forward.pushEfficientLabel(1, label(duration, 0., &params));
    backward.pushEfficientLabel(1, label(20. - duration, 0., &params));
  }
  forward.pushEfficientLabel(2, label(5., 0., &params, 2));
  require(forward.duration_buckets[0].empty(), "empty node creates buckets");
  require(forward.duration_buckets[1].size() == 3, "occupied intervals");
  verify(forward);
  verify(backward);
  // Reinitialization clears the old grouping and indexes the existing labels.
  forward.initializeDurationBuckets(1, 20., 2.);
  verify(forward);
  require(forward.duration_buckets[1].size() == 3, "reconfiguration");
}

void test_dominance_and_reallocation() {
  for (const auto direction : {bidirectional::FWD, bidirectional::BWD}) {
    bidirectional::Params params;
    params.elementary = false;
    Search search(direction);
    search.efficient_labels.resize(2);
    search.initializeDurationBuckets(0, 100., 5.);
    auto candidate = [&](double consumed, double cost) {
      return label(direction == bidirectional::FWD ? consumed : 100. - consumed,
                   cost, &params);
    };
    for (int i = 0; i < 40; ++i) {
      const auto next = candidate(i, 40. - i);
      require(!search.runDominance(1, next, false), "tradeoff falsely dominated");
      search.pushEfficientLabel(1, next);
      verify(search); // Includes repeated vector reallocations.
    }
    const auto better = candidate(5., -1.);
    require(!search.runDominance(1, better, false), "better rejected");
    verify(search); // Verify immediately after erase, before insertion.
    search.pushEfficientLabel(1, better);
    require(search.efficient_labels[1].size() == 6, "wrong surviving labels");
    require(search.duration_buckets[1].size() == 2, "empty intervals not removed");
    verify(search);
    require(search.runDominance(1, candidate(9., 100.), false), "bad label accepted");
    verify(search);
    const auto best = candidate(0., -2.);
    require(!search.runDominance(1, best, false), "best rejected");
    require(search.duration_buckets[1].empty(), "last deleted labels remain indexed");
    search.pushEfficientLabel(1, best);
    verify(search);
  }
}

void test_invalid_widths() {
  Search search(bidirectional::FWD);
  search.efficient_labels.resize(1);
  for (const double width : {0., -1., std::numeric_limits<double>::infinity(),
                             std::numeric_limits<double>::quiet_NaN()}) {
    bool rejected = false;
    try { search.initializeDurationBuckets(0, 20., width); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "invalid width accepted");
  }
}

void test_adaptive_cost_refinement() {
  for (const auto direction : {bidirectional::FWD, bidirectional::BWD}) {
    bidirectional::Params params;
    params.elementary = false;
    Search search(direction);
    search.efficient_labels.resize(2);
    search.initializeDurationBuckets(0, 600., 5., true);
    for (int i = 0; i < 512; ++i) {
      const double resource = direction == bidirectional::FWD ? i : 600. - i;
      // Cheap costs deliberately at HIGH durations: resource magnitude alone
      // must not determine which region receives finer intervals.
      const double cost = i >= 384 ? -100. : 100.;
      search.pushEfficientLabel(1, label(resource, cost, &params));
      if (i == 255) {
        // This must have adapted automatically during insertion.
        require(search.duration_buckets[1].size() < 52, "automatic adaptation missing");
      }
    }
    verify(search);
    const auto original = search.efficient_labels[1];
    search.adaptDurationBuckets(1);
    require(search.efficient_labels[1] == original, "adaptation changed labels/order");
    verify(search);
    const auto cheap = label(direction == bidirectional::FWD ? 450. : 150., 0., &params);
    const auto expensive = label(direction == bidirectional::FWD ? 200. : 400., 0., &params);
    const auto cheap_range = search.durationBucketBounds(1, search.durationBucketIndex(cheap));
    const auto expensive_range = search.durationBucketBounds(1, search.durationBucketIndex(expensive));
    require(cheap_range.second - cheap_range.first == 32., "cheap region not refined");
    require(expensive_range.second - expensive_range.first == 128., "costly region not coarser");
    require(search.duration_buckets[1].size() <= 64, "bucket cap exceeded");
    // Dominance deletes nearly everything; the partition must coarsen and its
    // membership remain correct, including immediately after deletion.
    const auto best = label(direction == bidirectional::FWD ? 0. : 600., -200., &params);
    require(!search.runDominance(1, best, false), "best label rejected");
    search.pushEfficientLabel(1, best);
    verify(search);
    require(search.duration_buckets[1].size() == 1, "sparse buckets not merged");
  }
}

void test_adaptive_ties_and_fixed_mode() {
  bidirectional::Params params;
  params.elementary = false;
  Search ties(bidirectional::FWD), fixed(bidirectional::FWD);
  ties.efficient_labels.resize(2);
  fixed.efficient_labels.resize(2);
  ties.initializeDurationBuckets(0, 1000., 5., true);
  fixed.initializeDurationBuckets(0, 1000., 5., false);
  for (int i = 0; i < 600; ++i) {
    ties.pushEfficientLabel(1, label(7., i, &params));
    fixed.pushEfficientLabel(1, label(i, i, &params));
  }
  ties.adaptDurationBuckets(1);
  fixed.adaptDurationBuckets(1);
  verify(ties);
  verify(fixed);
  require(ties.duration_buckets[1].size() == 1, "equal durations split");
  require(fixed.duration_buckets[1].size() == 120, "fixed mode adapted");
}

int main() {
  test_intervals_and_directions();
  test_dominance_and_reallocation();
  test_invalid_widths();
  test_adaptive_cost_refinement();
  test_adaptive_ties_and_fixed_mode();
  std::cout << "Duration-bucket membership and adaptation tests passed\n";
}
