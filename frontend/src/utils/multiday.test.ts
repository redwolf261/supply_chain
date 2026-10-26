import { describe, expect, it } from 'vitest';
import type { MultiDayDayDto, MultiDayResponse, MultiDayRunDto } from '../types/api';
import { completedRuns, disruptedStretches, gapToBound, networkOnDay, recoveryText, timelineRows } from './multiday';

// A hand-made 3-day response with the exact shape the C++ server returns (backend/src/multiday/MultiDayJson.cpp).
// It only tests the transformations; the page itself shows nothing but server output.
const day = (d: number, extra: Partial<MultiDayDayDto> = {}): MultiDayDayDto => ({
  day: d, demand: 10, due: 10, served: 10, newBackorders: 0, backlog: 0, stock: 5, inTransit: 20, serviceLevel: 1,
  cost: { purchase: 10, transport: 5, holding: 1, backlog: 0, actions: 0, total: 16 }, cumulativeCost: 16 * (d + 1),
  actions: [], events: [], activeDisruptions: 0, decisionMs: 0.1, planningMs: 0.2, plannedSaturatedShare: 0.5,
  stockByNode: [0, 5, 0], backlogByNode: [0, 0, d], dispatchedByEdge: [10, 10], inTransitByEdge: [10, 10], capacityByEdge: [10, 6],
  nodeActive: [d !== 1, true, true], edgeUsable: [d !== 1, true], ...extra,
});
const run = (policy: MultiDayRunDto['policy'], cost: number, days: MultiDayDayDto[]): MultiDayRunDto => ({
  policy, skipped: false, days, recovery: { applicable: true, recovered: true, days: 1, recoveryDay: 2 },
  totals: { totalCost: cost, settlement: 0, actionCost: 0, actions: 0, totalDemand: 30, totalServed: 30, finalBacklog: 0,
    finalInTransit: 20, finalStock: 5, fillRate: 1, decisionMs: 0.3, runtimeMs: 1, feasible: true },
});
const response: MultiDayResponse = {
  scenario: {
    name: 't', seed: 1, horizon: 3,
    nodes: [
      { id: 0, name: 'S1', type: 'supplier', isBackup: false, active: true, supplyPerDay: 10, openingStock: 0, unitPurchaseCost: 2, holdingCostPerUnit: 0, shortagePenaltyPerUnit: 0, activationCost: 0 },
      { id: 1, name: 'W1', type: 'warehouse', isBackup: false, active: true, supplyPerDay: 0, openingStock: 5, unitPurchaseCost: 0, holdingCostPerUnit: 1, shortagePenaltyPerUnit: 0, activationCost: 0 },
      { id: 2, name: 'C1', type: 'customer', isBackup: false, active: true, supplyPerDay: 0, openingStock: 0, unitPurchaseCost: 0, holdingCostPerUnit: 1, shortagePenaltyPerUnit: 50, activationCost: 0 },
    ],
    edges: [
      { id: 0, from: 0, to: 1, capacityPerDay: 10, costPerUnit: 1, transitDays: 2, reopenCost: 5, active: true },
      { id: 1, from: 1, to: 2, capacityPerDay: 10, costPerUnit: 1, transitDays: 1, reopenCost: 5, active: true },
    ],
    events: [], demandPerDay: [10, 10, 10], firstDisruptionDay: 1, initialInTransit: 30, supplyLeadDays: 3, planningWindowDays: 7,
    forecast: 'persistence', activationLeadDays: 1, repairLeadDays: 1, serviceThreshold: 0.95, stabilityDays: 3,
  },
  normalService: [1, 1, 1],
  policies: [
    run('greedy', 120, [day(0), day(1, { activeDisruptions: 1, serviceLevel: 0.5 }), day(2)]),
    { policy: 'rolling_exact', skipped: true, skipReason: 'too many candidates' },
    run('none', 150, [day(0), day(1, { activeDisruptions: 1, serviceLevel: 0.4 }), day(2)]),
  ],
  clairvoyant: { computed: true, reason: '', cost: 100, candidates: 1, runtimeMs: 1, note: '', actions: [] },
  lowestTotalCost: 'greedy', totalRuntimeMs: 5,
  limits: { minHorizon: 5, maxHorizon: 30, maxNodes: 60, maxLookaheadDays: 12, maxExhaustiveCandidates: 10 },
  note: '',
};

describe('multi-day view helpers', () => {
  it('drops skipped runs and keeps one column per completed policy', () => {
    expect(completedRuns(response).map((r) => r.policy)).toEqual(['greedy', 'none']);
    const rows = timelineRows(response, 'serviceLevel', true);
    expect(rows).toHaveLength(3);
    expect(rows[1]).toEqual({ day: 1, greedy: 0.5, none: 0.4, normal: 1 });
    expect(timelineRows(response, 'backlog')[0]).toEqual({ day: 0, greedy: 0, none: 0 });   // no 'normal' unless asked
  });

  it('groups consecutive disrupted days into stretches', () => {
    const days = [0, 1, 2, 3, 4, 5].map((d) => day(d, { activeDisruptions: [0, 2, 1, 0, 1, 0][d] }));
    expect(disruptedStretches(days)).toEqual([[1, 2], [4, 4]]);
    expect(disruptedStretches([day(0)])).toEqual([]);
  });

  it('reports a recovery number only when the definition was met', () => {
    expect(recoveryText({ applicable: false, recovered: false, days: null, recoveryDay: null }, 20)).toBe('no disruption');
    expect(recoveryText({ applicable: true, recovered: false, days: null, recoveryDay: null }, 20)).toBe('not recovered within 20 days');
    expect(recoveryText({ applicable: true, recovered: true, days: 0, recoveryDay: 2 }, 20)).toBe('service never fell below normal');
    expect(recoveryText({ applicable: true, recovered: true, days: 4, recoveryDay: 6 }, 20)).toBe('4 days (stable from day 6)');
  });

  it('builds the network of one day from the snapshot', () => {
    const greedy = completedRuns(response)[0];
    const net = networkOnDay(response, greedy, 1)!;
    expect(net.nodes[0].active).toBe(false);                       // S1 down on day 1
    expect(net.nodes[2].meta).toBe('stock 0 · owed 1');
    expect(net.edges[0].usable).toBe(false);
    expect(net.edges[0].active).toBe(true);                        // blocked by its failed endpoint, not failed itself
    expect(net.edges[1].capacity).toBe(6);                         // today's available capacity, not the design value
    expect(net.edges[1].utilisation).toBeCloseTo(10 / 6);
    expect(net.edges[1].inTransit).toBe(10);
    expect(net.edges[1].transitDays).toBe(1);
    expect(networkOnDay(response, greedy, 9)).toBeNull();
  });

  it('computes the gap to the lower bound only when the bound exists', () => {
    const [greedy, none] = completedRuns(response);
    expect(gapToBound(response, greedy)).toBeCloseTo(20);
    expect(gapToBound(response, none)).toBeCloseTo(50);
    expect(gapToBound({ ...response, clairvoyant: { ...response.clairvoyant, computed: false, cost: null } }, greedy)).toBeNull();
  });
});
