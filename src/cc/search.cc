#include "search.h"

#include <cmath>
#include <functional>
#include <limits>
#include <stdexcept>

namespace bidirectional {

namespace {
size_t duplicateFingerprint(const labelling::Label& label) {
  // Resources intentionally remain in the exact equality check only: its
  // existing semantics compare the retained label's resource prefix.
  // Masks, Params, phi, user vertex IDs and queue IDs are not equality fields.
  size_t value = std::hash<int>{}(label.vertex.lemon_id);
  const auto combine = [&value](size_t next) {
    value ^= next + size_t{0x9e3779b9u} + (value << 6) + (value >> 2);
  };
  combine(std::hash<double>{}(label.weight == 0. ? 0. : label.weight));
  combine(label.partial_path.size());
  for (int node : label.partial_path) combine(std::hash<int>{}(node));
  return value;
}
std::uint64_t dominanceMask(const labelling::Label& label) {
  const auto* params = label.params_ptr;
  if (!params->elementary) return 0;
  if (params->use_bitwise_elementary && params->max_nodes > 0 && params->max_nodes <= 64)
    return label.unreachable_mask;
  // A subset-preserving projection for the set representation. Collisions for
  // IDs beyond 63 can only weaken screening; exact dominance still checks sets.
  std::uint64_t mask = 0;
  for (const int node : label.unreachable_nodes)
    mask |= std::uint64_t{1} << (static_cast<unsigned int>(node) & 63u);
  return mask;
}
void includeValue(double value, double& lower, double& upper) {
  if (std::isnan(value)) {
    lower = -std::numeric_limits<double>::infinity();
    upper = std::numeric_limits<double>::infinity();
  } else {
    lower = std::min(lower, value);
    upper = std::max(upper, value);
  }
}
} // namespace

void Search::BucketSummary::include(const labelling::Label& label, std::uint64_t mask) {
  if (count++ == 0) {
    min_weight = max_weight = label.weight;
    min_resources = max_resources = label.resource_consumption;
    mask_and = mask_or = mask;
    params = label.params_ptr;
    shared_params = true;
  } else {
    mask_and &= mask;
    mask_or |= mask;
    shared_params = shared_params && params == label.params_ptr;
  }
  includeValue(label.weight, min_weight, max_weight);
  for (size_t i = 0; i < min_resources.size(); ++i)
    includeValue(label.resource_consumption[i], min_resources[i], max_resources[i]);
}

unsigned char Search::BucketSummary::possibleDirections(
    const labelling::Label& label, Directions direction, std::uint64_t mask) const {
  bool candidate_dominates = !(label.weight > max_weight);
  bool member_dominates = !(min_weight > label.weight);
  // Different native parameter objects can use different dominance semantics.
  if (shared_params && params == label.params_ptr) {
    for (size_t i = 0; i < min_resources.size() && (candidate_dominates || member_dominates); ++i) {
      const double value = label.resource_consumption[i];
      if (direction != FWD && static_cast<int>(i) == params->critical_res) {
        if (value < min_resources[i]) candidate_dominates = false;
        if (max_resources[i] < value) member_dominates = false;
      } else {
        if (value > max_resources[i]) candidate_dominates = false;
        if (min_resources[i] > value) member_dominates = false;
      }
    }
    if (params->elementary) {
      if (mask & ~mask_or) candidate_dominates = false;
      if (mask_and & ~mask) member_dominates = false;
    }
  }
  return (candidate_dominates ? 1 : 0) | (member_dominates ? 2 : 0);
}

bool Search::runBucketDominance(const int& lemon_id, const labelling::Label& label,
                                labelling::DominanceStats* stats) {
  auto& labels = efficient_labels[lemon_id];
  if (label.params_ptr->timeLimitExceeded()) return true;
  const auto mask = dominanceMask(label);
  size_t eligible = 0;
  for (auto& entry : bucket_summaries_[lemon_id]) {
    if (label.params_ptr->timeLimitExceeded()) return true;
    auto& summary = entry.second;
    summary.directions = summary.possibleDirections(label, direction, mask);
    summary.filter_masks = summary.shared_params && summary.params == label.params_ptr &&
                           label.params_ptr->elementary;
    if (summary.directions) eligible += summary.count;
    if (stats) {
      ++stats->bucket_checks;
      stats->bucket_directions_skipped += !(summary.directions & 1);
      stats->bucket_directions_skipped += !(summary.directions & 2);
    }
  }
  // For selective queries, visit only potentially comparable positions. Sort
  // their union to preserve the original early-exit and deletion order.
  const bool sparse = eligible < labels.size() / 4;
  dominance_candidates_.clear();
  if (sparse) {
    for (const auto& entry : duration_buckets[lemon_id]) {
      if (bucket_summaries_[lemon_id].at(entry.first).directions)
        dominance_candidates_.insert(dominance_candidates_.end(), entry.second.begin(), entry.second.end());
    }
    std::sort(dominance_candidates_.begin(), dominance_candidates_.end());
  }
  const auto& summaries = label_summaries_[lemon_id];
  const size_t count = sparse ? dominance_candidates_.size() : labels.size();
  size_t erased = 0;
  for (size_t cursor = 0; cursor < count; ++cursor) {
    if (label.params_ptr->timeLimitExceeded()) return true;
    const size_t original = sparse ? dominance_candidates_[cursor] : cursor;
    const auto& cached = summaries[original];
    auto flags = cached.bucket->directions;
    // Use the cached subset-preserving signature before any path comparison or
    // exact resource/set checks. A collision only leaves an extra candidate.
    if (flags && cached.bucket->filter_masks) {
      if (mask & ~cached.mask) flags &= ~1u;
      if (cached.mask & ~mask) flags &= ~2u;
    }
    if (!flags) continue;
    const size_t position = original - erased;
    const auto& other = labels[position];
    if (stats) ++stats->label_pairs;
    if (label == other) continue;
    if (flags & 1) {
      if (stats) ++stats->exact_checks;
      if (label.checkDominance(other, direction)) {
        labels.erase(labels.begin() + position);
        ++erased;
        continue;
      }
    }
    if (flags & 2) {
      if (stats) ++stats->exact_checks;
      if (other.checkDominance(label, direction)) return true;
    }
  }
  return false;
}

Search::Search(const Directions& direction_in)
    : direction(direction_in),
      lower_bound_weight(std::make_unique<std::vector<double>>()),
      unprocessed_labels(std::make_unique<std::vector<labelling::Label>>()){};

void Search::initializeDurationBuckets(int critical_resource, double max_resource,
                                       double width, bool adaptive,
                                       bool bucket_dominance) {
  if (!std::isfinite(width) || width <= 0.) {
    throw std::invalid_argument("bucket_width must be finite and positive");
  }
  bucket_resource_ = critical_resource;
  bucket_max_resource_ = max_resource;
  bucket_width_ = width;
  adaptive_buckets_ = adaptive;
  bucket_dominance_ = bucket_dominance;
  duplicate_indices_.clear();
  duplicate_indices_.resize(efficient_labels.size());
  dominance_stats = {};
  extension_stats = {};
  extension_adjacency.clear();
  extension_adjacency.resize(efficient_labels.size());
  extension_nodes_.clear();
  extension_nodes_.resize(efficient_labels.size());
  bucket_summaries_.clear();
  bucket_summaries_.resize(efficient_labels.size());
  label_summaries_.clear();
  label_summaries_.resize(efficient_labels.size());
  bucket_partitions_.clear();
  bucket_partitions_.resize(efficient_labels.size());
  duration_buckets.clear();
  duration_buckets.resize(efficient_labels.size());
  for (size_t node = 0; node < efficient_labels.size(); ++node) {
    rebuildDurationBuckets(static_cast<int>(node));
  }
}

double Search::consumedBucketResource(const labelling::Label& label) const {
  const double stored = label.resource_consumption[bucket_resource_];
  return direction == BWD ? bucket_max_resource_ - stored : stored;
}

Search::DurationBucketIndex Search::durationBucketIndex(
    const labelling::Label& label) const {
  const double consumed = consumedBucketResource(label);
  const auto& partition = bucket_partitions_[label.vertex.lemon_id];
  if (partition.active) {
    if (!std::isfinite(consumed)) {
      throw std::overflow_error("nonfinite resource in duration bucket");
    }
    return std::upper_bound(partition.boundaries.begin(), partition.boundaries.end(),
                            consumed) - partition.boundaries.begin();
  }
  const double index = std::floor(consumed / bucket_width_);
  // Check before conversion; the upper endpoint 2^63 is not representable.
  const double lower = static_cast<double>(std::numeric_limits<DurationBucketIndex>::min());
  if (!std::isfinite(index) || index < lower || index >= -lower) {
    throw std::overflow_error("duration bucket index is outside the supported range");
  }
  return static_cast<DurationBucketIndex>(index);
}

void Search::appendDuplicateEntry(DuplicateIndex& index, const labelling::Label& label) {
  const size_t position = index.entries.size();
  const auto fingerprint = duplicateFingerprint(label);
  auto& chain = index.chains[fingerprint & (index.chains.size() - 1)];
  index.entries.push_back({fingerprint, DuplicateIndex::empty});
  if (chain.first == DuplicateIndex::empty) chain.first = position;
  else index.entries[chain.last].next = position;
  chain.last = position;
}

void Search::rebuildDuplicateIndex(const int& lemon_id) {
  auto& index = duplicate_indices_[lemon_id];
  const auto& labels = efficient_labels[lemon_id];
  size_t capacity = 64;
  while (capacity / 2 < labels.size()) {
    if (capacity > index.chains.max_size() / 2)
      throw std::length_error("duplicate-label index is too large");
    capacity *= 2;
  }
  index.valid = false;
  // Reuse allocations after deletions, while keeping the hash load <= 1/2.
  index.chains.assign(std::max(capacity, index.chains.size()), {});
  index.entries.clear();
  index.entries.reserve(labels.size());
  for (const auto& label : labels) appendDuplicateEntry(index, label);
  index.valid = true;
}

bool Search::containsEfficientLabel(const int& lemon_id, const labelling::Label& label) {
  const auto& labels = efficient_labels[lemon_id];
  if (labels.size() < 32)
    return std::find(labels.begin(), labels.end(), label) != labels.end();
  auto& index = duplicate_indices_[lemon_id];
  if (!index.valid || index.entries.size() != labels.size())
    rebuildDuplicateIndex(lemon_id);
  const auto fingerprint = duplicateFingerprint(label);
  size_t position = index.chains[fingerprint & (index.chains.size() - 1)].first;
  while (position != DuplicateIndex::empty) {
    const auto& entry = index.entries[position];
    // Chains retain ascending vector positions and the original operand order.
    if (entry.fingerprint == fingerprint && labels[position] == label) return true;
    position = entry.next;
  }
  return false;
}

void Search::pushEfficientLabel(const int& lemon_id, const labelling::Label& label) {
  const auto bucket = durationBucketIndex(label);
  auto& labels = efficient_labels[lemon_id];
  labels.push_back(label);
  auto& duplicates = duplicate_indices_[lemon_id];
  if (duplicates.valid) {
    if (duplicates.entries.size() + 1 != labels.size() ||
        labels.size() > duplicates.chains.size() / 2) {
      duplicates.valid = false; // Rebuild only if another indexed lookup needs it.
    } else appendDuplicateEntry(duplicates, labels.back());
  }
  duration_buckets[lemon_id][bucket].push_back(labels.size() - 1);
  if (bucket_dominance_) {
    auto& summary = bucket_summaries_[lemon_id][bucket];
    const auto mask = dominanceMask(label);
    summary.include(label, mask);
    label_summaries_[lemon_id].push_back({&summary, mask});
  }
  maybeAdaptDurationBuckets(lemon_id, 1);
}

std::pair<double, double> Search::durationBucketBounds(
    const int& lemon_id, DurationBucketIndex index) const {
  const auto& partition = bucket_partitions_[lemon_id];
  if (!partition.active) {
    return {static_cast<double>(index) * bucket_width_,
            (static_cast<double>(index) + 1.) * bucket_width_};
  }
  const size_t i = static_cast<size_t>(index);
  if (index < 0 || i > partition.boundaries.size()) {
    throw std::out_of_range("invalid adaptive duration bucket");
  }
  return {i == 0 ? -std::numeric_limits<double>::infinity()
                : partition.boundaries[i - 1],
          i == partition.boundaries.size() ? std::numeric_limits<double>::infinity()
                                           : partition.boundaries[i]};
}

bool Search::maybeAdaptDurationBuckets(const int& lemon_id, size_t changes) {
  if (!adaptive_buckets_) return false;
  auto& partition = bucket_partitions_[lemon_id];
  partition.changes += changes;
  if (partition.changes < partition.next_update) return false;
  adaptDurationBuckets(lemon_id);
  return true;
}

void Search::adaptDurationBuckets(const int& lemon_id) {
  if (!adaptive_buckets_) return;
  const auto& labels = efficient_labels[lemon_id];
  auto& partition = bucket_partitions_[lemon_id];
  partition.changes = 0;
  ++partition.revision;
  // Repartition at most once per 256 changes, and less often for large pools.
  partition.next_update = std::max(size_t{256}, labels.size() / 2);
  partition.active = true;
  partition.boundaries.clear();
  if (labels.size() < 2) {
    rebuildDurationBuckets(lemon_id);
    return;
  }

  std::vector<double> costs;
  costs.reserve(labels.size());
  std::vector<std::pair<double, size_t>> ordered;
  ordered.reserve(labels.size());
  for (size_t i = 0; i < labels.size(); ++i) {
    costs.push_back(labels[i].weight);
    ordered.emplace_back(consumedBucketResource(labels[i]), i);
  }
  // Partial additive cost is only a refinement heuristic, never a completion
  // bound or a dominance certificate. Keep all exact resources, including SRI.
  const auto quartile = costs.begin() + (costs.size() - 1) / 4;
  std::nth_element(costs.begin(), quartile, costs.end());
  const double cheap_cost = *quartile;
  std::sort(ordered.begin(), ordered.end());
  size_t total_weight = 0;
  for (const auto& label : labels) total_weight += label.weight <= cheap_cost ? 4 : 1;
  // Target about 32 cheap labels or 128 other labels per interval. Cap the
  // partition at 64 intervals to bound its indexing overhead.
  const size_t target = std::max(size_t{128}, (total_weight + 63) / 64);
  size_t accumulated = 0;
  for (size_t i = 0; i + 1 < ordered.size(); ++i) {
    accumulated += labels[ordered[i].second].weight <= cheap_cost ? 4 : 1;
    const double left = ordered[i].first;
    const double right = ordered[i + 1].first;
    if (accumulated >= target && left < right && partition.boundaries.size() < 63) {
      double cut = left + (right - left) * .5;
      if (!std::isfinite(cut)) cut = left * .5 + right * .5;
      if (!(left < cut && cut <= right)) cut = right;
      partition.boundaries.push_back(cut);
      accumulated = 0;
    }
  }
  // Recomputing the partition can both split crowded/cheap ranges and merge
  // ranges emptied by dominance. Do not reorder the actual labels or the heap.
  rebuildDurationBuckets(lemon_id);
}

void Search::rebuildDurationBuckets(const int& lemon_id) {
  auto& buckets = duration_buckets[lemon_id];
  // Reuse capacities in occupied buckets; remove buckets that become empty.
  for (auto& entry : buckets) entry.second.clear();
  const auto& labels = efficient_labels[lemon_id];
  auto& summaries = bucket_summaries_[lemon_id];
  auto& pointers = label_summaries_[lemon_id];
  for (auto& entry : summaries) entry.second.count = 0;
  pointers.clear();
  if (bucket_dominance_) pointers.reserve(labels.size());
  for (size_t i = 0; i < labels.size(); ++i) {
    const auto bucket = durationBucketIndex(labels[i]);
    buckets[bucket].push_back(i);
    if (bucket_dominance_) {
      auto& summary = summaries[bucket];
      const auto mask = dominanceMask(labels[i]);
      summary.include(labels[i], mask);
      pointers.push_back({&summary, mask});
    }
  }
  for (auto it = buckets.begin(); it != buckets.end();) {
    if (it->second.empty()) {
      summaries.erase(it->first);
      it = buckets.erase(it);
    } else ++it;
  }
}

bool Search::runDominance(const int& lemon_id, const labelling::Label& label,
                           const bool& elementary) {
  auto& labels = efficient_labels[lemon_id];
  const auto previous_size = labels.size();
  auto* stats = label.params_ptr->profile_dominance ? &dominance_stats : nullptr;
  std::chrono::steady_clock::time_point start;
  if (stats) {
    start = std::chrono::steady_clock::now();
    ++stats->queries;
  }
  // Small pools are cheaper to scan directly than to query bucket summaries.
  const bool dominated = bucket_dominance_ && labels.size() >= 32
      ? runBucketDominance(lemon_id, label, stats)
      : labelling::runDominanceEff(&labels, label, direction, elementary, stats);
  if (labels.size() != previous_size) duplicate_indices_[lemon_id].valid = false;
  // erase() shifts vector positions, including when the candidate is rejected
  // or the dominance scan stops at its time limit.
  if (labels.size() != previous_size &&
      !maybeAdaptDurationBuckets(lemon_id, previous_size - labels.size())) {
    rebuildDurationBuckets(lemon_id);
  }
  if (stats) stats->seconds += std::chrono::duration<double>(
      std::chrono::steady_clock::now() - start).count();
  return dominated;
}

const std::vector<unsigned char>& Search::bucketExtensionMask(
    const labelling::Label& label, const std::vector<AdjVertex>& arcs,
    const std::vector<double>& max_res, const std::vector<double>& min_res) {
  const int critical = label.params_ptr->critical_res;
  const double stored = label.resource_consumption[critical];
  if (label.params_ptr->ref_callback != nullptr || critical != bucket_resource_ ||
      !std::isfinite(stored)) {
    extension_fallback_.assign(arcs.size(), 0);
    return extension_fallback_;
  }
  const int node = label.vertex.lemon_id;
  auto& node_cache = extension_nodes_[node];
  const auto revision = bucket_partitions_[node].revision;
  if (node_cache.revision != revision) {
    // Lazy invalidation keeps an in-use arc mask valid during self-loop
    // extensions that repartition this node while processing the current label.
    node_cache.buckets.clear();
    node_cache.revision = revision;
  }
  const auto bucket = durationBucketIndex(label);
  auto& cache = node_cache.buckets[bucket];
  if (cache.ready && cache.blocked.size() == arcs.size() &&
      cache.min_bound == min_res[critical] && cache.max_bound == max_res[critical] &&
      cache.lower <= stored && stored <= cache.upper) {
    if (label.params_ptr->profile_extension) ++extension_stats.cache_hits;
    return cache.blocked;
  }
  if (label.params_ptr->profile_extension) ++extension_stats.cache_rebuilds;
  const auto interval = durationBucketBounds(node, bucket);
  double lower = interval.first, upper = interval.second;
  if (direction == BWD) {
    lower = bucket_max_resource_ - interval.second;
    upper = bucket_max_resource_ - interval.first;
  }
  // A closed envelope is conservative at the open upper boundary. Include the
  // ACTUAL value too: division/inversion rounding or a stale heap label must
  // never make us use a proof for a resource value outside its envelope.
  cache.lower = std::min(lower, stored);
  cache.upper = std::max(upper, stored);
  cache.min_bound = min_res[critical];
  cache.max_bound = max_res[critical];
  cache.blocked.assign(arcs.size(), 0);
  for (size_t i = 0; i < arcs.size(); ++i) {
    const double consumption = arcs[i].resource_consumption[critical];
    if (!std::isfinite(consumption)) continue;
    double smallest, largest;
    if (direction == FWD) {
      smallest = cache.lower + consumption;
      largest = cache.upper + consumption;
    } else {
      // Match additiveBackwardREF, including its nonpositive-arc convention.
      const double decrement = consumption > 0. ? consumption : 1.;
      smallest = cache.lower - decrement;
      largest = cache.upper - decrement;
    }
    cache.blocked[i] = (std::isfinite(smallest) && smallest > cache.max_bound) ||
                       (std::isfinite(largest) && largest < cache.min_bound);
  }
  cache.ready = true;
  return cache.blocked;
}

void Search::makeHeap() {
  if (direction == FWD) {
    // Min-heap
    std::make_heap(
        unprocessed_labels->begin(),
        unprocessed_labels->end(),
        std::greater<>{});
  } else {
    // Max-heap
    std::make_heap(unprocessed_labels->begin(), unprocessed_labels->end());
  }
}

void Search::pushHeap() {
  if (unprocessed_labels->size() > 1) {
    if (direction == FWD) {
      // Min-heap
      std::push_heap(
          unprocessed_labels->begin(),
          unprocessed_labels->end(),
          std::greater<>{});
    } else {
      // Max-heap
      std::push_heap(unprocessed_labels->begin(), unprocessed_labels->end());
    }
  }
}
} // namespace bidirectional
