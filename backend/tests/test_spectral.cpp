#include "advanced/spectral/JacobiEigen.h"
#include "advanced/spectral/SpectralAnalysis.h"
#include "graph/SupplyChainGraph.h"
#include "test_framework.h"

#include <cmath>

using namespace scx;

namespace {

Node makeNode(const std::string& name, NodeType type) {
    Node n;
    n.name = name;
    n.type = type;
    return n;
}

Edge makeEdge(int from, int to, int capacity = 50, int cost = 1) {
    Edge e;
    e.from = from;
    e.to = to;
    e.baseCapacity = capacity;
    e.capacity = capacity;
    e.costPerUnit = cost;
    return e;
}

bool approx(double a, double b, double tol = 1e-4) {
    return std::abs(a - b) <= tol;
}

}  // namespace

static void testJacobiKnownMatrices() {
    // 2x2 symmetric matrix: [[2, 1], [1, 2]] -> eigenvalues 1 and 3
    std::vector<std::vector<double>> m2 = {
        {2.0, 1.0},
        {1.0, 2.0}
    };
    EigenResult res2 = solveSymmetricEigenvalues(m2);
    SCX_CHECK(res2.converged);
    SCX_CHECK_EQ(static_cast<int>(res2.eigenvalues.size()), 2);
    SCX_CHECK(approx(res2.eigenvalues[0], 1.0));
    SCX_CHECK(approx(res2.eigenvalues[1], 3.0));

    // 3x3 diagonal matrix
    std::vector<std::vector<double>> diag = {
        {5.0, 0.0, 0.0},
        {0.0, 1.0, 0.0},
        {0.0, 0.0, 3.0}
    };
    EigenResult resDiag = solveSymmetricEigenvalues(diag);
    SCX_CHECK(resDiag.converged);
    SCX_CHECK(approx(resDiag.eigenvalues[0], 1.0));
    SCX_CHECK(approx(resDiag.eigenvalues[1], 3.0));
    SCX_CHECK(approx(resDiag.eigenvalues[2], 5.0));
}

static void testSingleNodeGraph() {
    SupplyChainGraph g;
    g.addNode(makeNode("S1", NodeType::Supplier));
    SpectralResult spec = computeSpectralAnalysis(g);

    SCX_CHECK_EQ(spec.activeNodeCount, 1);
    SCX_CHECK_EQ(spec.connectedComponents, 1);
    SCX_CHECK(!spec.algebraicConnectivity.has_value());
    SCX_CHECK_EQ(static_cast<int>(spec.eigenvalues.size()), 1);
    SCX_CHECK(approx(spec.eigenvalues[0], 0.0));
}

static void testConnectedPathGraph() {
    // Path graph P4: S1 - W1 - D1 - C1
    // Laplacian eigenvalues for P_n: 2 - 2*cos(pi*k/n), for k = 0, 1, 2, 3
    // For n = 4:
    // lambda_1 = 0
    // lambda_2 = 2 - 2*cos(pi/4) = 2 - sqrt(2) approx 0.585786
    // lambda_3 = 2 - 2*cos(pi/2) = 2.0
    // lambda_4 = 2 - 2*cos(3pi/4) = 2 + sqrt(2) approx 3.414214
    SupplyChainGraph g;
    int s = g.addNode(makeNode("S1", NodeType::Supplier));
    int w = g.addNode(makeNode("W1", NodeType::Warehouse));
    int d = g.addNode(makeNode("D1", NodeType::DistributionCenter));
    int c = g.addNode(makeNode("C1", NodeType::Customer));

    g.addEdge(makeEdge(s, w));
    g.addEdge(makeEdge(w, d));
    g.addEdge(makeEdge(d, c));

    SpectralResult spec = computeSpectralAnalysis(g);
    SCX_CHECK_EQ(spec.activeNodeCount, 4);
    SCX_CHECK_EQ(spec.connectedComponents, 1);
    SCX_CHECK(spec.algebraicConnectivity.has_value());
    SCX_CHECK(approx(*spec.algebraicConnectivity, 2.0 - std::sqrt(2.0), 1e-3));
    SCX_CHECK(approx(spec.eigenvalues[2], 2.0, 1e-3));
    SCX_CHECK(approx(spec.eigenvalues[3], 2.0 + std::sqrt(2.0), 1e-3));

    // Fiedler partition should split the path
    SCX_CHECK(!spec.partitionPositive.empty());
    SCX_CHECK(!spec.partitionNegative.empty());
    SCX_CHECK_EQ(static_cast<int>(spec.partitionPositive.size() + spec.partitionNegative.size()), 4);
    SCX_CHECK(!spec.crossingEdgeIds.empty());
}

