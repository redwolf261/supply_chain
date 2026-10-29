#pragma once
// Spectral Graph Analysis for SupplyChainX.
// Constructs the unweighted graph Laplacian L = D - A on the undirected structural projection
// of active nodes and usable edges in the supply network.
// Computes Laplacian eigenvalues, algebraic connectivity (lambda_2), the Fiedler vector,
// the sign-based structural partition, and candidate crossing bottleneck routes.

#include <optional>
#include <vector>

#include "graph/SupplyChainGraph.h"
#include "nlohmann/json.hpp"

namespace scx {

struct SpectralResult {
    int activeNodeCount = 0;
    int connectedComponents = 0;
    std::optional<double> algebraicConnectivity;   // lambda_2 (second smallest eigenvalue)
    std::vector<double> eigenvalues;              // sorted ascending: lambda_1 <= lambda_2 <= ...
    std::vector<double> fiedlerVector;            // entries corresponding to activeNodeIds
    std::vector<int> activeNodeIds;               // activeNodeIds[i] = node id in graph

    // Fiedler partition by sign:
    std::vector<int> partitionPositive;           // node ids with fiedlerVector entry >= 0
    std::vector<int> partitionNegative;           // node ids with fiedlerVector entry < 0

    // Active routes in the graph that cross between the positive and negative partitions:
    std::vector<int> crossingEdgeIds;
};

// Analyzes the active subgraph of the supply network.
// Inactive nodes and disabled edges are excluded from the projection.
SpectralResult computeSpectralAnalysis(const SupplyChainGraph& graph);

// Serializes spectral metrics and partitions to JSON.
nlohmann::json spectralResultToJson(const SpectralResult& result);

}  // namespace scx

