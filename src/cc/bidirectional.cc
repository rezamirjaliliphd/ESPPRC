#include "src/cc/bidirectional.h"

#include <algorithm> // sort, all_of, find
#include <limits>    // numeric_limits
#include <cmath>
#include <unordered_map>
#include <exception>
#include "src/cc/preprocessing.h" // lowerBoundWeight, getCriticalRes

#ifdef _OPENMP
#include <omp.h>
#endif

namespace bidirectional {

namespace {

class ExtensionTimer {
 public:
  explicit ExtensionTimer(Search::ExtensionStats* stats) : stats_(stats) {
    if (stats_) start_ = std::chrono::steady_clock::now();
  }
  ~ExtensionTimer() {
    if (stats_) stats_->total_seconds += std::chrono::duration<double>(
        std::chrono::steady_clock::now() - start_).count();
  }
 private:
  Search::ExtensionStats* stats_;
  std::chrono::steady_clock::time_point start_;
};

struct PathMask {
  std::uint64_t bits = 0;
  bool complete = true;
};

PathMask visitedPathMask(const labelling::Label& label) {
  PathMask mask;
  for (const int node : label.partial_path) {
    if (node < 0 || node >= 64) {
      mask.complete = false;
      break;
    }
    mask.bits |= std::uint64_t{1} << node;
  }
  return mask;
}

struct JoinEntry {
  const labelling::Label* label;
  PathMask mask;
  size_t group = 0;
};

struct JoinBlock {
  size_t begin, end;
  double min_weight;
  std::uint64_t common_nodes;
  std::uint64_t groups = 0;
};

struct JoinGroup {
  double min_weight = std::numeric_limits<double>::infinity();
  std::uint64_t common_nodes = ~std::uint64_t{0};
  std::vector<double> min_resources, max_resources;

  void add(const JoinEntry& entry, const bool resources) {
    min_weight = std::min(min_weight, entry.label->weight);
    common_nodes &= entry.mask.complete ? entry.mask.bits : 0;
    if (!resources) return;
    const auto& values = entry.label->resource_consumption;
    if (min_resources.empty()) {
      min_resources.assign(values.size(), std::numeric_limits<double>::infinity());
      max_resources.assign(values.size(), -std::numeric_limits<double>::infinity());
    }
    for (size_t r = 0; r < values.size(); ++r) {
      // A NaN cannot prove that every label violates a resource bound.
      if (std::isnan(values[r])) {
        min_resources[r] = -std::numeric_limits<double>::infinity();
        max_resources[r] = std::numeric_limits<double>::infinity();
      } else {
        min_resources[r] = std::min(min_resources[r], values[r]);
        max_resources[r] = std::max(max_resources[r], values[r]);
      }
    }
  }

