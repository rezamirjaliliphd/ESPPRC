#ifndef SRC_CC_BIDIRECTIONAL_H__
#define SRC_CC_BIDIRECTIONAL_H__

#include <array>
#include <exception>
#include <unordered_map>
#include <chrono> // timing (e.g. time_point)
#include <vector>

// cspy
#include "src/cc/config.h" // log-level
#include "src/cc/digraph.h"
#include "src/cc/params.h"
#include "src/cc/ref_callback.h"
#include "src/cc/search.h"

// logging
#include "spdlog/spdlog.h" // after config.h as

namespace bidirectional {

/**
 * BiDirectional algorithm. see docs
 *
 * 1. ctor (memory allocation for the graph)
 * 2. add edges to graph using `addEdge`.
 * 3. [optional] set solving parameters if desired (e.g. time_limit, ...)
 * 	3.1. [optional] set callback using `setREFCallback`
 * 	3.2. [optional] set seed using `setSeed`
 * 4. call `run`
 */
class BiDirectional {
#ifdef ESPPRC_TESTING
  friend struct BucketJoiningTestAccess;
  friend struct BucketExtensionTestAccess;
#endif
 public:
  /**
   * @param[in] number_vertices, int number of vertices in the graph (to be
   * added using addEdge)
   * @param[in] number_edges, int number of edges in the graph
   * @param[in] source_id, int vertex id for the source
   * @param[in] sink_id, int vertex id for the sink
   * @param[in] max_res, vector of double with upper bound for resource
   * consumption
   * @param[in] min_res, vector of double with lower bound for resource
   * consumption
   */
  BiDirectional(
      const int&                 number_vertices,
      const int&                 number_edges,
      const int&                 source_id,
      const int&                 sink_id,
      const std::vector<double>& max_res_in,
      const std::vector<double>& min_res_in,
      const std::vector<double>& CG_cut_pi_in
    );

  /// Default destructor
  ~BiDirectional(){};

  /* Parameters */
  /// vector with upper and lower bounds for resources
  std::vector<double> max_res;
  std::vector<double> min_res;
  int                 source_id;
  int                 sink_id;
  std::vector<double> CG_cut_pi;

  /* Methods */

  /* Graph creation */
  /// Wrapper to add nodes to the graph. @see DiGraph::addNodes
  void addNodes(const std::vector<int>& nodes) { graph_ptr_->addNodes(nodes); }
  /// Add an edge to the graph
  void addEdge(
      const int&                 tail,
      const int&                 head,
      const double&              weight,
      const std::vector<double>& resource_consumption) {
    graph_ptr_->addEdge(tail, head, weight, resource_consumption);
  }
  /// run the algorithm (assumes all the appropriate options are set)
  void run();
  /* Getters */

  /// Return the final path
  std::vector<int> getPath() const;
  /// Return the consumed resources
  std::vector<double> getConsumedResources() const;
  /// Return the total cost
  double getTotalCost() const;
  /// After running the algorithm, one can check if critical resource is tight
  /// (difference between final resource and maximum) and prints a message if it
  /// doesn't match to the one chosen in Params.
  void checkCriticalRes() const;

  /* Setters: wrappers. @see bidirectional::Params */

  /// @see bidirectional::Params
  void setDirection(const std::string& direction_in) {
    params_ptr_->setDirection(direction_in);
  };
  /// @see bidirectional::Params
  void setMethod(const std::string& method_in) {
    params_ptr_->setMethod(method_in);
  }
  /// @see bidirectional::Params
  void setTimeLimit(const double& time_limit_in) {
    params_ptr_->setTimeLimit(time_limit_in);
  }
  /// @see bidirectional::Params
  void setThreshold(const double& threshold_in) {
    params_ptr_->setThreshold(threshold_in);
  }
  /// @see bidirectional::Params
  void setElementary(const bool& elementary_in) {
    params_ptr_->setElementary(elementary_in);
  }
  /// @see bidirectional::Params
  void setTwoCycleElimination(const bool& two_cycle_elimination_in) {
    params_ptr_->setTwoCycleElimination(two_cycle_elimination_in);
  }
  /// @see bidirectional::Params
  void setBoundsPruning(const bool& bounds_pruning_in) {
    params_ptr_->setBoundsPruning(bounds_pruning_in);
  }
  /// @see bidirectional::Params and getCriticalRes
  void setFindCriticalRes(const bool& find_critical_res_in) {
    params_ptr_->setFindCriticalRes(find_critical_res_in);
  }
  /// @see bidirectional::Params
  void setCriticalRes(const int& critical_res_in) {
    params_ptr_->setCriticalRes(critical_res_in);
  }

