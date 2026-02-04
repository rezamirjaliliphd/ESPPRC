#ifndef SRC_CC_PARAMS_H__
#define SRC_CC_PARAMS_H__

#include <cmath>
#include <vector>

#include "ref_callback.h"

namespace bidirectional {

enum Directions {
  FWD,
  BWD,
  BOTH,
  NODIR
};

/**
 * Parameters with time window support and k-cycle elimination
 */
class Params {
 public:
  Directions direction = bidirectional::BOTH;
  std::string method = "unprocessed";
  double time_limit = std::nan("na");
  double threshold = std::nan("na");
  bool elementary = true;
  bool two_cycle_elimination = false;
  
  // K-cycle elimination (k=2: two-cycle, k=3: three-cycle, etc.)
  int k_cycle_elimination = 0;  // 0 = disabled, 2 = 2-cycle, 3 = 3-cycle
  
  bool bounds_pruning = false;
  bool find_critical_res = false;
  int critical_res = 0;
  bidirectional::REFCallback* ref_callback = nullptr;
  
  // Time window support
  bool use_time_windows = false;
  int time_window_res_idx = -1;  // Resource index for time windows (default: -1 disabled)
  
  // Optimization flags
  bool use_bitwise_elementary = false;  // Use bitwise operations for elementary checking
  int max_nodes = 0;  // Maximum number of nodes (for bitwise mask sizing)
  
  // Parallelization options
  bool enable_parallel = false;
  int num_threads = 1;

  /* Constructors */
  Params(){};
  ~Params() {
    ref_callback = nullptr;
    delete ref_callback;
  };

  /* Setters */
  void setDirection(const std::string& direction_in) {
    if (direction_in == "forward")
      direction = FWD;
    else if (direction_in == "backward")
      direction = BWD;
  }
  
  void setMethod(const std::string& method_in) { method = method_in; }
  void setTimeLimit(const double& time_limit_in) { time_limit = time_limit_in; }
  void setThreshold(const double& threshold_in) { threshold = threshold_in; }
  void setElementary(const bool& elementary_in) { elementary = elementary_in; }
  
  void setTwoCycleElimination(const bool& two_cycle_elimination_in) {
    two_cycle_elimination = two_cycle_elimination_in;
    if (two_cycle_elimination_in && k_cycle_elimination == 0) {
      k_cycle_elimination = 2;
    }
  };
  
  /**
   * Set k-cycle elimination
   * @param k: cycle length to eliminate (2, 3, etc.). 0 to disable.
   * Literature suggests k=3 provides best tradeoff between pruning and overhead
   */
  void setKCycleElimination(const int& k) {
    k_cycle_elimination = k;
    if (k >= 2) {
      two_cycle_elimination = true;
    }
  }
  
  void setBoundsPruning(const bool& bounds_pruning_in) {
    bounds_pruning = bounds_pruning_in;
  }
  
  void setFindCriticalRes(const bool& find_critical_res_in) {
    find_critical_res = find_critical_res_in;
  }
  
  void setCriticalRes(const int& critical_res_in) {
    critical_res = critical_res_in;
  }
  
  void setREFCallback(bidirectional::REFCallback* cb) { ref_callback = cb; };
  
  /**
   * Enable time window constraints
   * @param enable: true to enable time windows
   * @param res_idx: resource index for time (default: 0)
   */
  void setTimeWindows(const bool& enable, const int& res_idx = 0) {
    use_time_windows = enable;
    time_window_res_idx = res_idx;
  }
  
  /**
   * Enable bitwise elementary checking (faster for graphs with <= 64 or 128 nodes)
   * @param enable: true to use bitwise operations
   * @param max_nodes_in: maximum number of nodes in graph
   */
  void setBitwiseElementary(const bool& enable, const int& max_nodes_in = 0) {
    use_bitwise_elementary = enable;
    max_nodes = max_nodes_in;
  }
  
  /**
   * Enable parallel label processing
   * @param enable: true to enable parallelization
   * @param threads: number of threads (default: hardware concurrency)
   */
  void setParallel(const bool& enable, const int& threads = 0) {
    enable_parallel = enable;
    if (threads > 0) {
      num_threads = threads;
    }
  }
};

} // namespace bidirectional

#endif // SRC_CC_PARAMS_H__
