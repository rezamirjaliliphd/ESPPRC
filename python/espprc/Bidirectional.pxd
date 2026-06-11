# distutils: language = c++
from libcpp.vector cimport vector
from libcpp cimport bool
from libcpp.string cimport string

cdef extern from "bidirectional.h" namespace "bidirectional":
    cdef cppclass BiDirectional:
        BiDirectional(
            int number_vertices,
            int number_edges,
            int source_id,
            int sink_id,
            const vector[double]& max_res_in,
            const vector[double]& min_res_in,
            const vector[double]& cg_cut_pi
        ) except +

        void addNodes(const vector[int]& nodes)
        void addEdge(int tail, int head, double weight,
                     const vector[double]& resource_consumption)

        void run()
        
        # Getters
        vector[int] getPath() const
        vector[double] getConsumedResources() const
        double getTotalCost() const
        const vector[vector[int]]& getAllFeasiblePaths() const
        
        # Setters (already exist in original code)
        void setDirection(const string& direction_in)
        void setMethod(const string& method_in)
        void setTimeLimit(const double& time_limit_in)
        void setThreshold(const double& threshold_in)
        void setElementary(const bool& elementary_in)
        void setTwoCycleElimination(const bool& two_cycle_elimination_in)
        void setBoundsPruning(const bool& bounds_pruning_in)
        void setFindCriticalRes(const bool& find_critical_res_in)
        void setCriticalRes(const int& critical_res_in)
        
        # New enhanced setters
        void setKCycleElimination(const int& k)
        void setTimeWindows(const bool& enable, const int& res_idx)
        void setBitwiseElementary(const bool& enable, const int& max_nodes_in)
        void setParallel(const bool& enable, const int& threads)