  void setBucketWidth(const double& width) { params_ptr_->bucket_width = width; }
  void setAdaptiveBuckets(const bool& enable) { params_ptr_->adaptive_buckets = enable; }
  void setBucketDominance(const bool& enable) { params_ptr_->bucket_dominance = enable; }
  void setProfileDominance(const bool& enable) { params_ptr_->profile_dominance = enable; }
  void setBucketExtension(const bool& enable) { params_ptr_->bucket_extension = enable; }
  void setProfileExtension(const bool& enable) { params_ptr_->profile_extension = enable; }
  void setBucketJoining(const bool& enable) { params_ptr_->bucket_joining = enable; }
  void setProfileJoining(const bool& enable) { params_ptr_->profile_joining = enable; }
  void setParallelJoining(const bool& enable, const int& threads = 1) {
    params_ptr_->parallel_joining = enable;
    params_ptr_->joining_threads = threads > 1 ? threads : 1;
  }
  std::vector<double> getJoiningStats() const {
    return {static_cast<double>(joining_stats_.group_checks),
            static_cast<double>(joining_stats_.groups_skipped),
            static_cast<double>(joining_stats_.blocks_skipped),
            static_cast<double>(joining_stats_.label_pairs),
            static_cast<double>(joining_stats_.full_merges),
            static_cast<double>(joining_stats_.resource_shortcuts),
            joining_stats_.seconds,
            static_cast<double>(joining_stats_.parallel_batches),
            static_cast<double>(joining_stats_.parallel_pairs),
            static_cast<double>(joining_stats_.speculative_records)};
  }
  std::vector<double> getExtensionStats() const {
    const auto& a = fwd_search_ptr_->extension_stats;
    const auto& b = bwd_search_ptr_->extension_stats;
    return {static_cast<double>(a.labels_processed + b.labels_processed),
            static_cast<double>(a.arcs_considered + b.arcs_considered),
            static_cast<double>(a.eligible_extensions + b.eligible_extensions),
            static_cast<double>(a.full_extensions + b.full_extensions),
            static_cast<double>(a.bucket_rejections + b.bucket_rejections),
            static_cast<double>(a.infeasible_extensions + b.infeasible_extensions),
            static_cast<double>(a.cache_hits + b.cache_hits),
            static_cast<double>(a.cache_rebuilds + b.cache_rebuilds),
            static_cast<double>(a.parallel_labels + b.parallel_labels),
            a.generation_seconds + b.generation_seconds,
            a.total_seconds + b.total_seconds,
            static_cast<double>(a.parallel_batches + b.parallel_batches),
            static_cast<double>(a.prepared_labels + b.prepared_labels),
            static_cast<double>(a.prepared_extensions + b.prepared_extensions),
            static_cast<double>(a.discarded_labels + b.discarded_labels)};
  }
  std::vector<double> getDominanceStats() const {
    const auto& a = fwd_search_ptr_->dominance_stats;
    const auto& b = bwd_search_ptr_->dominance_stats;
    return {static_cast<double>(a.queries + b.queries),
            static_cast<double>(a.label_pairs + b.label_pairs),
            static_cast<double>(a.exact_checks + b.exact_checks),
            static_cast<double>(a.bucket_checks + b.bucket_checks),
            static_cast<double>(a.bucket_directions_skipped + b.bucket_directions_skipped),
            a.seconds + b.seconds};
  }
  /// Pass python callback for label extensions.
  /// Note: swig needs namespace specifier
  void setREFCallback(bidirectional::REFCallback* cb) {
    params_ptr_->setREFCallback(cb);
  }

  /// Set k-cycle elimination
  /// @see bidirectional::Params
  void setKCycleElimination(const int& k) {
    params_ptr_->setKCycleElimination(k);
  }

  /// Enable time window constraints
  /// @see bidirectional::Params
  void setTimeWindows(const bool& enable, const int& res_idx = 0) {
    params_ptr_->setTimeWindows(enable, res_idx);
  }

  /// Enable bitwise elementary checking
  /// @see bidirectional::Params
  void setBitwiseElementary(const bool& enable, const int& max_nodes_in = 0) {
    params_ptr_->setBitwiseElementary(enable, max_nodes_in);
  }

