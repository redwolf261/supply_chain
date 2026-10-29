// Shapes of the JSON returned by the C++ server. See docs/API_REFERENCE.md.

export type NodeType = 'supplier' | 'factory' | 'warehouse' | 'distribution_center' | 'customer';

export interface NodeDto {
  id: number;
  name: string;
  type: NodeType;
  supply: number;
  demand: number;
  inventory: number;
  isBackup: boolean;
  activationCost: number;
  unitPurchaseCost: number;
  shortagePenaltyPerUnit: number;
  active: boolean;
  supplyDraw?: number;
  inventoryDraw?: number;
  unmetDemand?: number;
  /** Overrides the second line of the node card (the multi-day view shows stock and backlog per day). */
  meta?: string;
}

export interface EdgeDto {
  id: number;
  from: number;
  to: number;
  fromName: string;
  toName: string;
  baseCapacity: number;
  capacity: number;
  costPerUnit: number;
  transitTime: number;
  reopenCost: number;
  active: boolean;
  usable: boolean;
  flow?: number;
  utilisation?: number;
  /** Multi-day view: units moving on the route at the end of the day, and its transit time in days. */
  inTransit?: number;
  transitDays?: number;
}

export interface NetworkDto {
  nodes: NodeDto[];
  edges: EdgeDto[];
}

export interface MetricsDto {
  purchaseCost: number;
  transportCost: number;
  holdingCost: number;
  shortagePenalty: number;
  actionCost: number;
  totalCost: number;
  totalDemand: number;
  unmetDemand: number;
  deliveredDemand: number;
  fulfilmentRatio: number;
  networkUtilisation: number;
  totalSuppliers: number;
  activeSuppliers: number;
  factories: number;
  warehouses: number;
  distributionCenters: number;
  customers: number;
  failedNodes: number;
  failedRoutes: number;
  reducedRoutes: number;
  demandSpikes: number;
  activeDisruptions: number;
  feasible: boolean;
}

export interface EventDto {
  type: string;
  description: string;
  node?: string;
  edge?: number;
  percentRemaining?: number;
  percentIncrease?: number;
}

export interface Snapshot {
  scenario: { name: string; seed: number };
  network: NetworkDto;
  metrics: MetricsDto;
  flowSource: string;
  recoveryExecuted: boolean;
  appliedEvents: EventDto[];
  pendingEvents: EventDto[];
  applied?: string;
}

export interface AlgorithmInfo {
  id: string;
  title: string;
  category: 'analysis' | 'recovery';
  description: string;
  complexity: { time: string; space: string };
  parameters: string;
}

export interface ScenarioList {
  standard: string[];
  files: string[];
  strategies: string[];
}

export interface NodeRef {
  id: number;
  name: string;
}

export interface PlanActionDto {
  type: 'activate_supplier' | 'reopen_route';
  target: number;
  label: string;
  cost: number;
}

export interface PlanStepDto {
  target: number;
  type: string;
  score: number;
  shortageReduction: number;
  costGain: number;
  bottleneckRisk: number;
}

export interface PlanDto {
  algorithm: string;
  actions: PlanActionDto[];
  steps: PlanStepDto[];
  actionCost: number;
  flowCost: number;
  totalCost: number;
  unmetDemand: number;
  candidateEvaluations: number;
  runtimeMs: number;
  network: NetworkDto;
}

/** Result payloads differ per algorithm; the Lab narrows them with the `algorithm` id. */
export interface TraversalResult {
  source: NodeRef;
  visitOrder: NodeRef[];
  finishOrder?: NodeRef[];
  hops?: { node: string; hops: number }[];
  reachedCount: number;
  unreachable: NodeRef[];
  treeEdgeIds: number[];
  lostSinceInitial: NodeRef[];
}

export interface DijkstraResult {
  source: NodeRef;
  distances: { node: string; cost: number | null }[];
  treeEdgeIds: number[];
  target?: NodeRef;
  reachable?: boolean;
  pathNodes?: NodeRef[];
  pathEdgeIds?: number[];
  cost?: number | null;
}

export interface MaxFlowResult {
  maxDeliverable: number;
  totalDemand: number;
  minCutCapacity: number;
  minCutEdgeIds: number[];
  supplyLimitedNodes: NodeRef[];
  demandLimitedNodes: NodeRef[];
  network: NetworkDto;
}

export interface RunResponse {
  algorithm: string;
  title: string;
  category: 'analysis' | 'recovery';
  complexity: { time: string; space: string };
  runtimeMs: number;
  peakMemoryBytes: number | null;
  result?: TraversalResult | DijkstraResult | MaxFlowResult;
  plan?: PlanDto;
  metrics?: MetricsDto;
  executed?: boolean;
  snapshot?: Snapshot;
}

export interface CompareRow {
  strategy: string;
  feasible?: boolean;
  metrics?: MetricsDto;
  runtimeMs?: number;
  peakMemoryBytes?: number | null;
  plan?: PlanDto;
  skipped?: string;
}

export interface CompareResponse {
  scenario: { name: string; seed: number };
  eventsApplied: number;
  before: NetworkDto;
  results: CompareRow[];
  lowestTotalCost: string | null;
  note: string;
}

export interface ResultRow {
  id: number;
  timestamp: string;
  scenario: string;
  kind: string;
  algorithm: string;
  eventsApplied: number;
  runtimeMs: number;
  peakMemoryBytes: number | null;
  totalCost?: number;
  unmetDemand?: number;
  fulfilmentRatio?: number;
  networkUtilisation?: number;
  feasible?: boolean;
}

export type LoadRequest =
  | { standard: string; seed?: number }
  | { file: string }
  | { generate: Record<string, number>; disruptionSeverity?: number };

