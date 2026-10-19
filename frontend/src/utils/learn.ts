// Study material for the Learning Center. Complexities shown next to it come live from GET /api/algorithms,
// so the page cannot drift from what the server reports.
export interface Lesson {
  id: string;
  title: string;
  paradigm: string;
  what: string;
  why: string;
  where: string;
  how: string[];
  pseudocode: string;
  cpp: string;
  dryRun: string;
  limitations: string;
  /** Only for algorithms the server does not run from the UI; the others take their complexity from GET /api/algorithms. */
  complexity?: { time: string; space: string };
  /** Where to run it when it is not in the Algorithm Lab. */
  runIt?: string;
}

export const LESSONS: Lesson[] = [
  {
    id: 'bfs', title: 'Breadth-First Search', paradigm: 'Graph traversal',
    what: 'Visits nodes in rings around a source: first everything one hop away, then two hops, and so on.',
    why: 'It answers "what can still be reached?" in linear time and finds the fewest-hop path.',
    where: 'Failure-impact analysis: run it from a supplier before and after a disruption; the nodes that disappear are cut off.',
    how: ['Put the source in a queue and mark it reached.', 'Take the front node; look at each usable outgoing route.', 'Unreached neighbours are marked, remember their parent, and queued.', 'Stop when the queue is empty.'],
    pseudocode: 'BFS(G, s):\n  mark s reached; enqueue s\n  while queue not empty:\n    u = dequeue\n    for each usable edge (u, v):\n      if v not reached:\n        mark v; parent[v] = u; hops[v] = hops[u] + 1; enqueue v',
    cpp: 'std::queue<int> frontier gives first-in-first-out order. result.reached is a std::vector<bool> indexed by node id. Edges come from the adjacency list outgoingEdgeIds(u), and isEdgeUsable() hides failed edges and nodes.',
    dryRun: 'Edges 0→1, 0→2, 1→2, 1→3, 2→3, source 0. Queue [0]. Visit 0, queue 1,2. Visit 1, queue 3. Visit 2 (3 already marked). Visit 3. Order 0,1,2,3; hops 0,1,1,2.',
    limitations: 'Ignores costs and capacities. "Reachable" does not mean "can carry the demand".',
  },
  {
    id: 'dfs', title: 'Depth-First Search', paradigm: 'Graph traversal',
    what: 'Follows one route as deep as it can before backing up and trying the next.',
    why: 'Linear-time exploration of everything downstream of a node; its finish order is the basis of topological sorting.',
    where: 'Mapping the whole region a supplier feeds, following edge direction only.',
    how: ['Mark the source and push it on a stack.', 'Look at the top node\'s next unexplored route.', 'If it leads somewhere new, mark it and go deeper.', 'When a node has no routes left, record it as finished and back up.'],
    pseudocode: 'DFS(G, s):\n  mark s; visit s\n  for each usable edge (s, v):\n    if v not marked: DFS(G, v)\n  finish s',
    cpp: 'The code uses a std::vector of (node, next-edge-index) pairs as an explicit stack, which is what the recursion would remember, so very deep networks cannot overflow the call stack.',
    dryRun: 'Same graph from 0: visit 0 → 1 → 2 → 3. 3 has no exits: finish 3, back to 2 finish, 1 (edge to 3 already seen) finish, 0 finish. Visit 0,1,2,3; finish 3,2,1,0.',
    limitations: 'Follows direction: starting at a customer with no outgoing routes reaches only that customer. It is not undirected connectivity.',
  },
  {
    id: 'dijkstra', title: 'Dijkstra\'s shortest path', paradigm: 'Greedy',
    what: 'Finds the cheapest route from one node to every other, given non-negative route costs.',
    why: 'Cheapest delivery of a single unit; greedy choice of the closest unsettled node is provably safe.',
    where: 'Algorithm Laboratory route queries and the shortest-path dispatch baseline.',
    how: ['Distance 0 for the source, infinity elsewhere; put the source in a min-heap.', 'Pop the closest node; skip it if the entry is outdated.', 'Relax each outgoing route: if dist[u] + cost < dist[v], update v and push it.', 'Repeat until the heap is empty.'],
    pseudocode: 'dist[s] = 0\nheap.push((0, s))\nwhile heap not empty:\n  (d, u) = heap.pop()\n  if d > dist[u]: continue\n  for each usable edge (u, v, cost):\n    if d + cost < dist[v]:\n      dist[v] = d + cost; parent[v] = u; heap.push((dist[v], v))',
    cpp: 'std::priority_queue is a max-heap, so std::greater turns it into a min-heap. Instead of decrease-key we push duplicates and skip stale ones.',
    dryRun: 'Costs 0→1=2, 0→2=5, 1→2=1, 1→3=7, 2→3=2. dist: 1=2, 2=5. Pop 1: dist[2] improves to 3, dist[3]=9. Pop 2: dist[3] improves to 5. Path 0-1-2-3, cost 5.',
    limitations: 'Ignores capacities and other shipments, so it cannot allocate goods across the network: two customers may both choose the same cheap route that cannot carry both.',
  },
  {
    id: 'max_flow', title: 'Maximum flow (Edmonds-Karp)', paradigm: 'Network flow',
    what: 'Computes the most goods the network can deliver, and the minimum cut that limits it.',
    why: 'It separates "how much is possible" from "what is cheapest" and reveals the bottleneck routes.',
    where: 'Algorithm Laboratory and Network page: minimum-cut routes are the bottlenecks.',
    how: ['Add a super-source feeding suppliers and a super-sink fed by customers.', 'Find the fewest-edge path with spare capacity using BFS.', 'Push the smallest spare capacity along it; update forward and reverse residual arcs.', 'Repeat until no such path exists; routes leaving the reachable set form the minimum cut.'],
    pseudocode: 'while BFS finds path P in residual graph:\n  b = min residual capacity on P\n  for each arc on P: residual -= b; reverse residual += b\n  flow += b',
    cpp: 'Arcs are stored in pairs in one std::vector, so the reverse of arc a is a ^ 1. Residual capacity is changed in place.',
    dryRun: 'S→W1 cap 4, S→W2 cap 3, both onward to C. Path S-W1-C pushes 4, S-W2-C pushes 3, no path remains: flow 7. The cut is S→W1 and S→W2 (4 + 3).',
    limitations: 'Ignores cost: the maximum flow is not necessarily the cheapest plan.',
  },
  {
    id: 'min_cost_flow', title: 'Minimum-cost flow', paradigm: 'Network flow',
    what: 'The cheapest complete shipment plan: purchase, transport, holding and shortage penalty together.',
    why: 'It allocates goods network-wide, undoing earlier choices when a better overall plan exists.',
    where: 'Every recovery strategy ships with it; "Min-cost flow" alone shows the best plan without paid recovery actions.',
    how: ['Each customer gets a delivery arc with cost minus its shortage penalty, so unmet demand is simply demand not delivered.', 'Repeatedly find the cheapest residual path from source to sink.', 'Reverse arcs have negative cost, so node potentials keep reduced costs non-negative and Dijkstra stays valid.', 'Stop when the cheapest path no longer saves money.'],
    pseudocode: 'potentials = BellmanFord(source)\nwhile true:\n  find cheapest path P using reduced costs (Dijkstra)\n  if cost(P) >= 0: stop\n  push bottleneck along P; update potentials',
    cpp: 'long long is used for flows and costs; kInfinity is max/4 so adding a cost cannot overflow. The result is re-checked by an independent validator and cost function.',
    dryRun: 'Two suppliers serve one customer (demand 15). Supplier 1 costs 3 per unit up to 8 units, supplier 2 costs 4 per unit. Plan: 8 × 3 + 7 × 4 = 52.',
    limitations: 'Optimal only for the current set of active suppliers and routes. Deciding which backups to activate adds fixed charges, which this algorithm does not model.',
  },
  {
    id: 'greedy', title: 'Greedy recovery', paradigm: 'Greedy',
    what: 'Repeatedly takes the recovery action that removes the most unmet demand per unit of cost.',
    why: 'A fast baseline for a decision (which backups to activate) that is expensive to solve exactly.',
    where: 'Algorithm Laboratory, Comparison, the API.',
    how: ['List legal actions: activate a backup supplier, reopen a failed route.', 'Trial-apply each to a copy of the network and re-solve min-cost flow.', 'Keep only actions that reduce shortage and save more than they cost.', 'Apply the best-scoring one and repeat on the new state.'],
    pseudocode: 'while true:\n  best = none\n  for each legal action a:\n    trial = copy(state) + a; solve flow\n    if shortage falls and gain > cost(a): score = shortageReduction / cost(a)\n  if best == none: stop\n  apply best',
    cpp: 'Value semantics: SupplyChainGraph trial = state; is a deep copy, so trying an action never touches the real state. The score is a lambda passed to a shared loop.',
    dryRun: 'Demand 10, penalty 10. Backup B1: 6 units for 6 (ratio 1.0). Backup B2: 10 units for 30 (ratio 0.33). Greedy takes B1 then B2 (cost 56); the optimum is B2 alone (cost 50).',
    limitations: 'Not optimal, as the dry run shows. It is myopic: it cannot see two actions that only help together.',
  },
  {
    id: 'bottleneck_aware', title: 'Bottleneck-aware recovery (experimental)', paradigm: 'Greedy heuristic',
    what: 'Greedy recovery whose score also penalises plans that push most flow through nearly full routes.',
    why: 'Hypothesis: avoiding saturated routes gives more robust recovery. This is a hypothesis to test, not a result.',
    where: 'Comparison and the Experiment Lab (set λ, or compare "bottleneck aware" with "bottleneck aware lambda0" to see the risk term on versus off).',
    how: ['Compute avoided shortage loss L in currency.', 'Compute f, the share of shipped units on routes at 90% utilisation or more.', 'Risk R = f × L. Score = L / (cost + λ × R + ε).', 'λ = 0 switches the risk term off for the ablation.'],
    pseudocode: 'score(a) = L(a) / (cost(a) + lambda * f(a) * L(a) + eps)',
    cpp: 'Same loop as greedy; only the scoring lambda differs, so any difference in results is due to the score alone.',
    dryRun: 'Two backups both avoid 100 of loss. X costs 20 but its route is saturated (f = 1); Y costs 22 with slack (f = 0). λ = 0 picks X (total 40). λ = 1 picks Y (total 42): safer, 2 more expensive.',
    limitations: 'No superiority is claimed, and the measured experiments did not find one: in the ablation (100 paired runs for each λ from 0 to 5) and every sweep, no cost difference to greedy excluded zero, and 88% of plans had exactly the same cost as without the risk term. In one-period tests the slack it buys is never rewarded, so this does not show the idea is wrong in general. See the Experiment Lab to run your own batches.',
  },
  {
    id: 'inventory_dp', title: 'Dynamic programming: inventory over several days', paradigm: 'Dynamic programming',
    what: 'Decides how much to order on each day of a short horizon so that ordering (with a fixed cost per order), holding and lost-sales costs are lowest in total.',
    why: 'Ordering ahead can avoid repeated setup costs, but stock costs money to hold. The best plan depends on the whole horizon, and DP finds it exactly without trying every plan.',
    where: 'A standalone module (one product, one location) in backend/src/advanced/InventoryDP.cpp. It is deliberately small: it does not claim to solve the whole multi-product network.',
    how: ['State: (day t, stock i at the start of the day). Value: the cheapest cost of days t to the end.', 'Base case: after the last day nothing is left to pay, so the value is 0.', 'Fill the table backwards: for each state try every legal order q and keep the cheapest "cost today + value of tomorrow\'s state".', 'Read the plan forward from the initial stock using the stored best decisions.'],
    pseudocode: 'for t = T-1 down to 0:\n  for i = 0..C:\n    costToGo[t][i] = min over q in 0..min(Q, C - i) of\n        setup*[q>0] + unitCost[t]*q + holding*end + penalty*lost + costToGo[t+1][end]\n(stock = i + q, sold = min(stock, demand[t]), lost = demand[t] - sold, end = stock - sold)',
    cpp: 'The table is a std::vector<std::vector<long long>> of size (T+1) x (C+1). A single helper simulateDay defines one day\'s cost and is shared by the DP, the brute-force checker and the policy evaluator, so they cannot disagree about the model.',
    dryRun: 'Demand 2, 3, 2; unit cost 2; setup 5; holding 1; penalty 10; capacity 5. Last day: with 0 in stock order 2 (5 + 4 = 9), with 1 order 1 (7), with 2 nothing (0). Best plan: order 2 on day 0 (9), order 5 on day 1 (15, carry 2 for holding 2), nothing on day 2: total 26. Ordering only each day\'s need costs 29.',
    limitations: 'One product, one location, integer units, lost sales, known demand. The table grows with the warehouse capacity (pseudo-polynomial).',
    complexity: { time: 'O(T * C * Q), T days, capacity C, max order Q', space: 'O(T * C)' },
    runIt: 'build\\backend\\scx_advanced_demo.exe prints this table from the real implementation.',
  },
  {
    id: 'backtracking', title: 'Backtracking: which backup suppliers to activate', paradigm: 'Backtracking / branch and bound',
    what: 'Finds the cheapest set of backup suppliers to cover a shortage exactly, counting activation costs, unit costs and penalties for anything left unserved.',
    why: 'Each supplier is a yes/no decision, so there are 2^n possible sets. Backtracking searches them systematically and cuts branches that provably cannot beat the best set found so far.',
    where: 'A standalone module in backend/src/advanced/SupplierSelection.cpp, compared with a greedy heuristic; the network-level exact solver (the "exact" strategy) uses plain enumeration.',
    how: ['Decide suppliers one at a time: include it, or leave it out.', 'Keep the cost of activations already committed.', 'Bound: committed cost + the cost of covering the shortage if every undecided supplier were free. No completion can do better.', 'If the bound is not below the best known cost, cut the branch; undo each choice before trying the other branch.'],
    pseudocode: 'search(level, committed):\n  if level == n: evaluate; keep if cheapest; return\n  if committed + coverCost(chosen + undecided) >= best: return   // prune\n  include supplier[level]; search(level + 1, committed + a); undo\n  exclude supplier[level]; search(level + 1, committed)',
    cpp: 'The recursion is a generic lambda that receives itself (auto&& self). Shared state (chosen, usable) is changed before the recursive call and restored after it.',
    dryRun: 'Shortage 10, penalty 100. A: 6 units for 6. B: 6 units for 6. C: 10 units for 15. Optimum {A, B} costs 12. Greedy adds C first (15 is better than 406 for A alone) and then stops: 15, not optimal.',
    limitations: 'Worst case still exponential. The bound ignores future activation costs, so on tiny instances it prunes nothing; it helps on average over larger random instances.',
    complexity: { time: 'O(2^n) nodes in the worst case, O(n) each', space: 'O(n) recursion depth' },
    runIt: 'build\\backend\\scx_advanced_demo.exe shows the exact and greedy answers.',
  },
  {
    id: 'merge_sort', title: 'Divide and conquer: merge sort', paradigm: 'Divide and conquer',
    what: 'Sorts records by splitting the list in half, sorting each half, and merging the two sorted halves.',
    why: 'Guaranteed O(n log n) in every case, and stable: records with equal keys keep their original order, which matters for readable shipment reports.',
    where: 'Ordering shipment records by cost, quantity or route (backend/include/advanced/MergeSort.h). A supporting algorithm, not part of recovery.',
    how: ['If the range has 0 or 1 element it is already sorted.', 'Split it at the middle and sort both halves recursively.', 'Merge: repeatedly take the smaller front element; on a tie take the left one (stability).', 'Copy the merged buffer back.'],
    pseudocode: 'mergeSort(A, lo, hi):\n  if hi - lo < 2: return\n  mid = (lo + hi) / 2\n  mergeSort(A, lo, mid); mergeSort(A, mid, hi)\n  merge A[lo..mid) and A[mid..hi) into a buffer, ties from the left',
    cpp: 'A function template: mergeSort(std::vector<T>&, Less less) works for any type and comparison (a lambda, std::less). Elements are moved, not copied, into the buffer.',
    dryRun: '[5, 2, 9, 1]: split [5,2] | [9,1]. Merge gives [2,5] and [1,9] (1 comparison each). Final merge: 2 vs 1 take 1; 2 vs 9 take 2; 5 vs 9 take 5; copy 9. Result [1,2,5,9] with 5 comparisons.',
    limitations: 'Needs O(n) extra memory for the buffer. For tiny inputs a simpler sort is faster in practice.',
    complexity: { time: 'Theta(n log n): T(n) = 2T(n/2) + Theta(n), Master Theorem case 2', space: 'O(n) buffer, O(log n) recursion' },
  },
  {
    id: 'topological_sort', title: 'Topological sort and cycle detection', paradigm: 'Graph algorithm',
    what: 'Orders tasks so that every prerequisite comes before the tasks that depend on it, or reports a circular dependency.',
    why: 'Manufacturing steps depend on components; a valid build order exists only when the dependency graph has no cycle.',
    where: 'backend/src/advanced/TopologicalSort.cpp, for an optional manufacturing dependency graph. The transport network is NOT assumed to be acyclic: applied to it, a loop of routes is reported as a cycle.',
    how: ['Count the unfinished prerequisites of every node.', 'Repeatedly output a node with none (smallest id first) and release its dependents.', 'If nodes remain but none is free, there is a cycle.', 'A depth-first search over the blocked nodes finds one explicit cycle to report.'],
    pseudocode: 'for each node v: if prereq[v] == 0: ready.push(v)\nwhile ready not empty:\n  v = ready.pop(); output v\n  for each edge (v, w): prereq[w] -= 1; if prereq[w] == 0: ready.push(w)\nif output size < n: cycle exists',
    cpp: 'std::priority_queue with std::greater gives the smallest free id first, so the order is deterministic. The cycle search uses white/grey/black colours and an explicit stack.',
    dryRun: 'Edges 0->2, 1->2, 2->3, 2->4. Free: 0, 1. Output 0, 1 (now 2 is free), output 2 (now 3 and 4 are free), output 3, 4. With 1->2 and 2->1 nothing after 0 becomes free, and the cycle 1 -> 2 -> 1 is reported.',
    limitations: 'Many valid orders usually exist; this returns one of them (the smallest-id-first one).',
    complexity: { time: 'O(V + E)', space: 'O(V + E)' },
  },
];
