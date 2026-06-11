#pragma once

/*
 * C++ wrapper for the BiDirectional ESPPRC solver.
 *
 * Example:
 *
 *   #include "espprc.hpp"
 *
 *   espprc::Graph graph(num_vertices, source_id, sink_id);
 *   graph.add_edge(tail, head, weight, {res0, res1});
 *
 *   auto result = espprc::solve(
 *       graph,
 *       {max_res0, max_res1},
 *       {min_res0, min_res1},
 *       {cut_pi0, cut_pi1},
 *       {.time_limit = 0.5, .elementary = true, .use_bitwise = true}
 *   );
 */

// The .cc implementation files must be compiled together. The recommended
// route is the project CMake build, which also downloads LEMON, fmt, and spdlog.

#include "src/cc/bidirectional.h"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace espprc {

struct EdgeDef {
    int tail;
    int head;
    double weight;
    std::vector<double> res_cost;
};

struct Graph {
    int num_vertices;
    int source;
    int sink;
    std::vector<EdgeDef> edges;

    Graph(int nv, int src, int snk)
        : num_vertices(nv), source(src), sink(snk) {}

    void add_edge(int tail, int head, double weight,
                  const std::vector<double>& res_cost) {
        edges.push_back({tail, head, weight, res_cost});
    }

    bool has_path() const {
        bool src_out = false;
        bool sink_in = false;
        for (const auto& edge : edges) {
            if (edge.tail == source) {
                src_out = true;
            }
            if (edge.head == sink) {
                sink_in = true;
            }
        }
        return src_out && sink_in;
    }
};

struct Options {
    bool elementary = true;
    bool bounds_pruning = true;
    bool use_bitwise = false;
    std::string direction = "both";
    double time_limit = 0.0;
};

struct Result {
    std::vector<int> path;
    double cost = std::numeric_limits<double>::infinity();
    bool feasible = false;
};

inline Result solve(
    const Graph& graph,
    const std::vector<double>& max_res,
    const std::vector<double>& min_res,
    const std::vector<double>& cg_cut_pi,
    const Options& opts = Options{}
) {
    Result result;

    if (max_res.empty() || max_res.size() != min_res.size()
        || (!cg_cut_pi.empty() && max_res.size() != cg_cut_pi.size())) {
        return result;
    }
    if (graph.edges.empty() || !graph.has_path()) {
        return result;
    }
    if (opts.use_bitwise && graph.num_vertices > 64) {
        throw std::invalid_argument("use_bitwise=true supports at most 64 nodes");
    }

    bidirectional::BiDirectional solver(
        graph.num_vertices,
        static_cast<int>(graph.edges.size()),
        graph.source,
        graph.sink,
        max_res,
        min_res,
        cg_cut_pi
    );

    std::vector<int> node_ids;
    node_ids.reserve(graph.num_vertices);
    for (int node = 0; node < graph.num_vertices; ++node) {
        node_ids.push_back(node);
    }
    solver.addNodes(node_ids);

    for (const auto& edge : graph.edges) {
        if (edge.res_cost.size() != max_res.size()) {
            return result;
        }
        solver.addEdge(edge.tail, edge.head, edge.weight, edge.res_cost);
    }

    solver.setElementary(opts.elementary);
    solver.setBoundsPruning(opts.bounds_pruning);
    if (opts.use_bitwise) {
        solver.setBitwiseElementary(true, graph.num_vertices);
    }
    if (opts.time_limit > 0.0) {
        solver.setTimeLimit(opts.time_limit);
    }
    if (opts.direction == "forward") {
        solver.setDirection("forward");
    } else if (opts.direction == "backward") {
        solver.setDirection("backward");
    }

    solver.run();

    result.path = solver.getPath();
    result.cost = solver.getTotalCost();
    result.feasible = !result.path.empty() && std::isfinite(result.cost);
    return result;
}

inline Result solve(
    int num_vertices,
    int source,
    int sink,
    const std::vector<EdgeDef>& edges,
    const std::vector<double>& max_res,
    const std::vector<double>& min_res,
    const std::vector<double>& cg_cut_pi,
    const Options& opts = Options{}
) {
    Graph graph(num_vertices, source, sink);
    graph.edges = edges;
    return solve(graph, max_res, min_res, cg_cut_pi, opts);
}

} // namespace espprc
