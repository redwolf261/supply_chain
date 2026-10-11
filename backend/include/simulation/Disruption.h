#pragma once
// Disruption events change the CURRENT network; they never touch the pristine copy the
// simulation keeps, so a reset can always restore the original state.
//
//   node_failure / supplier_failure   target node becomes inactive
//   route_failure                     target edge becomes inactive
//   capacity_reduction                edge keeps `value` percent of its BASE capacity (0..100)
//   demand_spike                      customer demand grows by `value` percent (>= 0)
//   restore_node / restore_route      a failed element comes back (route: at full base capacity).
//                                     Free of charge: this models the outside world recovering,
//                                     unlike recovery ACTIONS, which cost money.

#include <string>

#include "graph/SupplyChainGraph.h"

namespace scx {

enum class DisruptionType {
    NodeFailure, RouteFailure, CapacityReduction, DemandSpike, RestoreNode, RestoreRoute
};

struct Disruption {
    DisruptionType type = DisruptionType::NodeFailure;
    int targetId = -1;     // node id, or edge id for route/capacity/restore_route
    long long value = 0;   // percent for capacity_reduction / demand_spike, unused otherwise
};

const char* toString(DisruptionType type);

// Throws std::invalid_argument with a precise message if the event is not applicable to the current
// state (bad id, wrong node type, already failed, nothing to restore, percent out of range, ...).
// The graph is unchanged when it throws.
void applyDisruption(SupplyChainGraph& graph, const Disruption& event);

// Human-readable one-liner for logs and the UI, e.g. "supplier S1 fails".
std::string describe(const SupplyChainGraph& graph, const Disruption& event);

}  // namespace scx