  /// Enable OpenMP-assisted label extension when available.
  /// @see bidirectional::Params
  void setParallel(const bool& enable, const int& threads = 1) {
    params_ptr_->setParallel(enable, threads);
  }

  /// Retain merged labels and paths only when the caller needs them.
  void setCollectPaths(const bool& enable) { collect_paths_ = enable; }

  /// set random using a given seed
  // void setSeed(const int& seed) { std::srand(seed); }
  // Return every feasible merged label (source→sink) found during join
  const std::vector<labelling::Label>& getAllFeasibleSolutions() const { return merged_labels_; }
  const std::vector<std::vector<int>>& getAllFeasiblePaths() const {return paths;}
  double getBestBaseCost()  const { return best_base_cost_; }
  double getBestExtraCost() const { return best_extra_cost_; }


 private:
  struct JoiningStats {
    std::uint64_t group_checks = 0, groups_skipped = 0, blocks_skipped = 0;
    std::uint64_t label_pairs = 0, full_merges = 0, resource_shortcuts = 0;
    std::uint64_t parallel_batches = 0, parallel_pairs = 0, speculative_records = 0;
    double seconds = 0.;
  } joining_stats_;
  /// Search options for the algorithm. @see bidirectional::Params
  std::unique_ptr<Params> params_ptr_;
  /// Pointer to graph
  std::unique_ptr<DiGraph> graph_ptr_;
  double best_base_cost_  = std::numeric_limits<double>::infinity(); // join base
  double best_extra_cost_ = 0.0;
                                  // pi⋅floor term


  /// Start time to ensure time limit is met
  std::chrono::time_point<std::chrono::system_clock> start_time_;
  /// Current primal bound for a complete source-sink path
  double primal_st_bound_ = std::numeric_limits<double>::infinity();
  /// iteration number
  int iteration_ = 0;
  // whether the search terminated early with a valid source-sink path
  bool       terminated_early_w_st_path_ = false;
  Directions terminated_early_w_st_path_direction_;
  bool       time_limit_reached_ = false;
  std::vector<labelling::Label> merged_labels_;
  std::vector<std::vector<int>> paths;
  // Preserve the native API's historical collection behavior by default.
  bool collect_paths_ = true;
  /// Vectors with current maximum and minimum resources (first entry contains
  /// the dynamic halfway point).
  std::vector<double> max_res_curr_, min_res_curr_;

  /* labels and containers */

  /// Final best label (merged or otherwise)
  std::shared_ptr<labelling::Label> best_label_;

  std::unique_ptr<Search> fwd_search_ptr_, bwd_search_ptr_;

  struct PreparedExtension {
    PreparedExtension() = default;
    PreparedExtension(PreparedExtension&&) = default;
    PreparedExtension& operator=(PreparedExtension&&) = default;
    PreparedExtension(const PreparedExtension&) = delete;
    PreparedExtension& operator=(const PreparedExtension&) = delete;
    std::vector<std::unique_ptr<labelling::Label>> children;
    std::uint64_t generated = 0;
    bool expired = false;
    std::exception_ptr error;
  };
  struct ExtensionPrefetch {
    std::unordered_map<std::uint64_t, PreparedExtension> pending;
    size_t misses = 0;
  };
  std::array<ExtensionPrefetch, 2> extension_prefetch_;
  bool extendFromBatch(const Directions& direction,
                       const std::vector<AdjVertex>& arcs,
                       const std::vector<unsigned char>* blocked);
  const std::vector<AdjVertex>& cachedExtensionArcs(const Directions& direction, int node);
  void clearExtensionBatches();

  /* Initialisation Methods */

  /// Wrapper to initialise search in the appropriate direction
  void init();
  /// Initialise respective Search
  void initSearch(const Directions& direction);
  /// Initialise labels with appropriate resources
  void initResourceBounds();
  /// Make `unprocessed_labels_` a heap
  void initContainers();
  /// Construct first label and initialise `unprocessed_labels_` heap,
  /// `best_labels` source bucket and `visited_vertices`.
  void initLabels(const Directions& direction);
  /**
   * Run preprocessing steps if required. Currently includes:
   *   - Obtain shortest paths bounds
   *   - TODO: Bounding and adjusting of resource bounds
   */
  void runPreprocessing();

  /* Search Methods */
  /// Get the next direction to search
  Directions getDirection() const;

