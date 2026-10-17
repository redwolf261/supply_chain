import type { NodeType } from '../types/api';

// Colors are CSS variables (see styles/theme.css) so light/dark swap in one place. Each entity keeps the
// same color everywhere: a node type or a strategy never changes color because another one was filtered out.
export const NODE_TYPE_COLOR: Record<NodeType, string> = {
  supplier: 'var(--series-1)',
  factory: 'var(--series-3)',
  warehouse: 'var(--series-7)',
  distribution_center: 'var(--series-4)',
  customer: 'var(--series-6)',
};

/** One letter on every node so type is never conveyed by color alone. */
export const NODE_TYPE_LETTER: Record<NodeType, string> = {
  supplier: 'S', factory: 'F', warehouse: 'W', distribution_center: 'D', customer: 'C',
};

export const STRATEGY_COLOR: Record<string, string> = {
  min_cost_flow: 'var(--series-1)',
  greedy: 'var(--series-2)',
  bottleneck_aware: 'var(--series-3)',
  bottleneck_aware_lambda0: 'var(--series-7)',
  dijkstra_dispatch: 'var(--series-5)',
  exact: 'var(--series-6)',
  // multi-day policies: 'none' re-plans without paid actions like min_cost_flow, rolling_exact is the exhaustive one
  none: 'var(--series-1)',
  lookahead: 'var(--series-5)',
  rolling_exact: 'var(--series-6)',
};

export const strategyColor = (id: string): string => STRATEGY_COLOR[id] ?? 'var(--series-8)';

export const COST_COMPONENTS = [
  { key: 'purchaseCost', label: 'Purchase', color: 'var(--series-1)' },
  { key: 'transportCost', label: 'Transport', color: 'var(--series-3)' },
  { key: 'holdingCost', label: 'Holding', color: 'var(--series-7)' },
  { key: 'shortagePenalty', label: 'Shortage penalty', color: 'var(--series-2)' },
  { key: 'actionCost', label: 'Recovery actions', color: 'var(--series-5)' },
] as const;
