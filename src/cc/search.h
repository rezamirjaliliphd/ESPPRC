#ifndef SRC_CC_SEARCH_H__
#define SRC_CC_SEARCH_H__

#include <memory> // unique_ptr
#include <cstdint>
#include <map>
#include <set>
#include <vector>

#include "labelling.h"

namespace bidirectional {

class Search {
 public:
  using DurationBucketIndex = std::int64_t;
  // Positions in efficient_labels[node]; no additional Label/path copies.
  using DurationBuckets = std::map<DurationBucketIndex, std::vector<size_t>>;
  /// Direction of Search
  Directions direction;
  //  Params     params;
  /// Stopping criteria for each direction
  bool stop = false;
  /// Stopping criteria for each direction
  bool bound_exceeded = false;
  /// Number of unprocessed labels generated
  int unprocessed_count = 0;
  /// Number of labels processed
  int processed_count = 0;
  /// Number of labels generated (includes the possibly infeasible extensions)
  int generated_count = 0;

  /* Search-related parameters */

  /// Lower bounds from any node to sink
  std::unique_ptr<std::vector<double>> lower_bound_weight;
  /// vector with indices of vertices visited
  std::set<int>                     visited_vertices;
  std::shared_ptr<labelling::Label> current_label;
  /// Intermediate current best label with possibly complete source-sink path
  /// (shared pointer as we want to be able to substitute it without
  /// resetting)
  std::shared_ptr<labelling::Label> intermediate_label;

  /// vector with pareto optimal labels (per node) in each direction
  std::vector<std::vector<labelling::Label>> efficient_labels;
  /// Nonempty critical-resource intervals, maintained as labels are accepted
  /// and removed. Dominance filters these intervals; search order is unchanged.
  std::vector<DurationBuckets> duration_buckets;
  /// vector with pointer to label with least weight (per node) in each
  /// direction
  std::vector<std::shared_ptr<labelling::Label>> best_labels;
  /**
   * heap vector to keep unprocessed labels ordered.
   * the order depends on the on the direction of the search.
   * i.e. forward -> increasing in the monotone resource,
   * backward -> decreasing in the monotone resource.
   */
  std::unique_ptr<std::vector<labelling::Label>> unprocessed_labels;

  // TODO: Use bucket-heap
  /* Heap operations for vector of labels */

  /**
   * Initialises heap using the appropriate comparison
   * i.e. increasing in the monotone resource forward lists, decreasing
   * otherwise
   */
  void makeHeap();

  /**
   * Push new elements in heap using the appropriate comparison
   * i.e. increasing in the monotone resource forward lists, decreasing
   * otherwise
   */
  void pushHeap();

  void pushUnprocessedLabel(const labelling::Label& label) {
    unprocessed_labels->push_back(label);
    if (label.params_ptr->enable_parallel)
      unprocessed_labels->back().extension_id = ++next_extension_id_;
    pushHeap();
  }

  void appendUnprocessedLabel(const labelling::Label& label) {
    unprocessed_labels->push_back(label);
    if (label.params_ptr->enable_parallel)
      unprocessed_labels->back().extension_id = ++next_extension_id_;
  }

  void pushEfficientLabel(const int& lemon_id, const labelling::Label& label);

  /// Match the existing retained-label equality rule. Large pools use a
  /// fingerprint index; collisions still receive the original exact check.
  bool containsEfficientLabel(const int& lemon_id, const labelling::Label& label);

  /// Configure once after the critical resource has been selected. Backward
  /// buckets use max_resource - stored_resource, with the original full bound.
  void initializeDurationBuckets(int critical_resource, double max_resource,
                                 double width, bool adaptive = true,
                                 bool bucket_dominance = true);
  DurationBucketIndex durationBucketIndex(const labelling::Label& label) const;
  /// Repartition one node using label density and partial-cost ranks. This
  /// changes membership only; it does not establish dominance or remove labels.
  void adaptDurationBuckets(const int& lemon_id);
  std::pair<double, double> durationBucketBounds(
      const int& lemon_id, DurationBucketIndex index) const;

  /// Screen impossible comparisons, retaining the original scan order and
  /// exact label dominance test. Counters/timing are opt-in via Params.
  labelling::DominanceStats dominance_stats;
  bool runDominance(const int& lemon_id, const labelling::Label& label,
                    const bool& elementary);