static void testDisconnectedGraphTwoComponents() {
    // Two isolated pairs: (S1 - W1) and (S2 - W2)
    SupplyChainGraph g;
    int s1 = g.addNode(makeNode("S1", NodeType::Supplier));
    int w1 = g.addNode(makeNode("W1", NodeType::Warehouse));
    int s2 = g.addNode(makeNode("S2", NodeType::Supplier));
    int w2 = g.addNode(makeNode("W2", NodeType::Warehouse));

    g.addEdge(makeEdge(s1, w1));
    g.addEdge(makeEdge(s2, w2));

    SpectralResult spec = computeSpectralAnalysis(g);
    SCX_CHECK_EQ(spec.activeNodeCount, 4);
    SCX_CHECK_EQ(spec.connectedComponents, 2);
    SCX_CHECK(spec.algebraicConnectivity.has_value());
    // Two components => lambda_2 = 0
    SCX_CHECK(approx(*spec.algebraicConnectivity, 0.0, 1e-5));
}

static void testCompleteGraphK4() {
    // Complete graph K4 has unweighted Laplacian eigenvalues: 0, 4, 4, 4
    SupplyChainGraph g;
    int n0 = g.addNode(makeNode("N0", NodeType::Warehouse));
    int n1 = g.addNode(makeNode("N1", NodeType::Warehouse));
    int n2 = g.addNode(makeNode("N2", NodeType::Warehouse));
    int n3 = g.addNode(makeNode("N3", NodeType::Warehouse));

    g.addEdge(makeEdge(n0, n1));
    g.addEdge(makeEdge(n0, n2));
    g.addEdge(makeEdge(n0, n3));
    g.addEdge(makeEdge(n1, n2));
    g.addEdge(makeEdge(n1, n3));
    g.addEdge(makeEdge(n2, n3));

    SpectralResult spec = computeSpectralAnalysis(g);
    SCX_CHECK_EQ(spec.activeNodeCount, 4);
    SCX_CHECK_EQ(spec.connectedComponents, 1);
    SCX_CHECK(spec.algebraicConnectivity.has_value());
    SCX_CHECK(approx(spec.eigenvalues[0], 0.0, 1e-4));
    SCX_CHECK(approx(spec.eigenvalues[1], 4.0, 1e-4));
    SCX_CHECK(approx(spec.eigenvalues[2], 4.0, 1e-4));
    SCX_CHECK(approx(spec.eigenvalues[3], 4.0, 1e-4));
    SCX_CHECK(approx(*spec.algebraicConnectivity, 4.0, 1e-4));
}

static void testDisruptionBridgeFailure() {
    // Bridge graph: (S1-W1) connected to (D1-C1) solely via bridge (W1->D1)
    SupplyChainGraph g;
    int s1 = g.addNode(makeNode("S1", NodeType::Supplier));
    int w1 = g.addNode(makeNode("W1", NodeType::Warehouse));
    int d1 = g.addNode(makeNode("D1", NodeType::DistributionCenter));
    int c1 = g.addNode(makeNode("C1", NodeType::Customer));

    g.addEdge(makeEdge(s1, w1));
    int bridgeEdge = g.addEdge(makeEdge(w1, d1));
    g.addEdge(makeEdge(d1, c1));

    SpectralResult initialSpec = computeSpectralAnalysis(g);
    SCX_CHECK_EQ(initialSpec.connectedComponents, 1);
    SCX_CHECK(initialSpec.algebraicConnectivity.has_value() && *initialSpec.algebraicConnectivity > 0.0);

    // Fail the bridge edge
    g.setEdgeActive(bridgeEdge, false);
    SpectralResult disruptedSpec = computeSpectralAnalysis(g);
    SCX_CHECK_EQ(disruptedSpec.connectedComponents, 2);
    SCX_CHECK(disruptedSpec.algebraicConnectivity.has_value());
    SCX_CHECK(approx(*disruptedSpec.algebraicConnectivity, 0.0, 1e-5));
}

int main() {
    std::cout << "Running test_spectral...\n";
    SCX_RUN(testJacobiKnownMatrices);
    SCX_RUN(testSingleNodeGraph);
    SCX_RUN(testConnectedPathGraph);
    SCX_RUN(testDisconnectedGraphTwoComponents);
    SCX_RUN(testCompleteGraphK4);
    SCX_RUN(testDisruptionBridgeFailure);
    return scx_test::failureCount() == 0 ? 0 : 1;
}
