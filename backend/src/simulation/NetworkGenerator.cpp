#include "simulation/NetworkGenerator.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <set>
#include <stdexcept>
#include <utility>

#include "algorithms/SupplyChainFlow.h"

namespace scx {

namespace {

class Random {
public:
    explicit Random(std::uint64_t seed) : engine_(static_cast<std::mt19937::result_type>(seed ^ (seed >> 32))) {}
    // Inclusive range; modulo bias is negligible for our small ranges and the mapping is portable.
    int between(int low, int high) { return low + static_cast<int>(engine_() % static_cast<unsigned>(high - low + 1)); }
    double unit() { return static_cast<double>(engine_() % 1000001u) / 1000000.0; }
    template <typename T> void shuffle(std::vector<T>& items) {
        for (size_t i = items.size(); i > 1; --i) std::swap(items[i - 1], items[static_cast<size_t>(engine_() % i)]);
    }
private:
    std::mt19937 engine_;
};

void requirePositive(int value, const char* what) {
    if (value < 1) throw std::invalid_argument(std::string(what) + " must be at least 1");
}

}  // namespace

Scenario generateNetwork(const GeneratorParams& p) {
    requirePositive(p.suppliers, "suppliers");
    requirePositive(p.warehouses, "warehouses");
    requirePositive(p.distributionCenters, "distribution centers");
    requirePositive(p.customers, "customers");
    if (p.backupSuppliers < 0) throw std::invalid_argument("backup suppliers cannot be negative");
    if (!(p.capacityFactor > 0)) throw std::invalid_argument("capacity factor must be positive");
    if (p.demandMin < 1 || p.demandMax < p.demandMin) throw std::invalid_argument("invalid demand range");

    Random random(p.seed);
    Scenario scenario;
    scenario.name = "generated_seed" + std::to_string(p.seed);
    scenario.seed = static_cast<long long>(p.seed);
    std::vector<Node> nodeData;
    auto addNodeData = [&](const Node& n) { nodeData.push_back(n); return static_cast<int>(nodeData.size()) - 1; };

    const int sourceCount = p.suppliers + p.backupSuppliers;
    std::vector<int> sources, warehouses, centers, customers;
    long long totalDemand = 0;
    std::vector<int> demands;
    for (int i = 0; i < p.customers; ++i) { demands.push_back(random.between(p.demandMin, p.demandMax)); totalDemand += demands.back(); }

    for (int i = 0; i < p.suppliers; ++i) {
        Node n; n.name = "S" + std::to_string(i + 1); n.type = NodeType::Supplier;
        const double share = static_cast<double>(totalDemand) * p.capacityFactor / p.suppliers;
        n.supply = std::max(1, static_cast<int>(std::lround(share * (0.7 + 0.6 * random.unit()))));
        n.unitPurchaseCost = random.between(3, 10);
        sources.push_back(addNodeData(n));
    }
    for (int i = 0; i < p.backupSuppliers; ++i) {
        Node n; n.name = "B" + std::to_string(i + 1); n.type = NodeType::Supplier; n.isBackup = true; n.active = false;
        const double share = static_cast<double>(totalDemand) * 0.4 / p.backupSuppliers;
        n.supply = std::max(1, static_cast<int>(std::lround(share * (0.7 + 0.6 * random.unit()))));
        n.unitPurchaseCost = random.between(6, 14);        // backups are pricier per unit...
        n.activationCost = random.between(100, 400);       // ...and cost money to bring online
        sources.push_back(addNodeData(n));
    }
    for (int i = 0; i < p.warehouses; ++i) {
        Node n; n.name = "W" + std::to_string(i + 1); n.type = NodeType::Warehouse;
        n.inventory = random.between(0, std::max(1, static_cast<int>(totalDemand / (4 * p.warehouses))));
        n.holdingCostPerUnit = random.between(0, 2);
        warehouses.push_back(addNodeData(n));
    }
    for (int i = 0; i < p.distributionCenters; ++i) {
        Node n; n.name = "D" + std::to_string(i + 1); n.type = NodeType::DistributionCenter;
        centers.push_back(addNodeData(n));
    }
    for (int i = 0; i < p.customers; ++i) {
        Node n; n.name = "C" + std::to_string(i + 1); n.type = NodeType::Customer;
        n.demand = demands[i]; n.shortagePenaltyPerUnit = random.between(60, 120);   // always above the dearest possible path (~40)
        customers.push_back(addNodeData(n));
    }

    // Plan edges as (from,to) pairs per layer first, so capacities can be scaled by layer size.
    using Layer = std::vector<std::pair<int, int>>;
    Layer upper, middle, lower;   // sources->warehouses, warehouses->centers, centers->customers
    std::set<std::pair<int, int>> used;
    auto addPlanned = [&](Layer& layer, int from, int to) {
        if (used.insert({from, to}).second) { layer.emplace_back(from, to); return true; }
        return false;
    };
    auto connect = [&](Layer& layer, const std::vector<int>& from, const std::vector<int>& to) {
        for (int f : from) addPlanned(layer, f, to[random.between(0, static_cast<int>(to.size()) - 1)]);
        for (int t : to) addPlanned(layer, from[random.between(0, static_cast<int>(from.size()) - 1)], t);
    };
    // Every warehouse is fed by a REGULAR supplier (so a healthy network can always be served) and every
    // supplier, backup or not, ships to some warehouse.
    const std::vector<int> regular(sources.begin(), sources.begin() + p.suppliers);
    connect(upper, regular, warehouses);
    for (int source : sources) addPlanned(upper, source, warehouses[random.between(0, p.warehouses - 1)]);
    connect(middle, warehouses, centers);
    connect(lower, centers, customers);

    const int minimum = static_cast<int>(upper.size() + middle.size() + lower.size());
    const long long possible = static_cast<long long>(sources.size()) * warehouses.size() +
                               static_cast<long long>(warehouses.size()) * centers.size() +
                               static_cast<long long>(centers.size()) * customers.size();
    // Automatic target: about half again the minimum, but never more than small networks can hold. (An explicit target
    // that is impossible is still an error.)
    const int target = p.edges > 0 ? p.edges : static_cast<int>(std::min<long long>(possible, minimum + minimum / 2));
    if (target < minimum) throw std::invalid_argument("edge target " + std::to_string(target) + " is below the " + std::to_string(minimum) + " needed to connect every layer");
    if (target > possible) throw std::invalid_argument("edge target exceeds the number of possible layered edges (" + std::to_string(possible) + ")");
    int total = minimum;
    for (int attempts = 0; total < target && attempts < 200 * target; ++attempts) {
        switch (random.between(0, 2)) {
            case 0: total += addPlanned(upper, sources[random.between(0, sourceCount - 1)], warehouses[random.between(0, p.warehouses - 1)]); break;
            case 1: total += addPlanned(middle, warehouses[random.between(0, p.warehouses - 1)], centers[random.between(0, p.distributionCenters - 1)]); break;
            default: total += addPlanned(lower, centers[random.between(0, p.distributionCenters - 1)], customers[random.between(0, p.customers - 1)]); break;
        }
    }

    std::vector<Edge> edgeData;
    auto emit = [&](const Layer& layer) {
        if (layer.empty()) return;
        // Layer capacity is about 2x the demand scaled by capacityFactor: the factor 2 reflects that
        // not every edge is on a used path. Individual edges vary between 0.5x and 1.5x of the mean.
        const double mean = 2.0 * static_cast<double>(totalDemand) * p.capacityFactor / layer.size();
        for (const auto& [from, to] : layer) {
            Edge e; e.from = from; e.to = to;
            e.baseCapacity = std::max(5, static_cast<int>(std::lround(mean * (0.5 + random.unit()))));
            e.costPerUnit = random.between(1, 9);
            e.transitTime = random.between(1, 12);
            e.reopenCost = random.between(10, 40);
            edgeData.push_back(e);
        }
    };
    emit(upper); emit(middle); emit(lower);

    // A healthy network must be able to serve all demand, otherwise "normal operations" would already be
    // a shortage scenario. If the random draw is too tight, scale regular supply and every route up by 15%
    // steps (at most 40 times) until the maximum flow covers the demand.
    auto assemble = [&](double scale) {
        SupplyChainGraph graph;
        for (Node n : nodeData) {
            if (n.type == NodeType::Supplier && !n.isBackup) n.supply = static_cast<int>(std::lround(n.supply * scale));
            graph.addNode(n);
        }
        for (Edge e : edgeData) {
            e.baseCapacity = static_cast<int>(std::lround(e.baseCapacity * scale));
            graph.addEdge(e);
        }
        return graph;
    };
    double scale = 1.0;
    SupplyChainGraph g = assemble(scale);
    for (int attempt = 0; attempt < 40 && computeMaxFlow(g).maxDeliverable < totalDemand; ++attempt) {
        scale *= 1.15;
        g = assemble(scale);
    }
    scenario.graph = std::move(g);

    const std::vector<std::string> problems = scenario.graph.validate();
    if (!problems.empty()) throw std::logic_error("generator produced an invalid network: " + problems.front());
    return scenario;
}

std::vector<Disruption> generateDisruptions(const SupplyChainGraph& graph, double severity, std::uint64_t seed) {
    if (!(severity >= 0.0 && severity <= 1.0)) throw std::invalid_argument("severity must be in [0, 1]");
    Random random(seed);
    SupplyChainGraph working = graph;        // events are applied here so each one is valid in sequence
    std::vector<Disruption> events;
    auto tryApply = [&](const Disruption& event) {
        try { scx::applyDisruption(working, event); events.push_back(event); return true; }
        catch (const std::exception&) { return false; }
    };

    std::vector<int> suppliers;
    for (const Node& n : graph.nodes()) if (n.type == NodeType::Supplier && n.active && !n.isBackup) suppliers.push_back(n.id);
    random.shuffle(suppliers);
    size_t failures = static_cast<size_t>(std::lround(severity * suppliers.size()));
    if (severity > 0 && failures == 0 && !suppliers.empty()) failures = 1;
    for (size_t i = 0; i < failures && i < suppliers.size(); ++i) tryApply({DisruptionType::NodeFailure, suppliers[i], 0});

    std::vector<int> routes;
    for (const Edge& e : graph.edges()) if (graph.isEdgeUsable(e.id)) routes.push_back(e.id);
    random.shuffle(routes);
    const size_t routeFailures = static_cast<size_t>(std::lround(severity * 0.15 * routes.size()));
    const size_t routeCuts = static_cast<size_t>(std::lround(severity * 0.15 * routes.size()));
    for (size_t i = 0; i < routeFailures; ++i) tryApply({DisruptionType::RouteFailure, routes[i], 0});
    for (size_t i = routeFailures; i < routeFailures + routeCuts && i < routes.size(); ++i) {
        tryApply({DisruptionType::CapacityReduction, routes[i], random.between(20, 60)});
    }

    std::vector<int> customers;
    for (const Node& n : graph.nodes()) if (n.type == NodeType::Customer) customers.push_back(n.id);
    random.shuffle(customers);
    const size_t spikes = static_cast<size_t>(std::lround(severity * customers.size()));
    for (size_t i = 0; i < spikes; ++i) tryApply({DisruptionType::DemandSpike, customers[i], random.between(20, 80)});
    return events;
}

const std::vector<std::string>& standardScenarioNames() {
    static const std::vector<std::string> names = {"normal_operations", "supplier_failure", "route_failure",
        "capacity_reduction", "demand_spike", "multiple_disruptions", "recovery_after_failure", "stress_test"};
    return names;
}

GeneratorParams stressTestParams() {
    GeneratorParams p;
    p.suppliers = 12; p.backupSuppliers = 4; p.warehouses = 8; p.distributionCenters = 8; p.customers = 30;
    p.capacityFactor = 1.1;
    return p;
}

namespace {
int largestSupplier(const SupplyChainGraph& graph) {
    int best = -1;
    for (const Node& n : graph.nodes()) {
        if (n.type == NodeType::Supplier && n.active && !n.isBackup && (best < 0 || n.supply > graph.node(best).supply)) best = n.id;
    }
    if (best < 0) throw std::invalid_argument("network has no active regular supplier");
    return best;
}

// The route carrying the most flow in the optimal plan: failing it hurts the most.
int busiestRoute(const SupplyChainGraph& graph) {
    const MinCostFlowResult solved = solveMinCostFlow(graph);
    int best = -1;
    for (const Edge& e : graph.edges()) {
        if (solved.plan.edgeFlow[e.id] > 0 && (best < 0 || solved.plan.edgeFlow[e.id] > solved.plan.edgeFlow[best])) best = e.id;
    }
    if (best < 0) throw std::invalid_argument("network ships nothing, so no route is busy");
    return best;
}

int largestCustomer(const SupplyChainGraph& graph) {
    int best = -1;
    for (const Node& n : graph.nodes()) {
        if (n.type == NodeType::Customer && (best < 0 || n.demand > graph.node(best).demand)) best = n.id;
    }
    return best;
}
}  // namespace

Scenario buildStandardScenario(const std::string& name, std::uint64_t seed) {
    GeneratorParams params = name == "stress_test" ? stressTestParams() : GeneratorParams{};
    params.seed = seed;
    Scenario scenario = generateNetwork(params);
    scenario.name = name + "_seed" + std::to_string(seed);
    const SupplyChainGraph& g = scenario.graph;

    if (name == "normal_operations") {
        // no events
    } else if (name == "supplier_failure") {
        scenario.events = {{DisruptionType::NodeFailure, largestSupplier(g), 0}};
    } else if (name == "route_failure") {
        scenario.events = {{DisruptionType::RouteFailure, busiestRoute(g), 0}};
    } else if (name == "capacity_reduction") {
        scenario.events = {{DisruptionType::CapacityReduction, busiestRoute(g), 30}};
    } else if (name == "demand_spike") {
        for (const Node& n : g.nodes()) if (n.type == NodeType::Customer) scenario.events.push_back({DisruptionType::DemandSpike, n.id, 50});
    } else if (name == "multiple_disruptions") {
        SupplyChainGraph working = g;
        const Disruption supplierDown{DisruptionType::NodeFailure, largestSupplier(g), 0};
        scx::applyDisruption(working, supplierDown);
        const Disruption routeDown{DisruptionType::RouteFailure, busiestRoute(working), 0};   // chosen AFTER the supplier failed
        scenario.events = {supplierDown, routeDown, {DisruptionType::DemandSpike, largestCustomer(g), 30}};
    } else if (name == "recovery_after_failure") {
        const int supplier = largestSupplier(g);
        scenario.events = {{DisruptionType::NodeFailure, supplier, 0}, {DisruptionType::RestoreNode, supplier, 0}};
    } else if (name == "stress_test") {
        scenario.events = generateDisruptions(g, 0.5, seed);
    } else {
        throw std::invalid_argument("unknown standard scenario '" + name + "'");
    }
    return scenario;
}

}  // namespace scx