  struct ExtensionStats {
    std::uint64_t labels_processed = 0, arcs_considered = 0;
    std::uint64_t eligible_extensions = 0, full_extensions = 0;
    std::uint64_t bucket_rejections = 0, infeasible_extensions = 0;
    std::uint64_t cache_hits = 0, cache_rebuilds = 0, parallel_labels = 0;
    double generation_seconds = 0., total_seconds = 0.;
    std::uint64_t parallel_batches = 0, prepared_labels = 0;
    std::uint64_t prepared_extensions = 0, discarded_labels = 0;
  } extension_stats;
  struct ExtensionAdjacency {
    bool ready = false;
    std::vector<AdjVertex> arcs;
  };
  // Immutable adjacency in original graph iteration order, initialized lazily.
  std::vector<ExtensionAdjacency> extension_adjacency;
  // Arc order/resources must remain fixed until initializeDurationBuckets().
  // Each cached proof is checked against the actual heap label's raw resource.
  const std::vector<unsigned char>& bucketExtensionMask(
      const labelling::Label& label, const std::vector<AdjVertex>& arcs,
      const std::vector<double>& max_res, const std::vector<double>& min_res);

  /// Replace best label
  void replaceBestLabel(const int& lemon_id, const labelling::Label& label) {
    auto label_ptr = std::make_shared<labelling::Label>(label);
    best_labels[lemon_id].swap(label_ptr);
  }

  /// Replace best label
  void replaceCurrentLabel(const labelling::Label& label) {
    auto label_ptr = std::make_shared<labelling::Label>(label);
    current_label.swap(label_ptr);
  }

  /// Replace intermediate label
  void replaceIntermediateLabel(const labelling::Label& label) {
    auto label_ptr = std::make_shared<labelling::Label>(label);
    intermediate_label.swap(label_ptr);
  }

  /// Update vertices visited
  void addVisitedVertex(const int& lemon_id) {
    visited_vertices.insert(lemon_id);
  }

  Search(const Directions& direction_in);
  ~Search(){};

 private:
  struct DuplicateIndex {
    static constexpr size_t empty = static_cast<size_t>(-1);
    struct Chain { size_t first = empty, last = empty; };
    struct Entry { size_t fingerprint, next = empty; };
    // Only positions and hashes: no extra Label/path copies or removed history.
    std::vector<Chain> chains;
    std::vector<Entry> entries;
    bool valid = false;
  };
  std::vector<DuplicateIndex> duplicate_indices_;
  void rebuildDuplicateIndex(const int& lemon_id);
  void appendDuplicateEntry(DuplicateIndex& index, const labelling::Label& label);
  std::uint64_t next_extension_id_ = 0;
  int bucket_resource_ = 0;
  double bucket_max_resource_ = 0.;
  double bucket_width_ = 5.;
  bool adaptive_buckets_ = true;
  bool bucket_dominance_ = true;
  struct BucketSummary {
    size_t count = 0;
    double min_weight = 0., max_weight = 0.;
    std::vector<double> min_resources, max_resources;
    std::uint64_t mask_and = 0, mask_or = 0;
    const Params* params = nullptr;
    bool shared_params = true;
    // Query-local permissions: 1=candidate dominates member, 2=reverse.
    unsigned char directions = 3;
    bool filter_masks = false;
    void include(const labelling::Label& label, std::uint64_t mask);
    unsigned char possibleDirections(const labelling::Label& label,
                                     Directions direction,
                                     std::uint64_t mask) const;
  };
  std::vector<std::map<DurationBucketIndex, BucketSummary>> bucket_summaries_;
  // Stable map-entry pointers indexed by the ORIGINAL retained-label position.
  struct LabelSummary {
    BucketSummary* bucket;
    std::uint64_t mask;
  };
  std::vector<std::vector<LabelSummary>> label_summaries_;
  std::vector<size_t> dominance_candidates_;
  bool runBucketDominance(const int& lemon_id, const labelling::Label& label,
                          labelling::DominanceStats* stats);
  struct AdaptivePartition {
    bool active = false;
    std::uint64_t revision = 0;
    size_t changes = 0;
    size_t next_update = 256;
    // Increasing internal boundaries; the two outer intervals are unbounded.
    std::vector<double> boundaries;
  };
  std::vector<AdaptivePartition> bucket_partitions_;
  struct ExtensionBucket {
    bool ready = false;
    double lower = 0., upper = 0., min_bound = 0., max_bound = 0.;
    std::vector<unsigned char> blocked;
  };
  struct ExtensionNode {
    std::uint64_t revision = 0;
    std::map<DurationBucketIndex, ExtensionBucket> buckets;
  };
  std::vector<ExtensionNode> extension_nodes_;
  std::vector<unsigned char> extension_fallback_;
  double consumedBucketResource(const labelling::Label& label) const;
  bool maybeAdaptDurationBuckets(const int& lemon_id, size_t changes);
  void rebuildDurationBuckets(const int& lemon_id);
};

} // namespace bidirectional

#endif // SRC_CC_SEARCH_H__
