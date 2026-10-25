#pragma once
// Seeded multi-day scenarios: a network from the existing generator plus everything that exists only in time
// (docs/MULTI_DAY_SIMULATION.md, section 10). The same parameters and seed always give the same scenario.

#include <cstdint>

#include "multiday/MultiDayModel.h"
#include "simulation/NetworkGenerator.h"

namespace scx {

struct MultiDayScenarioParams {
    GeneratorParams network;                // its seed is replaced by `seed`
    int horizon = 14;
    int transitMin = 1, transitMax = 2;     // transit days per route, drawn uniformly (equal values = fixed)
    double demandAmplitude = 0.2;           // daily demand = base * (1 + amplitude * u), u uniform in [-1, 1]
    int spikePercent = 0;                   // extra demand on every customer during the spike (0 = none)
    int spikeStart = 3, spikeDuration = 3;
    int supplierFailures = 1;               // the largest regular suppliers fail ...
    int supplierFailureDay = 2;             // ... on this day ...
    int supplierFailureDuration = 6;        // ... for this many days (0 = until the end)
    int routeFailures = 0;                  // the busiest routes (in a one-day optimal plan) are disrupted ...
    int routeFailureDay = 2;
    int routeFailureDuration = 5;           // (0 = until the end)
    int routeCapacityLossPercent = 100;     // 100 = the route fails; otherwise it keeps (100 - x)% of its capacity
    double initialStockDays = 1.0;          // warehouse stock at day 0, in days of total demand, split over warehouses
    double penaltyScale = 1.0;              // multiplies every customer's shortage penalty
    // Warm start: every route already carries the healthy one-day optimal flow on each of its transit days,
    // as if the network had been running normally before day 0. Without it the empty pipeline causes a
    // start-up shortage at every customer that has nothing to do with the disruptions being studied.
    bool warmStart = true;
    std::uint64_t seed = 1;
};

// Throws std::invalid_argument for out-of-range parameters.
MultiDayScenario buildMultiDayScenario(const MultiDayScenarioParams& params);

}  // namespace scx
