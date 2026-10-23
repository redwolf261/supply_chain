# C++ Concepts Explained

Grows with each phase. Covers only what the code actually uses.

## Graph representation (Phase 2)
`SupplyChainGraph` stores `std::vector<Node>`, `std::vector<Edge>` and two *adjacency lists*: `std::vector<std::vector<int>> outgoing_ / incoming_`, where `outgoing_[u]` holds the ids of edges leaving `u`. Ids are vector indices, so `edge(id)` is O(1). Const-correct getters return `const&`, so callers cannot modify the graph by accident. Mutation goes through named methods that check invariants (e.g. `setEdgeCapacity` refuses values above `baseCapacity`).

## Structs vs classes
`Node` / `Edge` are plain `struct`s (data with default member initialisers). `SupplyChainGraph` is a `class` with private data, so the invariants "ids are valid, quantities are non-negative" cannot be broken from outside.

## Queues, stacks, priority queues
* BFS uses `std::queue` (FIFO). DFS uses a `std::vector` as an explicit stack of `(node, next edge index)` pairs, the same information a recursive call stack would hold.
* Dijkstra uses `std::priority_queue` with `std::greater` to make a *min*-heap (the default is a max-heap). Instead of "decrease-key" we push duplicates and skip outdated entries.

## Residual graph storage (Phase 3)
`FlowNetwork` keeps arcs in one `std::vector<Arc>` and always pushes a forward arc followed by its reverse, so the reverse of arc `a` is `a ^ 1` (flip the lowest bit). Residual capacity is mutated in place; `flowOnArc` is derived as `original - residual`.

## Other idioms
* Structured bindings: `const auto [cost, node] = heap.top();`
* `long long` for flow and cost sums to avoid overflow; `kInfinity = max/4` so `distance + cost` cannot overflow.
* Exceptions (`std::invalid_argument`, `std::out_of_range`) report invalid input; the future API layer will turn them into HTTP errors.
* RAII: all memory is owned by `std::vector`s, so there is no manual `new` / `delete` and nothing leaks.
* CMake: one static library `scx_core`; each `tests/test_*.cpp` automatically becomes its own executable registered with CTest.

## Copy semantics and trial evaluation (Phase 4)
`SupplyChainGraph trial = state;` makes a deep copy (vectors copy their elements), so a greedy heuristic can try an action on `trial` and throw it away without touching the real planning `state`. This is the C++ value-semantics idea: no pointers, no manual cloning code.

## std::function and lambdas
Greedy and bottleneck-aware recovery share one loop, `runGreedyLoop`, and differ only in how they score a candidate. The score is passed as a `std::function<Score(const TrialOutcome&)>` built from a lambda such as `[config](const TrialOutcome& t) { ... }`. The `[config]` capture copies the configuration into the lambda. One loop, two strategies, no duplicated logic.

## enum class and std::set
`RecoveryActionType` is an `enum class` (scoped, not implicitly an int). `validateRecoveryPlan` uses `std::set<std::pair<int,int>>` of (type, target) to detect an action repeated in a plan, because inserting a duplicate returns `.second == false`.

## Exhaustive search by bitmask
`solveRecoveryByExhaustiveSearch` represents each subset of k actions as the bits of an integer `mask` in `[0, 2^k)`; `mask & (1ULL << i)` tests membership. It is the exact reference the heuristics are compared against (feasible only for small k, which is why it takes a `maxActions` guard).

## Simulation state and value semantics (Phase 5)
`Simulation` owns three graphs by value: `initial_`, `current_` and `std::optional<SupplyChainGraph> checkpoint_`. Because `SupplyChainGraph` is a value type, "save the state" is `checkpoint_ = current_` and "reset" is `current_ = initial_`; no pointers are shared, so a change to one copy can never leak into another. `std::optional` expresses "there may or may not be a checkpoint" without a magic value.

## Strong exception safety
`Simulation::applyDisruption` applies the event to a local copy `trial` and only then does `current_ = std::move(trial)`. If the event throws, `current_` was never touched. This "copy, modify, swap" pattern gives all-or-nothing behaviour, which the API needs so a bad request cannot leave the server half-updated.

## Header-only JSON and error types
`nlohmann::json` converts between JSON and C++ values. `ScenarioError` derives from `std::runtime_error` (`using std::runtime_error::runtime_error;` inherits its constructors), so the API layer can catch exactly the "bad user input" case separately from programmer bugs. Every JSON field is type-checked before use, so a string where an integer is expected produces a message, not a crash.

## Portable randomness
`std::mt19937` produces the same sequence everywhere for a given seed, but `std::uniform_int_distribution` is allowed to differ between standard libraries. The generator therefore maps raw values itself (`low + engine() % range`), so a seed means the same network on every machine and every compiler.

## Interfaces without dependencies (Phase 6)
`ApiService::handle(ApiRequest) -> ApiResponse` knows nothing about sockets. `HttpServer` is a thin adapter (pimpl idiom: `struct Impl` hides the heavy `httplib` header from every other file, so only one `.cpp` pays its compile time). Because the logic is a plain function of values, the same endpoints are tested directly and over real HTTP.

## Mutex and RAII locking
`std::lock_guard<std::mutex> lock(mutex_);` locks in its constructor and unlocks in its destructor, even when an exception is thrown. The server handles HTTP requests on several threads, but there is one live `Simulation`, so every request is serialised.

## Exceptions as control flow for errors
Handlers `throw ApiError{status, code, message}`; `handle()` has a chain of `catch` blocks ordered from most to least specific (`ApiError`, `ScenarioError`, JSON errors, `invalid_argument`, ..., `std::exception`). The order matters: `std::invalid_argument` is a `std::logic_error`, so it must be caught first.

