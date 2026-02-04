# distutils: language = c++
from libcpp.vector cimport vector

cdef extern from "bidirectional.h" namespace "bidirectional":
    cdef cppclass BiDirectional:
        BiDirectional(int number_vertices,
                      int number_edges,
                      int source_id,
                      int sink_id,
                      const vector[double]& max_res_in,
                      const vector[double]& min_res_in,
                      const vector[double]& cg_cut_pi) except +

        void addNodes(const vector[int]& nodes)
        void addEdge(int tail, int head, double weight,
                     const vector[double]& resource_consumption)

        void run()
        vector[int] getPath() const
        vector[double] getConsumedResources() const
        double getTotalCost() const
        const vector[vector[int]]& getAllFeasiblePaths() const