export interface SummaryDto { n: number; mean: number; stddev: number; min: number; max: number; ci95: number }
export interface PairedDto { n: number; meanDifference: number; stddev: number; ci95: number; firstBetter: number; secondBetter: number; ties: number }

export interface ExperimentStrategyDto {
  label: string;
  strategy: string;
  lambda: number;
  completed: number;
  skipped: number;
  cost: SummaryDto;
  unmetDemand: SummaryDto;
  fulfilment: SummaryDto;
  utilisation: SummaryDto;
  runtimeMs: SummaryDto;
  actions: SummaryDto;
  feasibilityRate: number;
  meanRegretPercent: number;
  wins: number;
  memoryMeasured: boolean;
  peakMemoryBytes: SummaryDto | null;
  pairedVs?: { against: string; cost: PairedDto };
}

export interface ExperimentRecordDto {
  run: number;
  seed: number;
  label: string;
  skipped: boolean;
  skipReason?: string;
  nodes: number;
  edges: number;
  candidateActions: number;
  disruptions: number;
  actions?: number;
  totalCost?: number;
  unmetDemand?: number;
  totalDemand?: number;
  fulfilment?: number;
  utilisation?: number;
  runtimeMs?: number;
  peakMemoryBytes?: number | null;
  feasible?: boolean;
}

export interface ExperimentResponse {
  kind: string;
  x: number;
  severity: number;
  runs: number;
  seed: number;
  meanNodes: number;
  meanEdges: number;
  strategies: ExperimentStrategyDto[];
  records: ExperimentRecordDto[];
  pairedAgainst: string;
  totalRuntimeMs: number;
  note: string;
  limits: { maxRuns: number; maxNodes: number; maxRunsTimesNodes: number };
}

// ---- POST /api/multiday/run (backend/src/multiday/MultiDayJson.cpp) ----

export type MultiDayPolicyName = 'none' | 'greedy' | 'bottleneck_aware' | 'bottleneck_aware_lambda0' | 'lookahead' | 'rolling_exact';

export interface MultiDayNodeDto {
  id: number; name: string; type: NodeType; isBackup: boolean; active: boolean;
  supplyPerDay: number; openingStock: number; unitPurchaseCost: number; holdingCostPerUnit: number;
  shortagePenaltyPerUnit: number; activationCost: number;
}
export interface MultiDayEdgeDto {
  id: number; from: number; to: number; capacityPerDay: number; costPerUnit: number; transitDays: number; reopenCost: number; active: boolean;
}
export interface MultiDayEventDto { day: number; type: string; targetId: number; value: number; description: string }

export interface MultiDayScenarioDto {
  name: string; seed: number; horizon: number;
  nodes: MultiDayNodeDto[]; edges: MultiDayEdgeDto[]; events: MultiDayEventDto[];
  demandPerDay: number[]; firstDisruptionDay: number | null; initialInTransit: number;
  supplyLeadDays: number; planningWindowDays: number; forecast: 'persistence' | 'perfect';
  activationLeadDays: number; repairLeadDays: number; serviceThreshold: number; stabilityDays: number;
}

export interface MultiDayActionDto { type: 'activate_supplier' | 'reopen_route'; targetId: number; target: string; cost: number }

export interface MultiDayDayDto {
  day: number; demand: number; due: number; served: number; newBackorders: number; backlog: number;
  stock: number; inTransit: number; serviceLevel: number; normalService?: number;
  cost: { purchase: number; transport: number; holding: number; backlog: number; actions: number; total: number };
  cumulativeCost: number; actions: MultiDayActionDto[]; events: string[]; activeDisruptions: number;
  decisionMs: number; planningMs: number; plannedSaturatedShare: number;
  stockByNode?: number[]; backlogByNode?: number[]; dispatchedByEdge?: number[]; inTransitByEdge?: number[]; capacityByEdge?: number[];
  nodeActive?: boolean[]; edgeUsable?: boolean[];
}

export interface MultiDayRecoveryDto { applicable: boolean; recovered: boolean; days: number | null; recoveryDay: number | null }

export interface MultiDayRunDto {
  policy: MultiDayPolicyName;
  skipped: boolean;
  skipReason?: string;
  totals?: {
    totalCost: number; settlement: number; actionCost: number; actions: number; totalDemand: number; totalServed: number;
    finalBacklog: number; finalInTransit: number; finalStock: number; fillRate: number; decisionMs: number; runtimeMs: number; feasible: boolean;
  };
  recovery?: MultiDayRecoveryDto;
  days?: MultiDayDayDto[];
}

export interface MultiDayResponse {
  scenario: MultiDayScenarioDto;
  normalService: number[];
  policies: MultiDayRunDto[];
  clairvoyant: { computed: boolean; reason: string; cost: number | null; candidates: number; runtimeMs: number; note: string;
    actions: (MultiDayActionDto & { decidedDay: number; effectiveDay: number })[] };
  lowestTotalCost: MultiDayPolicyName | null;
  totalRuntimeMs: number;
  limits: { minHorizon: number; maxHorizon: number; maxNodes: number; maxLookaheadDays: number; maxExhaustiveCandidates: number };
  note: string;
}


export interface SpectralResultDto {
  activeNodeCount: number;
  connectedComponents: number;
  algebraicConnectivity: number | null;
  eigenvalues: number[];
  fiedlerVector: number[];
  activeNodeIds: number[];
  partitionPositive: number[];
  partitionNegative: number[];
  crossingEdgeIds: number[];
}

export interface SpectralCompareDto {
  initial: SpectralResultDto;
  current: SpectralResultDto;
  algebraicConnectivityDelta: number | null;
  componentsDelta: number;
}
