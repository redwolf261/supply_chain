#include "advanced/spectral/SpectralAnalysis.h"

#include <cmath>
#include <unordered_map>
#include <unordered_set>

#include "advanced/spectral/JacobiEigen.h"

namespace scx {

SpectralResult computeSpectralAnalysis(const SupplyChainGraph& graph) {
    SpectralResult result;

    // 1. Identify active nodes and build index mapping
    std::unordered_map<int, int> nodeToIndex;
    for (const Node& n : graph.nodes()) {
        if (n.active) {
            const int idx = static_cast<int>(result.activeNodeIds.size());
            nodeToIndex[n.id] = idx;
            result.activeNodeIds.push_back(n.id);
        }
    }

    const int n = static_cast<int>(result.activeNodeIds.size());
    result.activeNodeCount = n;
    if (n == 0) return result;

    if (n == 1) {
        result.connectedComponents = 1;
        result.eigenvalues = {0.0};
        result.fiedlerVector = {1.0};
        result.partitionPositive = {result.activeNodeIds[0]};
        return result;
    }

    // 2. Build unweighted undirected adjacency matrix A
    std::vector<std::vector<double>> A(n, std::vector<double>(n, 0.0));
    for (const Edge& e : graph.edges()) {
        if (!graph.isEdgeUsable(e.id)) continue;
        auto itU = nodeToIndex.find(e.from);
        auto itV = nodeToIndex.find(e.to);
        if (itU != nodeToIndex.end() && itV != nodeToIndex.end() && itU->second != itV->second) {
            const int u = itU->second;
            const int v = itV->second;
            A[u][v] = 1.0;
            A[v][u] = 1.0;
        }
    }

    // 3. Compute Graph Laplacian L = D - A
    std::vector<std::vector<double>> L(n, std::vector<double>(n, 0.0));
    for (int i = 0; i < n; ++i) {
        double deg = 0.0;
        for (int j = 0; j < n; ++j) {
            deg += A[i][j];
        }
        L[i][i] = deg;
        for (int j = 0; j < n; ++j) {
            if (i != j) {
                L[i][j] = -A[i][j];
            }
        }
    }

    // 4. Solve eigensystem of symmetric Laplacian L
    EigenResult eigen = solveSymmetricEigenvalues(L);
    result.eigenvalues = eigen.eigenvalues;

    // Multiplicity of zero eigenvalue (tolerance 1e-6) gives number of connected components
    int zeroCount = 0;
    for (double val : result.eigenvalues) {
        if (std::abs(val) < 1e-6) {
            zeroCount++;
        }
    }
    result.connectedComponents = std::max(1, zeroCount);

    // 5. Algebraic connectivity lambda_2 and Fiedler vector
    if (n >= 2) {
        const double lambda2 = std::max(0.0, result.eigenvalues[1]);
        result.algebraicConnectivity = lambda2;
        result.fiedlerVector = eigen.eigenvectors[1];

        // 6. Partition active nodes by sign of Fiedler vector
        std::unordered_map<int, int> partitionSign;  // nodeId -> +1 or -1
        for (int i = 0; i < n; ++i) {
            const int nodeId = result.activeNodeIds[i];
            if (result.fiedlerVector[i] >= 0.0) {
                result.partitionPositive.push_back(nodeId);
                partitionSign[nodeId] = 1;
            } else {
                result.partitionNegative.push_back(nodeId);
                partitionSign[nodeId] = -1;
            }
        }

        // 7. Identify active crossing routes that bridge the two partitions
        for (const Edge& e : graph.edges()) {
            if (!graph.isEdgeUsable(e.id)) continue;
            auto itU = partitionSign.find(e.from);
            auto itV = partitionSign.find(e.to);
            if (itU != partitionSign.end() && itV != partitionSign.end()) {
                if (itU->second != itV->second) {
                    result.crossingEdgeIds.push_back(e.id);
                }
            }
        }
    }

    return result;
}

nlohmann::json spectralResultToJson(const SpectralResult& result) {
    nlohmann::json j;
    j["activeNodeCount"] = result.activeNodeCount;
    j["connectedComponents"] = result.connectedComponents;
    if (result.algebraicConnectivity.has_value()) {
        j["algebraicConnectivity"] = *result.algebraicConnectivity;
    } else {
        j["algebraicConnectivity"] = nullptr;
    }
    j["eigenvalues"] = result.eigenvalues;
    j["fiedlerVector"] = result.fiedlerVector;
    j["activeNodeIds"] = result.activeNodeIds;
    j["partitionPositive"] = result.partitionPositive;
    j["partitionNegative"] = result.partitionNegative;
    j["crossingEdgeIds"] = result.crossingEdgeIds;
    return j;
}

}  // namespace scx

