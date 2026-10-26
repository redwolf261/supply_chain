import type { MultiDayDayDto, MultiDayRecoveryDto, MultiDayResponse, MultiDayRunDto, NetworkDto } from '../types/api';

export const POLICY_LABEL: Record<string, string> = {
  none: 'No actions',
  greedy: 'Greedy',
  bottleneck_aware: 'Bottleneck-aware',
  bottleneck_aware_lambda0: 'Bottleneck-aware (λ=0)',
  lookahead: 'Look-ahead',
  rolling_exact: 'Rolling exact',
};
export const policyLabel = (id: string): string => POLICY_LABEL[id] ?? id;

/** Runs that completed (a skipped run has no days to draw). */
export const completedRuns = (r: MultiDayResponse): MultiDayRunDto[] => r.policies.filter((p) => !p.skipped && p.days);

export type DayField = 'serviceLevel' | 'backlog' | 'cumulativeCost' | 'served' | 'stock' | 'inTransit' | 'newBackorders';

/**
 * One row per day with one column per completed policy (the policy id is the key), ready for a line chart.
 * `normal` adds the no-disruption service level of the same scenario as an extra column.
 */
export function timelineRows(r: MultiDayResponse, field: DayField, normal = false): Record<string, number>[] {
  const rows: Record<string, number>[] = [];
  for (let day = 0; day < r.scenario.horizon; ++day) {
    const row: Record<string, number> = { day };
    for (const run of completedRuns(r)) row[run.policy] = run.days![day][field];
    if (normal && r.normalService.length === r.scenario.horizon) row.normal = r.normalService[day];
    rows.push(row);
  }
  return rows;
}

/** Days with at least one supplier or route down, as [first, last] stretches for shading. */
export function disruptedStretches(days: MultiDayDayDto[]): [number, number][] {
  const out: [number, number][] = [];
  for (const d of days) {
    if (d.activeDisruptions <= 0) continue;
    const last = out[out.length - 1];
    if (last && last[1] === d.day - 1) last[1] = d.day;
    else out.push([d.day, d.day]);
  }
  return out;
}

/** Plain words for the recovery result; a number is only shown when the definition was met. */
export function recoveryText(rec: MultiDayRecoveryDto | undefined, horizon: number): string {
  if (!rec || !rec.applicable) return 'no disruption';
  if (!rec.recovered || rec.days === null) return `not recovered within ${horizon} days`;
  if (rec.days === 0) return 'service never fell below normal';
  return `${rec.days} day${rec.days === 1 ? '' : 's'} (stable from day ${rec.recoveryDay})`;
}

/**
 * The network as it stood at the END of `day` under one policy, in the shape NetworkGraph draws: node cards show
 * stock (and backlog at customers), routes show units dispatched that day over daily capacity and units still moving.
 */
export function networkOnDay(r: MultiDayResponse, run: MultiDayRunDto, day: number): NetworkDto | null {
  const d = run.days?.[day];
  if (!d || !d.stockByNode || !d.nodeActive || !d.edgeUsable || !d.dispatchedByEdge || !d.inTransitByEdge || !d.backlogByNode) return null;
  const s = r.scenario;
  const nodes = s.nodes.map((n) => {
    const stock = d.stockByNode![n.id];
    const backlog = d.backlogByNode![n.id];
    const meta = n.type === 'supplier'
      ? `${n.supplyPerDay}/day · $${n.unitPurchaseCost}/u`
      : n.type === 'customer'
        ? `stock ${stock}${backlog > 0 ? ` · owed ${backlog}` : ''}`
        : `stock ${stock}`;
    return {
      id: n.id, name: n.name, type: n.type, supply: n.supplyPerDay, demand: 0, inventory: stock, isBackup: n.isBackup,
      activationCost: n.activationCost, unitPurchaseCost: n.unitPurchaseCost, shortagePenaltyPerUnit: n.shortagePenaltyPerUnit,
      active: d.nodeActive![n.id], unmetDemand: n.type === 'customer' ? backlog : undefined, meta,
    };
  });
  const edges = s.edges.map((e) => {
    const usable = d.edgeUsable![e.id];
    const flow = d.dispatchedByEdge![e.id];
    const capacity = d.capacityByEdge ? d.capacityByEdge[e.id] : usable ? e.capacityPerDay : 0;
    // A route is drawn as failed when it is not usable although both of its endpoints are up.
    const active = usable || !(d.nodeActive![e.from] && d.nodeActive![e.to]);
    return {
      id: e.id, from: e.from, to: e.to, fromName: s.nodes[e.from].name, toName: s.nodes[e.to].name,
      baseCapacity: e.capacityPerDay, capacity, costPerUnit: e.costPerUnit, transitTime: e.transitDays,
      reopenCost: e.reopenCost, active, usable, flow, utilisation: capacity > 0 ? flow / capacity : 0,
      inTransit: d.inTransitByEdge![e.id], transitDays: e.transitDays,
    };
  });
  return { nodes, edges };
}

/** Percentage above the clairvoyant lower bound, or null when the bound was not computed. */
export function gapToBound(r: MultiDayResponse, run: MultiDayRunDto): number | null {
  const bound = r.clairvoyant.cost;
  if (!r.clairvoyant.computed || bound === null || bound <= 0 || !run.totals) return null;
  return (100 * (run.totals.totalCost - bound)) / bound;
}