## Replacing operator new (memory measurement)
`AllocationHooks.cpp` replaces the global `operator new/delete`. Each block gets a 16-byte header storing its size, so `delete` knows how many bytes were released and a counter tracks live and peak bytes. `AllocationScope` records the live level at its start and reports the peak above it. Pitfall found and fixed in this phase: with a dynamically linked `libstdc++`, code inside the DLL allocates with the DLL's own `operator new`, and our `delete` then read a header that did not exist (heap corruption, exit code 0xC0000374). The hooked executables are therefore linked with `-static`.

## Templates and callables: merge sort (Phase 8)
`mergeSort` is a function template: `template <typename T, typename Less> SortStats mergeSort(std::vector<T>&, Less less)`. The compiler generates a separate version for each element type and comparison, so sorting `ShipmentRecord`s by cost and sorting ints use the same code with no runtime overhead. `Less` can be a lambda (`[](const auto& a, const auto& b) { return a.first < b.first; }`), `std::less<int>()`, or any callable. Because it lives in a header, the template body is visible wherever it is used. The merge loop uses `std::move` to transfer elements into the buffer instead of copying them.

## Recursive lambdas and the DP table (Phase 8)
The DP table is `std::vector<std::vector<long long>>`, filled by three nested loops (no recursion needed because we go backwards in time). The brute-force and backtracking solvers recurse with a generic lambda that receives itself: `auto recurse = [&](auto&& self, int depth, ...) -> void { ... self(self, depth + 1, ...); };`. This keeps the recursion local to the function with captured state (`chosen`, `best`) instead of passing a long argument list.

## Backtracking state and undo
Backtracking mutates shared state (`chosen[index] = true`), recurses, then restores it (`chosen[index] = false`) before trying the other branch. Forgetting the undo is the classic bug; the test that compares against an all-subsets oracle exists to catch it. `std::vector<bool>` stores one bit per flag.

## Explicit stacks vs recursion
Both DFS (Phase 2) and cycle extraction in topological sort use an explicit `std::vector<std::pair<int, size_t>>` stack storing (node, next neighbour index). That is exactly the information a recursive call stack would keep, so deep graphs cannot overflow the call stack.

## std::priority_queue as a min-heap again
Kahn's algorithm uses `std::priority_queue<int, std::vector<int>, std::greater<int>>` so the smallest free node id is processed first, which makes the topological order deterministic.

## Experiments: determinism, statistics and paired design (Phase 9)
* **Reproducible randomness:** each run builds its network from `baseSeed + run`, so run 7 of any experiment is the same network every time and for every strategy. Strategies are then compared on *identical* inputs (a paired design), which removes scenario-to-scenario luck from the comparison.
* **Sample statistics in plain C++:** `summarize` computes the mean, the sample standard deviation (divides by n-1, because the mean was estimated from the same data), and a 95% confidence half-width `t * sd / sqrt(n)` using a small lookup table of Student-t values. `pairedCompare` summarises the per-run differences `a[i] - b[i]`; an interval that excludes zero is evidence of a real difference.
* **Timing:** `std::chrono::steady_clock` (monotonic, so it cannot go backwards) measures a strategy call; with several repeats the median is reported to damp noise. Memory comes from the `AllocationScope` hook of Phase 6.
* **Data as code:** the seven experiments are plain structs (`ExperimentPreset`) returned by `standardExperiments()`, so they can be listed, validated and run by the same code the API uses.
* **Work limits are policy, not parsing:** `experimentConfigFromJson` only checks types and ranges; the API separately refuses requests whose `runs x nodes` exceeds a limit (HTTP 422), so one request cannot occupy the single-threaded server for minutes.

## Multi-day simulation: state, copies and interfaces
* **State as a plain struct:** `MultiDayState` holds everything that changes from day to day (the graph with today's up/down status, `std::vector<long long>` stock and backlog per node, a `std::vector<Shipment>` pipeline, pending actions, a `std::set<std::pair<int,int>>` of purchases). Because every member is a value type, `MultiDayState copy = state;` is a complete, independent copy. The look-ahead policy simulates the future on such a copy and the real state is never touched.
* **Splitting the day into two functions:** `beginDay` (arrivals, events, capacities, demand) and `finishDay` (actions, plan, dispatch, fulfilment, costs) let the simulator call the policy in between with a `const PolicyContext&`. The policy can read the state but cannot change it.
* **Abstract interface and factory:** `MultiDayPolicy` has two pure virtual functions (`name`, `decide`); `makeMultiDayPolicy(name, ...)` returns a `std::unique_ptr<MultiDayPolicy>`, so the experiment runner, the API and the tests create any policy by name and own it without manual `delete`.
* **Information hiding as a design rule:** a policy never receives the scenario (which contains the future events). It only gets a `ForecastProvider`, the one class that decides what may be known (persistence or perfect forecast). Leakage tests check this.
* **A custom exception for "cannot apply":** `PolicyNotApplicable` (derived from `std::runtime_error`) is thrown when exhaustive search would be too large; `run` catches exactly that type and reports the run as skipped, while real errors (`std::logic_error` for a broken invariant) still stop the program.
* **Index arithmetic for a time-expanded graph:** node `(i, k)` of the planning network is `k * n + i`, so a 2-D (node, day) grid is stored in the existing 1-D `FlowNetwork` without a new data structure.
* **`std::optional` for "maybe constructed":** the API builds the simulator inside a `try` block with `sim.emplace(...)`, so a bad scenario becomes a 400 response, and the code after the block can use the simulator without a pointer.
