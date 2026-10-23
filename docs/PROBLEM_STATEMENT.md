# Problem Statement

## The real-world problem

Supply chains are networks: goods flow from suppliers through warehouses and distribution centers to customers, along routes that have limited capacity and a cost per unit. When something breaks (a supplier shuts down, a route closes or loses capacity, demand suddenly rises), a planner must decide quickly:

1. **What is affected?** Which facilities can still be reached, and where are the bottlenecks?
2. **What is still possible?** How much demand can the damaged network serve at all?
3. **What should we do?** Which recovery actions are worth paying for (bringing a backup supplier online, reopening a route), and how should goods then be routed so that total cost, including the penalty for unserved customers, is as low as possible?

Choosing badly is costly in both directions: doing nothing leaves customers short, while paying for every possible action wastes money.

## The computational problem

**Input**
* A directed graph G = (V, E). Each node has a type (supplier, factory, warehouse, distribution center, customer) and may have supply with a unit purchase cost, stock with a holding cost, demand with a shortage penalty per unit, or (for backup suppliers) a one-off activation cost.
* Each edge has a capacity, a cost per unit, a transit time, and a one-off reopen cost if it has failed.
* A set of disruptions already applied: inactive nodes and edges, reduced capacities, increased demands.

**Output**
* A set of recovery actions (backups to activate, failed routes to reopen), each taken at most once.
* A shipment plan: a non-negative flow on every usable edge, the amount drawn from each supply and each stock, and the unmet demand at each customer.

**Constraints:** flow on an edge never exceeds its available capacity; nothing is drawn beyond the supply or stock that exists; goods are conserved at every node; unmet demand is between 0 and the demand; failed or offline elements carry nothing unless an action restores them.

**Objective:** minimize purchase + transport + holding of unused stock + shortage penalty + action costs (formal model: `MATHEMATICAL_MODEL.md`).

## Why it is an algorithms problem

The question splits into sub-problems with different algorithmic answers:

| Sub-problem | Algorithm | Guarantee |
|---|---|---|
| What can goods still reach? | BFS, DFS | exact, O(V + E) |
| Cheapest route for one unit | Dijkstra | exact for non-negative costs |
| Most that can be delivered, and the bottleneck | Edmonds-Karp, minimum cut | exact, O(V E²) |
| Cheapest shipments for a fixed set of active elements | minimum-cost flow | exact |
| Which recovery actions to buy | greedy, bottleneck-aware (heuristics); exhaustive search (exact, exponential) | no polynomial exact algorithm expected: a restricted form is NP-complete (`NP_COMPLETENESS.md`) |
| Multi-day ordering at one location | dynamic programming | exact for its model |
| Daily dispatch over several days with goods in transit | time-expanded minimum-cost flow, re-planned every day (rolling horizon) | exact for the planning window and the information available |
| Which actions to buy day by day | greedy, bottleneck-aware, limited look-ahead (heuristics); per-day exhaustive search; clairvoyant lower bound | heuristics; exhaustive only per day; bound valid, not always attainable |
| Exact backup selection for one shortage | backtracking with pruning | exact, exponential worst case |

So the project contrasts exact polynomial algorithms, exact exponential search and heuristics on the same problem, and measures the trade-off.

## Scope and assumptions

* One commodity and integer quantities throughout. The main model is one planning period with static demand (`MATHEMATICAL_MODEL.md`, section 1); the multi-day extension adds 5-30 days, transit times, stock and backorders (`MULTI_DAY_SIMULATION.md`).
* Factories exist as a node type and are treated like other nodes that pass goods on (or supply them); manufacturing conversion and production capacity are **not** modelled. The `processingCapacity` field is stored and validated but not used by any algorithm, and neither the generator nor the bundled scenario contains factories.
* In the single-period model unserved demand is lost and penalized; in the multi-day model it is backordered (a daily cost) and penalized once if still owed at the end.
* The multi-day dynamic programming module is a separate, restricted model (one product, one location), not a multi-period version of the network.
* What is out of scope is listed in `LIMITATIONS_AND_FUTURE_WORK.md`.

## Research question

Does accounting for downstream capacity bottlenecks (the experimental `bottleneck_aware` heuristic) improve recovery compared with immediate-benefit greedy recovery? This is treated as a hypothesis to test, not a claim. Answer from the experiments: no measurable improvement on the tested scenario families, single-period and multi-day (`EXPERIMENTAL_EVALUATION.md`). In the multi-day model its risk signal does predict later shortage, but almost nothing beyond today's backlog.