  bool resourceInfeasible(const labelling::Label& fwd,
                          const bidirectional::AdjVertex& adjacent,
                          const std::vector<double>& maximum,
                          const std::vector<double>& minimum,
                          const int critical) const {
    for (size_t r = 0; r < min_resources.size(); ++r) {
      const double bmin = static_cast<int>(r) == critical
          ? maximum[r] - max_resources[r] : min_resources[r];
      const double bmax = static_cast<int>(r) == critical
          ? maximum[r] - min_resources[r] : max_resources[r];
      // Match mergeLabels exactly: backward + (forward + edge). Reassociation
      // can change feasibility and floor(SRI) at floating-point boundaries.
      const double prefix = fwd.resource_consumption[r] + adjacent.resource_consumption[r];
      const double lower = bmin + prefix;
      const double upper = bmax + prefix;
      if ((std::isfinite(lower) && lower > maximum[r]) ||
          (std::isfinite(upper) && upper < minimum[r])) return true;
    }
    return false;
  }
};

struct JoinBucket {
  std::vector<JoinEntry> entries;
  std::vector<JoinBlock> blocks;
  std::vector<JoinGroup> groups;
  std::uint64_t common_nodes = ~std::uint64_t{0};
};

// Workers read frozen frontiers and prepare candidates. Only the coordinator
// changes the incumbent, duplicate-path history and collected output.
struct PreparedJoinPair {
  size_t index;
  std::unique_ptr<labelling::Label> label;
  std::vector<int> shortcut_path;
  double phi = 0., base = 0., extra = 0.;
  bool feasible = false;
};

struct PreparedJoinArc {
  int head;
  double weight;
  std::vector<PreparedJoinPair> pairs;
};

struct PreparedForwardJoin {
  std::vector<PreparedJoinArc> arcs;
  bool overflow = false, expired = false;
  std::exception_ptr error;
  std::uint64_t pairs = 0, records = 0, groups = 0, groups_skipped = 0, blocks_skipped = 0;
};

class JoinTimer {
 public:
  explicit JoinTimer(double* seconds) : seconds_(seconds) {
    if (seconds_) start_ = std::chrono::steady_clock::now();
  }
  ~JoinTimer() {
    if (seconds_) *seconds_ += std::chrono::duration<double>(
        std::chrono::steady_clock::now() - start_).count();
  }
 private:
  double* seconds_;
  std::chrono::steady_clock::time_point start_;
};

struct PathHash {
  size_t operator()(const std::vector<int>& path) const {
    size_t hash = path.size();
    for (const int node : path) {
      hash ^= std::hash<int>{}(node) + 0x9e3779b9U + (hash << 6) + (hash >> 2);
    }
    return hash;
  }
};

int chooseThreadCount(const int requested_threads, const std::size_t work_items,
                      const std::size_t min_items_per_thread) {
  if (requested_threads <= 1 || work_items < min_items_per_thread * 2) {
    return 1;
  }
  const int useful_threads =
      static_cast<int>(work_items / min_items_per_thread);
  return std::max(1, std::min(requested_threads, useful_threads));
}

} // namespace

/* Public methods */

BiDirectional::BiDirectional(
    const int&                 number_vertices,
    const int&                 number_edges,
    const int&                 source_id,
    const int&                 sink_id,
    const std::vector<double>& max_res_in,
    const std::vector<double>& min_res_in,
    const std::vector<double>& CG_cut_pi_in)
    : max_res(max_res_in),
      min_res(min_res_in),
      CG_cut_pi(CG_cut_pi_in),
      // Private pointer initialisations
      params_ptr_(std::make_unique<bidirectional::Params>()),
      graph_ptr_(std::make_unique<DiGraph>(
          number_vertices,
          number_edges,
          source_id,
          sink_id)),
      fwd_search_ptr_(std::make_unique<bidirectional::Search>(FWD)),
      bwd_search_ptr_(std::make_unique<bidirectional::Search>(BWD)) {
#if SPDLOG_ACTIVE_LEVEL == SPDLOG_LEVEL_DEBUG
  // Needed as not printed otherwise
  spdlog::set_level(spdlog::level::off);
#endif
  // spdlog::default_logger()->set_pattern("%v");
  // SPDLOG_INFO(
  //     "************************************************************************"
  //     "********");
  // spdlog::set_pattern("%+"); // back to default format
}

std::vector<int> BiDirectional::getPath() const {
  return best_label_->partial_path;
}

std::vector<double> BiDirectional::getConsumedResources() const {
  return best_label_->resource_consumption;
}

double BiDirectional::getTotalCost() const {
  return best_label_->weight;
}

void BiDirectional::checkCriticalRes() const {
  const std::vector<double>& res      = best_label_->resource_consumption;
  double                     min_diff = std::numeric_limits<double>::infinity();
  int                        min_r    = 0;
  for (int r = 0; r < res.size(); r++) {
    const double& diff = max_res[r] - res[r];
    if (diff < min_diff) {
      min_diff = diff;
      min_r    = r;
    }
  }
  if (min_r != params_ptr_->critical_res){}
    // SPDLOG_WARN("Critical resource {} does not match final tighest {}",
        // params_ptr_->critical_res,
        // min_r);
}

void BiDirectional::run() {
  start_time_ = std::chrono::system_clock::now();
  params_ptr_->setStartTime(start_time_);
  init();

  // SPDLOG_INFO("\t Time (s) \t | \t Solution");
  while (fwd_search_ptr_->stop == false || bwd_search_ptr_->stop == false) {
    if (timeLimitExceeded()) {
      break;
    }
    const Directions& direction = getDirection();
    if (direction != NODIR) {
      move(direction);
    } else {
      break;
    }
    if (terminate(direction)) {
      break;
    }
  }
  clearExtensionBatches();
  postProcessing();
}

/* Private methods */

/* Preprocessing */

void BiDirectional::runPreprocessing() {
  if (params_ptr_->direction == BOTH && params_ptr_->find_critical_res) {
    const int c = getCriticalRes(max_res, *graph_ptr_);
    // SPDLOG_INFO("Set critical resource to index {}", c);
    setCriticalRes(c);
  }
  // No need to use elementary if no negative cost cycle is found, all
  // reasources have positive values, a callback is not registered and minimum
  // resources are not present.
  detectNegativeCostCycle(graph_ptr_.get());
  if (graph_ptr_->negative_cost_cycle_present == FALSE &&
      graph_ptr_->all_resources_positive &&
      params_ptr_->ref_callback == nullptr &&
      std::all_of(
          min_res.cbegin(), min_res.cend(), [](bool v) { return v == 0; })) {
    if (params_ptr_->elementary) {
      // SPDLOG_WARN(
      //     "No negative cost cycle has been found and elementary set to true.\n"
      //     "Consider setting elementary to false.");
      // setElementary(false);
    }
  }

  if (params_ptr_->bounds_pruning) {
    // SPDLOG_INFO("Setting lower bounds.");
    if (params_ptr_->direction == BOTH || params_ptr_->direction == FWD) {
      lowerBoundWeight(
          fwd_search_ptr_->lower_bound_weight.get(), *graph_ptr_, true);
    }
    if (params_ptr_->direction == BOTH || params_ptr_->direction == BWD) {
      lowerBoundWeight(
          bwd_search_ptr_->lower_bound_weight.get(), *graph_ptr_, false);
    }
  }
}

void BiDirectional::init() {
  clearExtensionBatches();
  joining_stats_ = {};
  // Initialise labels
  labelling::Label label;
  merged_labels_.clear();
  paths.clear();
  primal_st_bound_ = std::numeric_limits<double>::infinity();
  best_label_ = std::make_shared<labelling::Label>(label);
  // Initialise resource bounds
  initResourceBounds();
  // Init individual searches
  initContainers();
  if (params_ptr_->direction == BOTH || params_ptr_->direction == FWD) {
    initSearch(FWD);
  }
  if (params_ptr_->direction == BOTH || params_ptr_->direction == BWD) {
    initSearch(BWD);
  }
  // run preprocessing
  runPreprocessing();
  // Preprocessing may change critical_res. Configure buckets afterwards and
  // keep the original full resource bound, not the moving halfway point.
  for (const auto direction : {FWD, BWD}) {
    if (params_ptr_->direction == BOTH || params_ptr_->direction == direction) {
      getSearchPtr(direction)->initializeDurationBuckets(
          params_ptr_->critical_res, max_res[params_ptr_->critical_res],
          params_ptr_->bucket_width, params_ptr_->adaptive_buckets,
          params_ptr_->bucket_dominance);
    }
  }
  // Init labels
  if (params_ptr_->direction == BOTH || params_ptr_->direction == FWD) {
    initLabels(FWD);
  }
  if (params_ptr_->direction == BOTH || params_ptr_->direction == BWD) {
    initLabels(BWD);
  }
}

void BiDirectional::initSearch(const Directions& direction) {
  Search* search_ptr = getSearchPtr(direction);
  // Allocate memory
  search_ptr->lower_bound_weight->resize(graph_ptr_->number_vertices, 0.0);
  search_ptr->efficient_labels.resize(graph_ptr_->number_vertices);
  search_ptr->best_labels.resize(graph_ptr_->number_vertices, nullptr);
}

void BiDirectional::initResourceBounds() {
  max_res_curr_ = max_res;
  min_res_curr_ = min_res;
}

void BiDirectional::initLabels(const Directions& direction) {
  Vertex              vertex;
  std::vector<double> res(min_res.size(), 0.0);
  std::vector<int>    path;
  Search*             search_ptr       = getSearchPtr(direction);
  const int           size_unreachable = graph_ptr_->number_vertices + 1;

  if (direction == FWD) {
    vertex = graph_ptr_->source;
  } else { // backward
    // set monotone resource to upper bound
    res[params_ptr_->critical_res] = max_res_curr_[params_ptr_->critical_res];
    vertex                         = graph_ptr_->sink;
  }
  // Current label init
  path = {vertex.user_id};
  labelling::Label lab(0.0, vertex, res, path, params_ptr_.get());
  search_ptr->replaceCurrentLabel(lab);
  // Final label dummy init
  Vertex dum_vertex = {-1, -1};
  res               = {};
  path              = {};
  labelling::Label lab2(0.0, dum_vertex, res, path, params_ptr_.get());

  search_ptr->replaceIntermediateLabel(lab2);
  search_ptr->pushHeap();
  // Add to efficient and best labels
  search_ptr->pushEfficientLabel(vertex.lemon_id, *search_ptr->current_label);
  search_ptr->replaceBestLabel(vertex.lemon_id, *search_ptr->current_label);
  search_ptr->addVisitedVertex(vertex.lemon_id);
}

void BiDirectional::initContainers() {
  if (params_ptr_->direction != BOTH) {
    Search* search_ptr = getSearchPtr(params_ptr_->direction);
    search_ptr->makeHeap();
  } else {
    fwd_search_ptr_->makeHeap();
    bwd_search_ptr_->makeHeap();
  }
}

/* Search */

Directions BiDirectional::getDirection() const {
  if (params_ptr_->direction == BOTH) {
    if (!fwd_search_ptr_->stop && bwd_search_ptr_->stop) {
      return FWD;
    } else if (fwd_search_ptr_->stop && !bwd_search_ptr_->stop) {
      return BWD;
    } else if (!fwd_search_ptr_->stop && !bwd_search_ptr_->stop) {
      // TODO: fix random
      // if (method == "random") {
      //   // return a random direction
      //   const std::vector<std::string> directions = {forward,
      //   backward}; const int                      r          =
      //   std::rand() % 2; const std::string&             direction  =
      //   directions[r]; return direction;
      // } else
      if (params_ptr_->method == "generated") {
        // return direction with least number of generated labels
        if (fwd_search_ptr_->generated_count <
            bwd_search_ptr_->generated_count) {
          return FWD;
        }
        return BWD;
      } else if (params_ptr_->method == "processed") {
        // return direction with least number of processed labels
        if (fwd_search_ptr_->processed_count <
            bwd_search_ptr_->processed_count) {
          return FWD;
        }
        return BWD;
      } else if (params_ptr_->method == "unprocessed") {
        // return direction with least number of unprocessed labels
        if (fwd_search_ptr_->unprocessed_count <
            bwd_search_ptr_->unprocessed_count) {
          return FWD;
        }
        return BWD;
      }
    } else {
      ;
    }
  } else {
    // Single direction
    if (params_ptr_->direction == FWD && fwd_search_ptr_->stop) {
      ;
    } else if (params_ptr_->direction == BWD && bwd_search_ptr_->stop) {
      ;
    } else {
      return params_ptr_->direction;
    }
  }
  return NODIR;
}

void BiDirectional::move(const Directions& direction) {
  Search*     search_ptr      = getSearchPtr(direction);
  const bool& bounds_exceeded = checkBounds(direction);
  if (!bounds_exceeded) {
    extendCurrentLabel(direction);
    saveCurrentBestLabel(direction);
  } else {
    search_ptr->stop = true;
  }
  updateHalfWayPoints(direction);
  updateCurrentLabel(direction);
  ++search_ptr->processed_count;
  ++iteration_;
}

bool BiDirectional::terminate(const Directions& direction) {
  Search* search_ptr = getSearchPtr(direction);
  return terminate(direction, *search_ptr->intermediate_label);
}

bool BiDirectional::terminate(
    const Directions&       direction,
    const labelling::Label& label) {
  // Check time elapsed (if relevant)
  if (timeLimitExceeded()) {
    return true;
  }
  return checkValidLabel(direction, label);
}

void BiDirectional::updateCurrentLabel(const Directions& direction) {
  Search* search_ptr = getSearchPtr(direction);
  if (search_ptr->unprocessed_labels->size() > 0) {
    // Get next label and removes current_label from heap
    const labelling::Label& new_label = labelling::getNextLabel(
        search_ptr->unprocessed_labels.get(), direction);
    // swap current label with new label
    search_ptr->replaceCurrentLabel(new_label);
    // Update unprocessed label counter
    search_ptr->unprocessed_count = search_ptr->unprocessed_labels->size();
    // SPDLOG_DEBUG("{} left in {}", search_ptr->unprocessed_count, direction);
  } else {
    search_ptr->stop = true;
  }
}

/* Checks */
bool BiDirectional::checkValidLabel(
    const Directions&       direction,
    const labelling::Label& label) {
  if (label.vertex.lemon_id != -1 &&
      label.checkStPath(graph_ptr_->source.user_id, graph_ptr_->sink.user_id)) {
    if (!std::isnan(params_ptr_->threshold) &&
        label.checkThreshold(params_ptr_->threshold)) {
      terminated_early_w_st_path_           = true;
      terminated_early_w_st_path_direction_ = direction;
      return true;
    }
  }
  return false;
}

bool BiDirectional::checkBounds(const Directions& direction) {
  // Check resource bounds
  Search*    search_ptr = getSearchPtr(direction);
  const int& c_res      = params_ptr_->critical_res;

  if ((direction == FWD &&
       search_ptr->current_label->resource_consumption[c_res] <=
           max_res_curr_[c_res]) ||
      (direction == BWD &&
       search_ptr->current_label->resource_consumption[c_res] >
           min_res_curr_[c_res]) ||
      max_res_curr_[c_res] != min_res_curr_[c_res]) {
    return false;
  }
  // only stop if search is being performed in both directions
  else if (params_ptr_->direction == BOTH) {
    return true;
  }
  return false;
}

bool BiDirectional::checkPrimalBound(
    const Directions&       direction,
    const labelling::Label& candidate_label) {
  Search* search_ptr = getSearchPtr(direction);
  const std::unique_ptr<std::vector<double>>& lower_bound_weight =
      search_ptr->lower_bound_weight;
  if (!params_ptr_->bounds_pruning) {
    return false;
  }
  if (!std::isnan(primal_st_bound_) &&
      candidate_label.weight +
              (*lower_bound_weight)[candidate_label.vertex.lemon_id] >
          primal_st_bound_) {
    return true;
  }
  return false;
}

bool BiDirectional::checkVertexVisited(
    const Directions& direction,
    const int&        vertex_idx) {
  Search* search_ptr = getSearchPtr(direction);
  return (
      search_ptr->visited_vertices.find(vertex_idx) !=
      search_ptr->visited_vertices.end());
}

void BiDirectional::updateHalfWayPoints(const Directions& direction) {
  Search*    search_ptr = getSearchPtr(direction);
  const int& c_res      = params_ptr_->critical_res;
  if (direction == FWD) {
    min_res_curr_[c_res] = std::max(
        min_res_curr_[c_res],
        std::min(
            search_ptr->current_label->resource_consumption[c_res],
            max_res_curr_[c_res]));
  } else {
    max_res_curr_[c_res] = std::min(
        max_res_curr_[c_res],
        std::max(
            search_ptr->current_label->resource_consumption[c_res],
            min_res_curr_[c_res]));
  }
}

void BiDirectional::extendCurrentLabel(const Directions& direction) {
  Search*                            search_ptr    = getSearchPtr(direction);
  std::shared_ptr<labelling::Label>& current_label = search_ptr->current_label;
  auto* stats = params_ptr_->profile_extension ? &search_ptr->extension_stats : nullptr;
  ExtensionTimer timer(stats);
  std::chrono::steady_clock::time_point preparation_start;
  if (stats) {
    ++stats->labels_processed;
    preparation_start = std::chrono::steady_clock::now();
  }
  const bool cached_extension = params_ptr_->bucket_extension &&
                                params_ptr_->ref_callback == nullptr;
  std::vector<AdjVertex> uncached_adj_vertices;
  std::vector<AdjVertex>* adj_vertices_ptr = &uncached_adj_vertices;
  bool adjacency_ready = false;
  if (cached_extension) {
    auto& adjacency = search_ptr->extension_adjacency[current_label->vertex.lemon_id];
    adj_vertices_ptr = &adjacency.arcs;
    adjacency_ready = adjacency.ready;
  }
  auto& adj_vertices = *adj_vertices_ptr;

  if (!adjacency_ready) {
    adj_vertices.clear();
    if (direction == FWD) {
      for (LemonGraph::OutArcIt a(
               *graph_ptr_->lemon_graph_ptr,
               graph_ptr_->getLNodeFromId(current_label->vertex.lemon_id));
           a != lemon::INVALID;
           ++a) {
        if (timeLimitExceeded()) {
          search_ptr->stop = true;
          return;
        }
        adj_vertices.push_back(graph_ptr_->getAdjVertex(a, true));
      }
    } else {
      for (LemonGraph::InArcIt a(
               *graph_ptr_->lemon_graph_ptr,
               graph_ptr_->getLNodeFromId(current_label->vertex.lemon_id));
           a != lemon::INVALID;
           ++a) {
        if (timeLimitExceeded()) {
          search_ptr->stop = true;
          return;
        }
        adj_vertices.push_back(graph_ptr_->getAdjVertex(a, false));
      }
    }
    if (cached_extension) {
      search_ptr->extension_adjacency[current_label->vertex.lemon_id].ready = true;
    }
  }

  const std::vector<unsigned char>* blocked = nullptr;
  if (cached_extension) {
    blocked = &search_ptr->bucketExtensionMask(
        *current_label, adj_vertices, max_res_curr_, min_res_curr_);
  }
  if (stats) stats->generation_seconds += std::chrono::duration<double>(
      std::chrono::steady_clock::now() - preparation_start).count();
#ifdef _OPENMP
  if (params_ptr_->enable_parallel && params_ptr_->num_threads > 1 &&
      params_ptr_->ref_callback == nullptr &&
      extendFromBatch(direction, adj_vertices, blocked)) return;
#endif
  for (size_t i = 0; i < adj_vertices.size(); ++i) {
    if (timeLimitExceeded()) { search_ptr->stop = true; return; }
    extendSingleLabel(current_label.get(), direction, adj_vertices[i],
                      blocked && (*blocked)[i]);
  }
}

void BiDirectional::clearExtensionBatches() {
  for (const auto direction : {FWD, BWD}) {
    auto& cache = extension_prefetch_[direction == FWD ? 0 : 1];
    if (params_ptr_->profile_extension)
      getSearchPtr(direction)->extension_stats.discarded_labels += cache.pending.size();
    cache.pending.clear();
    cache.misses = 0;
  }
}

const std::vector<AdjVertex>& BiDirectional::cachedExtensionArcs(
    const Directions& direction, const int node) {
  auto& adjacency = getSearchPtr(direction)->extension_adjacency[node];
  if (!adjacency.ready) {
    adjacency.arcs.clear();
    if (direction == FWD) {
      for (LemonGraph::OutArcIt a(*graph_ptr_->lemon_graph_ptr, graph_ptr_->getLNodeFromId(node));
           a != lemon::INVALID; ++a)
        adjacency.arcs.push_back(graph_ptr_->getAdjVertex(a, true));
    } else {
      for (LemonGraph::InArcIt a(*graph_ptr_->lemon_graph_ptr, graph_ptr_->getLNodeFromId(node));
           a != lemon::INVALID; ++a)
        adjacency.arcs.push_back(graph_ptr_->getAdjVertex(a, false));
    }
    adjacency.ready = true;
  }
  return adjacency.arcs;
}

bool BiDirectional::extendFromBatch(
    const Directions& direction, const std::vector<AdjVertex>& arcs,
    const std::vector<unsigned char>* blocked) {
#ifndef _OPENMP
  return false;
#else
  Search* search = getSearchPtr(direction);
  auto& cache = extension_prefetch_[direction == FWD ? 0 : 1];
  auto* stats = params_ptr_->profile_extension ? &search->extension_stats : nullptr;
  auto& parent = *search->current_label;
  auto found = cache.pending.find(parent.extension_id);
  if (found == cache.pending.end()) {
    // Reuse prepared work across moves. Do not replace a useful batch after
    // every newly inserted child changes the next heap selection.
    if (!cache.pending.empty() && ++cache.misses < 32) return false;
    constexpr size_t batch_size = 256;
    constexpr size_t max_arc_slots = 16384;
    const auto& heap = *search->unprocessed_labels;
    const size_t count = std::min(batch_size, heap.size() + 1);
    // Small-degree extensions cost less than copying/scheduling speculative
    // batches. Retain the optimized serial fallback for those nodes.
    if (count < 32 || arcs.size() < 32 || arcs.size() > max_arc_slots / 2)
      return false;
    const int requested_threads = chooseThreadCount(params_ptr_->num_threads, count, 8);
    if (requested_threads <= 1) return false;
    if (stats) stats->discarded_labels += cache.pending.size();
    cache.pending.clear();
    cache.misses = 0;

    struct Job {
      labelling::Label parent;
      const std::vector<AdjVertex>* arcs;
      PreparedExtension result;
    };
    std::vector<Job> jobs;
    jobs.reserve(count);
    jobs.push_back({parent, &arcs, {}});
    size_t arc_slots = arcs.size();
    // Predict the next pops using a sparse index overlay. This preserves the
    // heap's tie behavior without copying or mutating its Label array. Newly
    // inserted children can still change actual selection; predictions are
    // used only to choose speculative work.
    const int critical = params_ptr_->critical_res;
    const auto lower_priority = [&](const size_t a, const size_t b) {
      const double x = heap[a].resource_consumption[critical];
      const double y = heap[b].resource_consumption[critical];
      return direction == FWD ? x > y : x < y;
    };
    std::unordered_map<size_t, size_t> overlay;
    overlay.reserve(count * 8);
    const auto read = [&](size_t position) {
      const auto entry = overlay.find(position);
      return entry == overlay.end() ? position : entry->second;
    };
    size_t remaining = heap.size();
    while (jobs.size() < count && remaining > 0) {
      const auto& label = heap[read(0)];
      const auto& adjacent = cachedExtensionArcs(direction, label.vertex.lemon_id);
      if (adjacent.size() > max_arc_slots - arc_slots) break;
      jobs.push_back({label, &adjacent, {}});
      arc_slots += adjacent.size();
      const size_t replacement = read(--remaining);
      if (remaining == 0) break;
      size_t hole = 0;
      while (2 * hole + 1 < remaining) {
        size_t child = 2 * hole + 1;
        if (child + 1 < remaining && !lower_priority(read(child + 1), read(child))) ++child;
        overlay[hole] = read(child);
        hole = child;
      }
      while (hole > 0) {
        const size_t above = (hole - 1) / 2;
        if (!lower_priority(read(above), replacement)) break;
        overlay[hole] = read(above);
        hole = above;
      }
      overlay[hole] = replacement;
    }
    const int threads = chooseThreadCount(requested_threads, jobs.size(), 8);
    if (threads <= 1) return false;
    const auto preparation_start = std::chrono::steady_clock::now();
    // Workers touch private parent copies only. The current resource box is
    // frozen while they run and contains every later moving-halfway box.
    // Failed preparations remain infeasible; successes are rechecked at use.
    #pragma omp parallel for num_threads(threads) schedule(static)
    for (int j = 0; j < static_cast<int>(jobs.size()); ++j) {
      auto& job = jobs[j];
      try {
        auto& result = job.result;
        result.children.resize(job.arcs->size());
        for (size_t i = 0; i < job.arcs->size(); ++i) {
          if (params_ptr_->timeLimitExceeded()) { result.expired = true; break; }
          const auto& edge = (*job.arcs)[i];
          if ((params_ptr_->elementary && job.parent.isNodeUnreachable(edge.vertex.user_id)) ||
              (job.parent.partial_path.size() > 1 &&
               !job.parent.checkPathExtension(edge.vertex.user_id))) continue;
          ++result.generated;
          // Reject resource failures before allocating a path/Label. This is
          // the additive REF and soft feasibility test used by Label::extend;
          // callbacks never enter this branch.
          bool infeasible = false;
          for (size_t r = 0; r < job.parent.resource_consumption.size(); ++r) {
            const double increment = edge.resource_consumption[r];
            const double value = direction == BWD && static_cast<int>(r) == critical
                ? job.parent.resource_consumption[r] - (increment > 0. ? increment : 1.)
                : job.parent.resource_consumption[r] + increment;
            if (!(value <= max_res_curr_[r]) ||
                ((static_cast<int>(r) == critical || min_res_curr_[r] <= 0.) &&
                 !(value >= min_res_curr_[r]))) { infeasible = true; break; }
          }
          if (infeasible) {
            if (params_ptr_->elementary) job.parent.markNodeUnreachable(edge.vertex.user_id);
            continue;
          }
          auto child = std::unique_ptr<labelling::Label>(new labelling::Label(
              job.parent.extend(edge, direction, max_res_curr_, min_res_curr_)));
          if (child->vertex.lemon_id != -1) result.children[i] = std::move(child);
        }
      } catch (...) { job.result.error = std::current_exception(); }
    }
    if (stats) {
      ++stats->parallel_batches;
      stats->prepared_labels += jobs.size();
      stats->generation_seconds += std::chrono::duration<double>(
          std::chrono::steady_clock::now() - preparation_start).count();
    }
    for (auto& job : jobs) {
      if (stats) stats->prepared_extensions += job.result.generated;
      cache.pending.emplace(job.parent.extension_id, std::move(job.result));
    }
    found = cache.pending.find(parent.extension_id);
  }
  cache.misses = 0;
  auto prepared = std::move(found->second);
  cache.pending.erase(found);
  if (prepared.error) std::rethrow_exception(prepared.error);
  if (prepared.expired || timeLimitExceeded()) { search->stop = true; return true; }
  if (stats) ++stats->parallel_labels;
  // Replay in original arc order. Newly infeasible arcs must change the REAL
  // parent's unreachable set before a repeated destination is considered.
  for (size_t i = 0; i < arcs.size(); ++i) {
    if (timeLimitExceeded()) { search->stop = true; return true; }
    const auto& edge = arcs[i];
    const auto start = stats ? std::chrono::steady_clock::now()
                            : std::chrono::steady_clock::time_point{};
    if (stats) ++stats->arcs_considered;
    if ((!params_ptr_->elementary || !parent.isNodeUnreachable(edge.vertex.user_id)) &&
        (parent.partial_path.size() <= 1 || parent.checkPathExtension(edge.vertex.user_id))) {
      const bool resource_blocked = blocked && (*blocked)[i];
      if (stats) {
        ++stats->eligible_extensions;
        if (resource_blocked) ++stats->bucket_rejections;
        else ++stats->full_extensions;
      }
      const auto& child = prepared.children[i];
      const bool feasible = !resource_blocked && child &&
          child->checkFeasibility(max_res_curr_, min_res_curr_, true);
      if (!feasible) {
        if (params_ptr_->elementary) parent.markNodeUnreachable(edge.vertex.user_id);
        if (stats) ++stats->infeasible_extensions;
      }
      if (stats) stats->generation_seconds += std::chrono::duration<double>(
          std::chrono::steady_clock::now() - start).count();
      if (feasible) updateEfficientLabels(direction, *child);
    } else if (stats) stats->generation_seconds += std::chrono::duration<double>(
        std::chrono::steady_clock::now() - start).count();
  }
  return true;
#endif
}

void BiDirectional::extendSingleLabel(
    labelling::Label* label,
    const Directions& direction,
    const AdjVertex&  adj_vertex,
    bool resource_infeasible) {
  auto* stats = params_ptr_->profile_extension ? &getSearchPtr(direction)->extension_stats : nullptr;
  std::chrono::steady_clock::time_point start;
  if (stats) { ++stats->arcs_considered; start = std::chrono::steady_clock::now(); }
  if ( // Always extend when non-elementary
      !params_ptr_->elementary ||
      // When elementary, check if vertex already seen / unreachable and if the
      // next node is suitable (2-cycles are not allowed!)
      (params_ptr_->elementary &&
       !label->isNodeUnreachable(adj_vertex.vertex.user_id))) {
    if (label->partial_path.size() <= 1 ||
        (label->partial_path.size() > 1 &&
         label->checkPathExtension(adj_vertex.vertex.user_id))) {
      if (stats) {
        ++stats->eligible_extensions;
        if (resource_infeasible) ++stats->bucket_rejections;
        else ++stats->full_extensions;
      }
      // The resource shortcut has the same unreachable-node side effect as
      // normal failure, AFTER the original structural checks for this arc.
      labelling::Label new_label = label->extend(
          adj_vertex, direction, max_res_curr_, min_res_curr_, resource_infeasible);
      if (stats) {
        if (new_label.vertex.lemon_id == -1) ++stats->infeasible_extensions;
        stats->generation_seconds += std::chrono::duration<double>(
            std::chrono::steady_clock::now() - start).count();
      }

      // If label non-empty, (only when the extension is resource-feasible)
      if (new_label.vertex.lemon_id != -1) {
        // SPDLOG_DEBUG("\t Found new label: {}", new_label.getString());
        updateEfficientLabels(direction, new_label);
      } else {
        // SPDLOG_DEBUG("\t Extension infeasible");
      }
      return;
    }
  }
  if (stats) stats->generation_seconds += std::chrono::duration<double>(
      std::chrono::steady_clock::now() - start).count();
}

bool BiDirectional::updateEfficientLabels(
    const Directions&       direction,
    const labelling::Label& candidate_label,
    const bool&             maintain_heap) {
  Search* search_ptr = getSearchPtr(direction);
  // const ref vertex index
  const int& lemon_id = candidate_label.vertex.lemon_id;
  // ref efficient_labels_ for a given vertex
  std::vector<labelling::Label>& efficient_labels_vertex =
      search_ptr->efficient_labels[lemon_id];

  if (!search_ptr->containsEfficientLabel(lemon_id, candidate_label)) {
    ++search_ptr->generated_count;
    // If there already exists labels for the given vertex
    if (efficient_labels_vertex.size() > 1) {
      // check if new_label is dominated by any other comparable label
      const bool dominated = search_ptr->runDominance(
          lemon_id, candidate_label, params_ptr_->elementary);
      if (!dominated && !checkPrimalBound(direction, candidate_label)) {
        // add candidate_label to efficient_labels and unprocessed heap
        search_ptr->pushEfficientLabel(lemon_id, candidate_label);
        if (maintain_heap) {
          search_ptr->pushUnprocessedLabel(candidate_label);
        } else {
          search_ptr->appendUnprocessedLabel(candidate_label);
        }
        // SPDLOG_DEBUG("\t Added to the queue.");
      } else {
        // SPDLOG_DEBUG("\t Label dominated.");
        return false;
      }
    } else {
      // First label produced for the vertex
      // update both efficient and unprocessed labels
      search_ptr->pushEfficientLabel(lemon_id, candidate_label);
      if (maintain_heap) {
        search_ptr->pushUnprocessedLabel(candidate_label);
      } else {
        search_ptr->appendUnprocessedLabel(candidate_label);
      }
      // SPDLOG_DEBUG("\t Added to the queue (no other label at this vertex).");
    }
    updateBestLabels(direction, candidate_label);
    // Update vertices visited
    search_ptr->addVisitedVertex(lemon_id);
    return true;
  }
  return false;
}

void BiDirectional::updateBestLabels(
    const Directions&       direction,
    const labelling::Label& candidate_label) {
  // Only save full paths when they are global resource feasible
  Search*    search_ptr = getSearchPtr(direction);
  const int& lemon_id   = candidate_label.vertex.lemon_id;
  std::vector<std::shared_ptr<labelling::Label>>& best_labels =
      search_ptr->best_labels;

  bool stop = false;
  if (direction == FWD && lemon_id == graph_ptr_->sink.lemon_id &&
      !candidate_label.checkFeasibility(max_res, min_res)) {
    stop = true;
  } else if (
      direction == BWD && lemon_id == graph_ptr_->source.lemon_id &&
      !candidate_label.checkFeasibility(max_res, min_res)) {
    stop = true;
  }
  if (stop) {
    // SPDLOG_DEBUG("\t Label not globally feasible and not s-t path.");
    return;
  }
  // Update best_label only when new label has lower weight or first label
  if ((best_labels[lemon_id] &&
       candidate_label.weight < best_labels[lemon_id]->weight) ||
      !best_labels[lemon_id]) {
    search_ptr->replaceBestLabel(lemon_id, candidate_label);
    // SPDLOG_DEBUG(
    //     "\t Vertex improvement with {}.",
    //     search_ptr->best_labels[lemon_id]->getString());
  }
}

void BiDirectional::saveCurrentBestLabel(const Directions& direction) {
  Search* search_ptr = getSearchPtr(direction);

  std::shared_ptr<labelling::Label>& intermediate_label_ptr =
      search_ptr->intermediate_label;
  std::shared_ptr<labelling::Label>& current_label_ptr =
      search_ptr->current_label;

  if (intermediate_label_ptr->vertex.lemon_id == -1) {
    intermediate_label_ptr =
        std::make_shared<labelling::Label>(*current_label_ptr);
    return;
  }
  // Check for global feasibility
  if (!current_label_ptr->checkFeasibility(max_res, min_res)) {
    return;
  }
  bool improvement_found = false;
  if (intermediate_label_ptr->vertex.lemon_id ==
          current_label_ptr->vertex.lemon_id &&
      current_label_ptr->fullDominance(*intermediate_label_ptr, direction)) {
    // Save complete source-sink path
    search_ptr->replaceIntermediateLabel(*current_label_ptr);
    improvement_found = true;
  } else {
    // First source-sink path
    if ((direction == FWD &&
         (current_label_ptr->partial_path.back() == graph_ptr_->sink.user_id &&
          intermediate_label_ptr->vertex.user_id ==
              graph_ptr_->source.user_id)) ||
        (direction == BWD && (current_label_ptr->partial_path.back() ==
                                  graph_ptr_->source.user_id &&
                              intermediate_label_ptr->vertex.user_id ==
                                  graph_ptr_->sink.user_id))) {
      // Save complete source-sink path
      search_ptr->replaceIntermediateLabel(*current_label_ptr);
      improvement_found = true;
    }
  }

  if (improvement_found) {
    // Partial-label weights exclude SRI costs; only a complete feasible path
    // with all floor costs included can supply an incumbent upper bound.
    auto complete = direction == BWD
        ? labelling::processBwdLabel(*intermediate_label_ptr, max_res, min_res, true)
        : *intermediate_label_ptr;
    if (!complete.partial_path.empty() &&
        complete.checkStPath(graph_ptr_->source.user_id, graph_ptr_->sink.user_id) &&
        complete.checkFeasibility(max_res, min_res)) {
      double cost = complete.weight;
      const auto& R = complete.resource_consumption;
      for (size_t r = 0; r < std::min(R.size(), CG_cut_pi.size()); ++r) {
        cost += CG_cut_pi[r] * std::floor(R[r]);
      }
      primal_st_bound_ = std::min(primal_st_bound_, cost);
    }
    // SPDLOG_INFO(
    //     "\t {} \t | \t {}", getElapsedTime(), current_label_ptr->weight);
    // SPDLOG_DEBUG(
    //     "******* Global improvement {}.",
    //     search_ptr->intermediate_label->getString());
  }
}

/**
 * Post-processing methods
 */

void BiDirectional::postProcessing() {
  if (time_limit_reached_ && !terminated_early_w_st_path_) {
    best_label_ = std::make_shared<labelling::Label>();
    if ((params_ptr_->direction == BOTH || params_ptr_->direction == FWD) &&
        fwd_search_ptr_->intermediate_label->vertex.lemon_id != -1 &&
        !fwd_search_ptr_->intermediate_label->partial_path.empty() &&
        fwd_search_ptr_->intermediate_label->checkStPath(
            graph_ptr_->source.user_id, graph_ptr_->sink.user_id) &&
        fwd_search_ptr_->intermediate_label->checkFeasibility(max_res, min_res)) {
      best_label_ = fwd_search_ptr_->intermediate_label;
    }
    if ((params_ptr_->direction == BOTH || params_ptr_->direction == BWD) &&
        bwd_search_ptr_->intermediate_label->vertex.lemon_id != -1 &&
        !bwd_search_ptr_->intermediate_label->partial_path.empty()) {
      auto bwd_label = std::make_shared<labelling::Label>(
          labelling::processBwdLabel(
              *bwd_search_ptr_->intermediate_label, max_res, min_res, true));
      if (bwd_label->checkStPath(
              graph_ptr_->source.user_id, graph_ptr_->sink.user_id) &&
          bwd_label->checkFeasibility(max_res, min_res) &&
          (best_label_->vertex.lemon_id == -1 ||
           bwd_label->weight < best_label_->weight)) {
        best_label_ = bwd_label;
      }
    }
    return;
  }

  if (!terminated_early_w_st_path_) {
    if (params_ptr_->direction == BOTH) {
      // If bidirectional algorithm used and both directions traversed, run
      // path joining procedure.
      joinLabels();
    } else {
      // If FWD direction specified or backward direction not traversed
      if (params_ptr_->direction == FWD) {
        // Forward
        best_label_ = fwd_search_ptr_->intermediate_label;
      }
      // If backward direction specified or FWD direction not traversed
      else {
        // Backward
        best_label_ =
            std::make_shared<labelling::Label>(labelling::processBwdLabel(
                *bwd_search_ptr_->intermediate_label, max_res, min_res, true));
      }
    }
  } else {
    // final label contains the label that triggered the early termination
    if (terminated_early_w_st_path_direction_ == FWD) {
      best_label_ = fwd_search_ptr_->intermediate_label;
    } else {
      best_label_ =
          std::make_shared<labelling::Label>(labelling::processBwdLabel(
              *bwd_search_ptr_->intermediate_label, max_res, min_res, true));
    }
  }
  // 80 stars at the end
  // spdlog::default_logger()->set_pattern("%v");
  // SPDLOG_INFO(
  //     "************************************************************************"
  //     "********");
}

double BiDirectional::getUB() {
  double UB = std::numeric_limits<double>::infinity();
  // Extract forward and backward best labels (one's with least weight)
  const auto& fwd_best =
      fwd_search_ptr_->best_labels[graph_ptr_->sink.lemon_id];
  const auto& bwd_best =
      bwd_search_ptr_->best_labels[graph_ptr_->source.lemon_id];
  // Upper bound must be a resource-feasible s-t path
  if (fwd_best && fwd_best->checkFeasibility(max_res, min_res)) {
    UB = fwd_best->weight;
  }
  if (bwd_best && bwd_best->checkFeasibility(max_res, min_res)) {
    if (bwd_best->weight < UB) {
      UB = bwd_best->weight;
    }
  }
  return UB;
}

void BiDirectional::getMinimumWeights(double* fwd_min, double* bwd_min) {
  // Forward
  // init
  *fwd_min = std::numeric_limits<double>::infinity();
  for (const int& n : fwd_search_ptr_->visited_vertices) {
    if (n != graph_ptr_->source.lemon_id && fwd_search_ptr_->best_labels[n] &&
        fwd_search_ptr_->best_labels[n]->weight < *fwd_min) {
      *fwd_min = fwd_search_ptr_->best_labels[n]->weight;
    }
  }
  // backward
  *bwd_min = std::numeric_limits<double>::infinity();
  for (const int& n : bwd_search_ptr_->visited_vertices) {
    if (n != graph_ptr_->sink.lemon_id && bwd_search_ptr_->best_labels[n] &&
        bwd_search_ptr_->best_labels[n]->weight < *bwd_min) {
      *bwd_min = bwd_search_ptr_->best_labels[n]->weight;
    }
  }
}

void BiDirectional::joinLabels() {
  auto* stats = params_ptr_->profile_joining ? &joining_stats_ : nullptr;
  JoinTimer timer(stats ? &stats->seconds : nullptr);
  const bool bucket_joining = params_ptr_->bucket_joining;
  const bool resource_shortcuts = bucket_joining && !collect_paths_ &&
                                  params_ptr_->ref_callback == nullptr;
  const int c_res = params_ptr_->critical_res;
  // Tighten only with a feasible complete cost, including all SRI terms.
  double UB = std::numeric_limits<double>::infinity();
  const double HF = std::min(max_res_curr_[c_res], min_res_curr_[c_res]);
  double fwd_min, bwd_min;
  getMinimumWeights(&fwd_min, &bwd_min);

  // Index backward labels once. Blocks preserve the original visit order,
  // while allowing whole groups to fail the existing cost/overlap checks.
  constexpr size_t block_size = 32;
  std::vector<JoinBucket> buckets(graph_ptr_->number_vertices);
  for (const int m : bwd_search_ptr_->visited_vertices) {
    if (timeLimitExceeded()) return;
    auto& bucket = buckets[m];
    std::unordered_map<Search::DurationBucketIndex, size_t> group_ids;
    size_t scanned = 0;
    for (const auto& label : bwd_search_ptr_->efficient_labels[m]) {
      if (++scanned % 1024 == 0 && timeLimitExceeded()) return;
      if (label.resource_consumption[c_res] >= HF) {
        bucket.entries.push_back({&label, visitedPathMask(label)});
        if (bucket_joining) {
          auto& entry = bucket.entries.back();
          const auto inserted = group_ids.emplace(
              bwd_search_ptr_->durationBucketIndex(label), bucket.groups.size());
          if (inserted.second) bucket.groups.emplace_back();
          entry.group = inserted.first->second;
          bucket.groups[entry.group].add(entry, resource_shortcuts);
          bucket.common_nodes &= entry.mask.complete ? entry.mask.bits : 0;
        }
      }
    }
    for (size_t begin = 0; begin < bucket.entries.size(); begin += block_size) {
      if (timeLimitExceeded()) return;
      JoinBlock block{begin, std::min(begin + block_size, bucket.entries.size()),
                      std::numeric_limits<double>::infinity(), ~std::uint64_t{0}};
      for (size_t i = block.begin; i < block.end; ++i) {
        const auto& entry = bucket.entries[i];
        block.min_weight = std::min(block.min_weight, entry.label->weight);
        block.common_nodes &= entry.mask.complete ? entry.mask.bits : 0;
        if (bucket_joining && bucket.groups.size() <= 64) {
          block.groups |= std::uint64_t{1} << entry.group;
        }
      }
      bucket.blocks.push_back(block);
    }
  }

  // Same halfway rule as before, using exact path equality after hashing.
  // Only path keys and minimum phi are needed when collection is disabled.
  std::unordered_map<std::vector<int>, double, PathHash> minimum_phi;
  const auto record_phi = [&minimum_phi](const std::vector<int>& path, const double phi) {
    auto inserted = minimum_phi.emplace(path, phi);
    const bool halfway_ok = inserted.second || !(inserted.first->second < phi);
    if (!inserted.second && phi < inserted.first->second) inserted.first->second = phi;
    return halfway_ok;
  };
  // Reuse scratch storage on the coordinator only.
  std::vector<unsigned char> group_status;
  const auto serial_forward = [&](const labelling::Label& fwd_label, const int n) {
    if (timeLimitExceeded()) return false;
    if (!(fwd_label.resource_consumption[c_res] <= HF &&
          fwd_label.weight + bwd_min <= UB)) return true;
    // Use visited nodes, not unreachable_nodes: the latter also contains
    // nodes excluded by resource infeasibility and is not an overlap mask.
    const auto fwd_mask = visitedPathMask(fwd_label);
    for (LemonGraph::OutArcIt a(*graph_ptr_->lemon_graph_ptr,
                                graph_ptr_->getLNodeFromId(n));
         a != lemon::INVALID; ++a) {
      const int m = graph_ptr_->getId(graph_ptr_->head(a));
      const double edge_weight = graph_ptr_->getWeight(a);
      if (!checkVertexVisited(BWD, m) || m == graph_ptr_->source.lemon_id ||
          !(fwd_label.weight + edge_weight +
            bwd_search_ptr_->best_labels[m]->weight <= UB)) continue;
      const auto& bucket = buckets[m];
      if (bucket_joining && params_ptr_->elementary && fwd_mask.complete &&
          (fwd_mask.bits & bucket.common_nodes) != 0) {
        if (stats) stats->blocks_skipped += bucket.blocks.size();
        continue;
      }
      // Copy the edge resources once per bucket, instead of once per pair.
      const auto adjacent = graph_ptr_->getAdjVertex(a, true);
      std::uint64_t allowed_groups = 0;
      if (bucket_joining) {
        group_status.resize(bucket.groups.size());
        for (size_t g = 0; g < bucket.groups.size(); ++g) {
          if (stats) ++stats->group_checks;
          const auto& group = bucket.groups[g];
          // A strict comparison is conservative for non-finite arithmetic.
          if (fwd_label.weight + edge_weight + group.min_weight > UB ||
              (params_ptr_->elementary && fwd_mask.complete &&
               (fwd_mask.bits & group.common_nodes) != 0)) {
            group_status[g] = 0;
            if (stats) ++stats->groups_skipped;
            continue;
          }
          group_status[g] = 1;
          if (g < 64) allowed_groups |= std::uint64_t{1} << g;
          if (resource_shortcuts && group.resourceInfeasible(
                fwd_label, adjacent, max_res, min_res, c_res)) group_status[g] |= 2;
        }
      }
      for (const auto& block : bucket.blocks) {
        if (timeLimitExceeded()) return false;
        if (bucket_joining && bucket.groups.size() <= 64 &&
            (block.groups & allowed_groups) == 0) {
          if (stats) ++stats->blocks_skipped;
          continue;
        }
        if (!(fwd_label.weight + edge_weight + block.min_weight <= UB)) continue;
        if (params_ptr_->elementary && fwd_mask.complete &&
            (fwd_mask.bits & block.common_nodes) != 0) continue;
        for (size_t i = block.begin; i < block.end; ++i) {
          if (timeLimitExceeded()) return false;
          const auto& entry = bucket.entries[i];
          if (bucket_joining && group_status[entry.group] == 0) continue;
          if (stats) ++stats->label_pairs;
          const auto& bwd_label = *entry.label;
          if (!(fwd_label.weight + edge_weight + bwd_label.weight <= UB)) continue;
          if (params_ptr_->elementary && fwd_mask.complete && entry.mask.complete) {
            if ((fwd_mask.bits & entry.mask.bits) != 0) continue;
          } else if (!labelling::mergePreCheck(fwd_label, bwd_label, max_res)) {
            continue;
          }
          if (resource_shortcuts && (group_status[entry.group] & 2) != 0) {
            // Infeasible joins still participate in the original duplicate-
            // path/phi history. Preserve that update, avoiding Label/resource
            // construction only when no collected record is requested.
            std::vector<int> path;
            path.reserve(fwd_label.partial_path.size() + bwd_label.partial_path.size());
            path.insert(path.end(), fwd_label.partial_path.begin(), fwd_label.partial_path.end());
            path.insert(path.end(), bwd_label.partial_path.rbegin(), bwd_label.partial_path.rend());
            record_phi(path, labelling::getPhiValue(fwd_label, bwd_label, max_res));
            if (stats) ++stats->resource_shortcuts;
            continue;
          }
          if (stats) ++stats->full_merges;
          auto merged_label = labelling::mergeLabels(
              fwd_label, bwd_label, adjacent, graph_ptr_->sink, max_res, min_res);
          const double base_cost = fwd_label.weight + edge_weight + bwd_label.weight;
          double extra = 0;
          const auto& R = merged_label.resource_consumption;
          for (size_t r = 0; r < std::min(R.size(), CG_cut_pi.size()); ++r) {
            extra += CG_cut_pi[r] * std::floor(R[r]);
          }
          merged_label.weight += extra;

          const bool halfway_ok = record_phi(merged_label.partial_path, merged_label.phi);
          if (merged_label.vertex.lemon_id != -1 &&
              merged_label.checkFeasibility(max_res, min_res) && halfway_ok) {
            if (best_label_->vertex.lemon_id == -1 ||
                merged_label.fullDominance(*best_label_, FWD) ||
                merged_label.weight < best_label_->weight) {
              best_label_ = std::make_shared<labelling::Label>(merged_label);
              best_base_cost_ = base_cost;
              best_extra_cost_ = extra;
              UB = std::min(UB, best_label_->weight);
              if (terminate(FWD, *best_label_)) return false;
            }
          }
          if (collect_paths_) {
            paths.push_back(merged_label.partial_path);
            merged_labels_.push_back(std::move(merged_label));
          }
        }
      }
    }
    return true;
  };

#ifdef _OPENMP
  size_t widest_frontier = 0, estimated_pairs = 0;
  if (params_ptr_->parallel_joining && params_ptr_->joining_threads > 1 &&
      params_ptr_->ref_callback == nullptr) {
    for (const int n : fwd_search_ptr_->visited_vertices) {
      const size_t count = fwd_search_ptr_->efficient_labels[n].size();
      widest_frontier = std::max(widest_frontier, count);
      for (LemonGraph::OutArcIt a(*graph_ptr_->lemon_graph_ptr,
                                  graph_ptr_->getLNodeFromId(n));
           a != lemon::INVALID; ++a) {
        const auto& bucket = buckets[graph_ptr_->getId(graph_ptr_->head(a))];
        // Saturate: this is only a scheduling estimate, not a path bound.
        estimated_pairs += std::min(size_t{32768}, count) *
                           std::min(size_t{32768}, bucket.entries.size());
        estimated_pairs = std::min(size_t{32768}, estimated_pairs);
      }
    }
  }
  const int join_threads = estimated_pairs >= 32768
      ? chooseThreadCount(params_ptr_->joining_threads, widest_frontier, 4) : 1;
  if (join_threads > 1) {
    constexpr size_t forward_batch_size = 32;
    constexpr size_t scheduling_samples = 4;
    constexpr size_t min_pairs_per_label = 2048;
    constexpr size_t max_records_per_forward = 4096;
    const auto prepare_forward = [&](const labelling::Label& fwd, const int n,
                                     const double frozen_ub, const bool count_only = false) {
      PreparedForwardJoin result;
      if (!(fwd.resource_consumption[c_res] <= HF &&
            fwd.weight + bwd_min <= frozen_ub)) return result;
      const auto mask = visitedPathMask(fwd);
      std::vector<unsigned char> statuses;
      for (LemonGraph::OutArcIt a(*graph_ptr_->lemon_graph_ptr,
                                  graph_ptr_->getLNodeFromId(n));
           a != lemon::INVALID; ++a) {
        if (params_ptr_->timeLimitExceeded()) { result.expired = true; return result; }
        const int m = graph_ptr_->getId(graph_ptr_->head(a));
        const double weight = graph_ptr_->getWeight(a);
        if (!checkVertexVisited(BWD, m) || m == graph_ptr_->source.lemon_id ||
            !(fwd.weight + weight + bwd_search_ptr_->best_labels[m]->weight <= frozen_ub)) continue;
        const auto& bucket = buckets[m];
        if (bucket_joining && params_ptr_->elementary && mask.complete &&
            (mask.bits & bucket.common_nodes) != 0) {
          result.blocks_skipped += bucket.blocks.size();
          continue;
        }
        const auto adjacent = graph_ptr_->getAdjVertex(a, true);
        std::uint64_t allowed = 0;
        if (bucket_joining) {
          statuses.resize(bucket.groups.size());
          for (size_t g = 0; g < bucket.groups.size(); ++g) {
            ++result.groups;
            const auto& group = bucket.groups[g];
            if (fwd.weight + weight + group.min_weight > frozen_ub ||
                (params_ptr_->elementary && mask.complete &&
                 (mask.bits & group.common_nodes) != 0)) {
              statuses[g] = 0;
              ++result.groups_skipped;
              continue;
            }
            statuses[g] = 1;
            if (g < 64) allowed |= std::uint64_t{1} << g;
            if (resource_shortcuts && group.resourceInfeasible(
                  fwd, adjacent, max_res, min_res, c_res)) statuses[g] |= 2;
          }
        }
        PreparedJoinArc prepared{m, weight, {}};
        for (const auto& block : bucket.blocks) {
          if (params_ptr_->timeLimitExceeded()) { result.expired = true; return result; }
          if (bucket_joining && bucket.groups.size() <= 64 && (block.groups & allowed) == 0) {
            ++result.blocks_skipped;
            continue;
          }
          if (!(fwd.weight + weight + block.min_weight <= frozen_ub)) continue;
          if (params_ptr_->elementary && mask.complete && (mask.bits & block.common_nodes) != 0) continue;
          for (size_t i = block.begin; i < block.end; ++i) {
            const auto& entry = bucket.entries[i];
            if (bucket_joining && statuses[entry.group] == 0) continue;
            ++result.pairs;
            if (count_only) continue;
            const auto& bwd = *entry.label;
            if (!(fwd.weight + weight + bwd.weight <= frozen_ub)) continue;
            if (params_ptr_->elementary && mask.complete && entry.mask.complete) {
              if ((mask.bits & entry.mask.bits) != 0) continue;
            } else if (!labelling::mergePreCheck(fwd, bwd, max_res)) continue;
            // Bound temporary storage. An oversized task is replayed serially
            // from its original position, without publishing speculative state.
            if (result.records == max_records_per_forward) {
              result.overflow = true;
              result.arcs.clear();
              return result;
            }
            PreparedJoinPair pair;
            pair.index = i;
            pair.base = fwd.weight + weight + bwd.weight;
            if (resource_shortcuts && (statuses[entry.group] & 2) != 0) {
              pair.shortcut_path.reserve(fwd.partial_path.size() + bwd.partial_path.size());
              pair.shortcut_path.insert(pair.shortcut_path.end(), fwd.partial_path.begin(), fwd.partial_path.end());
              pair.shortcut_path.insert(pair.shortcut_path.end(), bwd.partial_path.rbegin(), bwd.partial_path.rend());
              pair.phi = labelling::getPhiValue(fwd, bwd, max_res);
            } else {
              // Direct initialization avoids an extra deep Label copy.
              pair.label.reset(new labelling::Label(labelling::mergeLabels(
                  fwd, bwd, adjacent, graph_ptr_->sink, max_res, min_res)));
              const auto& resources = pair.label->resource_consumption;
              for (size_t r = 0; r < std::min(resources.size(), CG_cut_pi.size()); ++r)
                pair.extra += CG_cut_pi[r] * std::floor(resources[r]);
              pair.label->weight += pair.extra;
              pair.feasible = pair.label->vertex.lemon_id != -1 &&
                               pair.label->checkFeasibility(max_res, min_res);
            }
            prepared.pairs.push_back(std::move(pair));
            ++result.records;
          }
        }
        if (!prepared.pairs.empty()) result.arcs.push_back(std::move(prepared));
      }
      return result;
    };

    const auto commit_forward = [&](const labelling::Label& fwd, PreparedForwardJoin& prepared) {
      if (timeLimitExceeded()) return false;
      if (!(fwd.resource_consumption[c_res] <= HF && fwd.weight + bwd_min <= UB)) return true;
      const auto mask = visitedPathMask(fwd);
      for (auto& arc : prepared.arcs) {
        if (timeLimitExceeded()) return false;
        const int m = arc.head;
        if (!(fwd.weight + arc.weight + bwd_search_ptr_->best_labels[m]->weight <= UB)) continue;
        const auto& bucket = buckets[m];
        // Evaluate group admission once at ARC entry, exactly as in serial.
        if (bucket_joining) {
          group_status.resize(bucket.groups.size());
          for (size_t g = 0; g < bucket.groups.size(); ++g) {
            const auto& group = bucket.groups[g];
            group_status[g] = !(fwd.weight + arc.weight + group.min_weight > UB ||
                (params_ptr_->elementary && mask.complete && (mask.bits & group.common_nodes) != 0));
          }
        }
        size_t last_block = std::numeric_limits<size_t>::max();
        bool block_admitted = false;
        for (auto& pair : arc.pairs) {
          if (timeLimitExceeded()) return false;
          const size_t block_id = pair.index / block_size;
          if (block_id != last_block) {
            last_block = block_id;
            const auto& block = bucket.blocks[block_id];
            // Do not recheck this gate after an incumbent changes INSIDE the block.
            block_admitted = fwd.weight + arc.weight + block.min_weight <= UB;
          }
          if (!block_admitted) continue;
          const auto& entry = bucket.entries[pair.index];
          if (bucket_joining && group_status[entry.group] == 0) continue;
          if (!(pair.base <= UB)) continue;
          if (!pair.label) {
            record_phi(pair.shortcut_path, pair.phi);
            if (stats) ++stats->resource_shortcuts;
            continue;
          }
          if (stats) ++stats->full_merges;
          auto& merged = *pair.label;
          const bool halfway_ok = record_phi(merged.partial_path, merged.phi);
          if (pair.feasible && halfway_ok) {
            if (best_label_->vertex.lemon_id == -1 || merged.fullDominance(*best_label_, FWD) ||
                merged.weight < best_label_->weight) {
              best_label_ = std::make_shared<labelling::Label>(merged);
              best_base_cost_ = pair.base;
              best_extra_cost_ = pair.extra;
              UB = std::min(UB, best_label_->weight);
              if (terminate(FWD, *best_label_)) return false;
            }
          }
          if (collect_paths_) {
            paths.push_back(merged.partial_path);
            merged_labels_.push_back(std::move(merged));
          }
        }
      }
      return true;
    };

    std::vector<PreparedForwardJoin> prepared(forward_batch_size);
    bool stop = false, node_admitted = false, node_parallel = false;
    double frozen_ub = UB;
    std::exception_ptr failure;
    // One worker team for the whole join stage, not one launch per label.
    #pragma omp parallel num_threads(join_threads)
    {
      for (const int n : fwd_search_ptr_->visited_vertices) {
        #pragma omp single
        {
          stop = stop || timeLimitExceeded();
          node_admitted = !stop && fwd_search_ptr_->best_labels[n] &&
              fwd_search_ptr_->best_labels[n]->weight + bwd_min <= UB &&
              n != graph_ptr_->sink.lemon_id;
          node_parallel = false;
          if (node_admitted) {
            const auto& frontier = fwd_search_ptr_->efficient_labels[n];
            try {
              // Commit the first label in order to establish an incumbent.
              if (!frontier.empty()) stop = !serial_forward(frontier.front(), n);
              // Sample throughout the remaining frontier: its first few labels
              // are often unrepresentative. Probes allocate no merged records.
              const size_t samples = std::min(scheduling_samples,
                  frontier.empty() ? size_t{0} : frontier.size() - 1);
              size_t work = 0;
              for (size_t i = 0; i < samples && !stop; ++i) {
                const size_t index = 1 + (frontier.size() - 2) * i / std::max(size_t{1}, samples - 1);
                const auto probe = prepare_forward(frontier[index], n, UB, true);
                work += probe.pairs;
                if (probe.expired) { timeLimitExceeded(); stop = true; }
              }
              node_parallel = !stop && samples > 0 && work >= samples * min_pairs_per_label;
              if (!node_parallel) {
                for (size_t i = 1; i < frontier.size() && !stop; ++i)
                  stop = !serial_forward(frontier[i], n);
              }
            } catch (...) { failure = std::current_exception(); stop = true; }
          }
        }
        if (node_parallel) {
          const auto& frontier = fwd_search_ptr_->efficient_labels[n];
          for (size_t begin = 1; begin < frontier.size(); begin += forward_batch_size) {
            if (stop) break;
            const int count = static_cast<int>(std::min(forward_batch_size, frontier.size() - begin));
            #pragma omp single
            { frozen_ub = UB; if (stats) ++stats->parallel_batches; }
            #pragma omp for schedule(dynamic, 1)
            for (int i = 0; i < count; ++i) {
              try { prepared[i] = prepare_forward(frontier[begin + i], n, frozen_ub); }
              catch (...) { prepared[i].error = std::current_exception(); }
            }
            #pragma omp single
            {
              try {
                if (stats) {
                  for (int i = 0; i < count; ++i) {
                    const auto& result = prepared[i];
                    stats->parallel_pairs += result.pairs;
                    stats->speculative_records += result.records;
                    stats->label_pairs += result.pairs;
                    stats->group_checks += result.groups;
                    stats->groups_skipped += result.groups_skipped;
                    stats->blocks_skipped += result.blocks_skipped;
                  }
                }
                for (int i = 0; i < count && !stop; ++i) {
                  auto& result = prepared[i];
                  if (result.error) std::rethrow_exception(result.error);
                  if (result.expired) { timeLimitExceeded(); stop = true; break; }
                  stop = !(result.overflow ? serial_forward(frontier[begin + i], n)
                                           : commit_forward(frontier[begin + i], result));
                }
              } catch (...) { failure = std::current_exception(); stop = true; }
            }
          }
        }
        // A fast worker must not overwrite node_admitted for the next node
        // while another worker is still reading this node's decision.
        #pragma omp barrier
      }
    }
    if (failure) std::rethrow_exception(failure);
    return;
  }
#endif

  for (const int n : fwd_search_ptr_->visited_vertices) {
    if (timeLimitExceeded()) return;
    if (!fwd_search_ptr_->best_labels[n] ||
        !(fwd_search_ptr_->best_labels[n]->weight + bwd_min <= UB) ||
        n == graph_ptr_->sink.lemon_id) continue;
    for (const auto& fwd_label : fwd_search_ptr_->efficient_labels[n]) {
      if (!serial_forward(fwd_label, n)) return;
    }
  }
} // end joinLabels

} // namespace bidirectional