  Search* getSearchPtr(Directions direction) {
    if (direction == FWD)
      return fwd_search_ptr_.get();
    return bwd_search_ptr_.get();
  };

  /// Advance the search in a given direction
  void move(const Directions& direction);

  /// checks if the time_limit if over (if set) or if a label under the
  /// threshold has been found (if set). Sets terminated_early_w_st_path_
  bool terminate(const Directions& direction, const labelling::Label& label);
  /// @overload to just use current intermediate_label in direction
  bool terminate(const Directions& direction);

  /* checks */

  /// Wrapper to check whether a label is valid. Checks s-t path + threshold.
  bool checkValidLabel(
      const Directions&       direction,
      const labelling::Label& label);

  /// Returns true if half-way points are being violated. False otherwise
  bool checkBounds(const Directions& direction);

  /**
   * If bounds_pruning is true then check whether an input label plus a lower
   * bound for the path (from the node of the label to the source/sink)
   * violates the current primal bound.
   *
   * @return true if primal bound is violated, false otherwise.
   * i.e. if true, then input label cannot appear in the optimal solution.
   */
  bool checkPrimalBound(
      const Directions&       direction,
      const labelling::Label& candidate_label);

  /**
   * checks if a given vertex has been visited in a given direction
   *
   * @param[in] direction, Directions
   * @param[in] vertex_idx, int. Lemon id of vertex to check.
   * @return true if it has been visited false otherwise
   */
  bool checkVertexVisited(const Directions& direction, const int& vertex_idx);

  /// Call labelling::getNextLabel to get next label in the heap
  /// (unprocessed_labels_) and update the current_label member
  void updateCurrentLabel(const Directions& direction);

  /// Update half-way points and check if the current labels violates them, in
  /// which case sets the stop member to true.
  void updateHalfWayPoints(const Directions& direction);

  /**
   * Given a candidate_label by looking at the vector of labels (bucket) for
   * that node, we can check if the candidate_label has already been saved or
   * it is dominated. If both of these conditions are false, not seen and not
   * dominated, then we add it to the bucket.
   * Additionally, if the bounds_pruning option is active, we check the bounds
   * here, as an extra condition to add a label to the bucket.
   * Also, `visited_vertices` is updated here.
   */
  bool updateEfficientLabels(
      const Directions&       direction,
      const labelling::Label& candidate_label,
      const bool&             maintain_heap = true);

  /// Update `best_labels` entry by node with the candidate_label if
  /// appropriate
  void updateBestLabels(
      const Directions&       direction,
      const labelling::Label& candidate_label);

  /**
   * Iterate over the neighbours for the current label node and extends it
   * when appropriate.
   */
  void extendCurrentLabel(const Directions& direction);

  /// Helper function to extend along a given arc
  void extendSingleLabel(
      labelling::Label* label,
      const Directions& direction,
      const AdjVertex&  adj_vertex,
      bool resource_infeasible = false);

  /**
   * Saves/updates globally best label and check if it is Source-Sink so we
   * can use it in the bounding. Sets `final_label`
   */
  void saveCurrentBestLabel(const Directions& direction);

  /* Post processing */

  /**
   * Wrapper to process of output path.
   * Either saves appropriate label (single-direction search or early
   * termination)
   * or, calls joinLabels to merge forward + backward labels
   */
  void postProcessing();

  /**
   * get upper bound using a valid source-sink path (minimum of both forward
   * and backward source-sink paths)
   * @return double with upper bound
   */
  double getUB();

  /**
   * get minimum weight across all forward / backward labels
   *
   * @param[out] fwd_min, double, minimum across all forward labels
   * @param[out] bwd_min, double, minimum across all backward labels
   */
  void getMinimumWeights(double* fwd_min, double* bwd_min);

  /// return time since start time in seconds.

  double getElapsedTime() {
    std::chrono::duration<double> duration =
        (std::chrono::system_clock::now() - start_time_);
    return static_cast<double>(duration.count());
  }

  bool timeLimitExceeded() {
    if (params_ptr_->timeLimitExceeded()) {
      time_limit_reached_ = true;
      return true;
    }
    return false;
  }

  /**
   * The procedure "Join" or Algorithm 3 from Righini and Salani (2006).
   *
   * @see: https://www.sciencedirect.com/science/article/pii/S1572528606000417
   */
  void joinLabels();
};

} // namespace bidirectional

#endif // SRC_CC_BIDIRECTIONAL_H__
