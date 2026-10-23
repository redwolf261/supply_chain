# Mathematical Model (v1: single commodity, single period)

This is the model every optimizer in SupplyChainX is measured against. Phase 3+ code must match it exactly.

## 1. Assumptions

1. One commodity (finished goods), one planning period, static demand.
2. Quantities are non-negative **integers** (units of goods). Costs are currency units **per unit**. Time is in hours (used only for reporting in v1).
3. Suppliers (and factories, treated as sources in v1) provide goods. Warehouses and distribution centers are transshipment nodes that may also hold pre-existing inventory. Customers consume goods. Factories do **not** transform goods in v1, so ordinary flow conservation is valid. Manufacturing conversion is deferred (see section 8).
4. Unmet demand is allowed at a per-unit penalty.
5. The network may contain cycles; we never assume it is a DAG.
6. A disruption never deletes anything. It sets `active=false`, lowers `capacity` (never below 0, never above `baseCapacity`), or changes `demand`.

## 2. Sets and parameters

| Symbol | Meaning |
|---|---|
| N, E | node set, edge set (directed). Edge e = (u,v) |
| C ⊆ N | customers |
| A ⊆ N | *available* nodes: `active = true` |
| u_e | **available** capacity of edge e (0 if the edge is inactive or either endpoint is inactive) |
| c_e | transportation cost per unit on e |
| s_i | supply of node i (0 if i inactive) |
| q_i | inventory of node i (0 if i inactive) |
| a_i | purchase cost per unit of supply at i |
| h_i | holding cost per unit of inventory left unused at the end of the period |
| d_j | demand of customer j |
| p_j | shortage penalty per unmet unit at customer j |
| F_i, G_e | one-off activation cost of backup supplier i / reopen cost of failed edge e |

## 3. Decision variables

* x_e ≥ 0 — units shipped on edge e
* y_i ≥ 0 — units drawn from the *supply* of node i (purchased)
* z_i ≥ 0 — units drawn from the *inventory* of node i
* r_j ≥ 0 — unmet demand at customer j (**shortage variable**)
* w_i ∈ {0,1} — backup supplier i activated; v_e ∈ {0,1} — failed edge e reopened (recovery actions)

## 4. Constraints

For every node i (flow conservation with sources and sinks):

    y_i + z_i + Σ_{e into i} x_e  −  Σ_{e out of i} x_e  =  { d_i − r_i   if i ∈ C
                                                              { 0           otherwise

Bounds:

    0 ≤ x_e ≤ u_e                     (capacity)
    0 ≤ y_i ≤ s_i · [i usable]        (supply limit)
    0 ≤ z_i ≤ q_i                     (cannot invent inventory)
    0 ≤ r_j ≤ d_j                     (cannot be short more than demanded)
    u_e = 0 if the edge is failed and v_e = 0; y_i = 0 if i is a backup and w_i = 0

**Delivered goods** at customer j are D_j = d_j − r_j. Shortage is a variable, never a flow of goods: it is never counted as delivered.

## 5. Objective (no double counting)

    J = Σ a_i y_i                         purchase    (only units drawn from SUPPLY)
      + Σ c_e x_e                         transport
      + Σ h_i (q_i − z_i)                 holding     (only inventory left UNUSED)
      + Σ p_j r_j                         shortage penalty
      + Σ F_i w_i + Σ G_e v_e             distinct recovery actions, each paid once

Why no double counting: inventory is a sunk cost, so drawing it costs nothing (z_i has no purchase term); it is charged only through holding when left unused. Each recovery action is charged once via its binary variable regardless of how much flow uses it. Production cost does not appear in v1 (no manufacturing).

## 6. Feasibility

Because r_j may equal d_j, the flow problem is **always feasible** (ship nothing, take full penalty). "Infeasible recovery" therefore means *unmet demand remains*, which is reported rather than treated as an error. A plan is *valid* iff it satisfies all constraints in section 4 for the post-disruption network, which `validatePlan` (Phase 4) checks independently of the algorithm that produced it.

## 7. Reduction to minimum-cost flow (fixed set of active nodes/edges)

Build a network with a super-source σ and super-sink τ:

* σ → i with capacity s_i, cost a_i (purchased supply);
* σ → i with capacity q_i, cost −h_i (stock: using it is free and saves its holding cost);
* each real edge e with (u_e, c_e);
* customer j → τ with capacity d_j and cost **−p_j** (a *delivery* arc).

Solve for the minimum-cost flow over **all flow values** (stop when the cheapest augmenting path costs ≥ 0). Then:

    delivered_j = flow on (j → τ),   r_j = d_j − delivered_j
    J = (network cost) + Σ p_j d_j + Σ h_i q_i

Why this is exact: p_j r_j = p_j d_j − p_j · delivered_j, so charging the penalty on shortage is the same as a constant minus a reward p_j per delivered unit. No "dummy shortage source" exists, so fake goods can never enter the network and be forwarded to other customers. (An earlier draft of this section used a dummy source; that variant is unsound when customers also transship, so it was replaced.)

Negative-cost arcs only leave σ or enter τ, and real edge costs are ≥ 0, so there is no negative cycle. Initial node potentials come from Bellman–Ford; afterwards Dijkstra on reduced costs is valid.

Total cost is **recomputed from the raw flows by `computeCost`**, independent of the solver, and tests compare it with exhaustive enumeration of every integer plan on random small networks.

## 8. What v1 does not model (deliberately)

* Multi-commodity flows and bill-of-materials. Conversion nodes (inputs → outputs) violate flow conservation, so they need either a generalized-flow model or a restricted production-capacity model. This will be added only as a clearly defined restricted version.
* Choosing w and v (which backups to activate, which routes to reopen) makes the problem a fixed-charge network design problem, which is NP-hard in general (it generalizes facility location). Min-cost flow solves the flow part *optimally for a given set of activations*; the greedy and bottleneck-aware recovery algorithms choose the activations heuristically. No NP-completeness proof is claimed for our specific restricted decision problem; that is Phase 8 work.
* Inventory dynamics across periods (done separately by the DP module).
