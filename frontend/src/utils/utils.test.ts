import { describe, expect, it } from 'vitest';
import type { EdgeDto, NodeDto, NodeType, Snapshot } from '../types/api';
import { buildEvent, needsPercent, targetsFor } from './disruptionOptions';
import { memory, milliseconds, percent, titleCase } from './format';
import { COLUMN_WIDTH, layoutNodes, ROW_HEIGHT, suggestedHeight } from './layout';

const node = (id: number, name: string, type: NodeType, extra: Partial<NodeDto> = {}): NodeDto => ({
  id, name, type, supply: 0, demand: 0, inventory: 0, isBackup: false, activationCost: 0,
  unitPurchaseCost: 0, shortagePenaltyPerUnit: 0, active: true, ...extra,
});
const edge = (id: number, from: NodeDto, to: NodeDto, extra: Partial<EdgeDto> = {}): EdgeDto => ({
  id, from: from.id, to: to.id, fromName: from.name, toName: to.name, baseCapacity: 10, capacity: 10,
  costPerUnit: 1, transitTime: 1, reopenCost: 0, active: true, usable: true, ...extra,
});

const S1 = node(0, 'S1', 'supplier'), S2 = node(1, 'S2', 'supplier', { active: false });
const B1 = node(2, 'B1', 'supplier', { isBackup: true, active: false });
const W1 = node(3, 'W1', 'warehouse'), C1 = node(4, 'C1', 'customer', { demand: 20 });
const snapshot = (edges: EdgeDto[]): Snapshot => ({
  scenario: { name: 't', seed: 1 }, network: { nodes: [S1, S2, B1, W1, C1], edges },
  metrics: {} as Snapshot['metrics'], flowSource: '', recoveryExecuted: false, appliedEvents: [], pendingEvents: [],
});

describe('layoutNodes', () => {
  const positions = layoutNodes([C1, W1, B1, S2, S1]);

  it('puts columns in supply-chain order from left to right', () => {
    expect(positions.get(S1.id)!.x).toBe(0);
    expect(positions.get(W1.id)!.x).toBe(COLUMN_WIDTH);
    expect(positions.get(C1.id)!.x).toBe(2 * COLUMN_WIDTH);   // no factory/DC column when none exist
  });

  it('never places two nodes on the same spot', () => {
    const seen = new Set([...positions.values()].map((p) => `${p.x},${p.y}`));
    expect(seen.size).toBe(positions.size);
  });

  it('lists backup suppliers below regular ones and centres shorter columns', () => {
    expect(positions.get(B1.id)!.y).toBeGreaterThan(positions.get(S2.id)!.y);
    expect(positions.get(W1.id)!.y).toBe(ROW_HEIGHT);          // 1 node in a column next to a 3-node column
  });

  it('is deterministic regardless of input order and handles an empty network', () => {
    const again = layoutNodes([S1, S2, B1, W1, C1]);
    expect([...again.entries()].sort()).toEqual([...positions.entries()].sort());
    expect(layoutNodes([]).size).toBe(0);
  });
});

describe('suggestedHeight', () => {
  it('grows with the tallest column but stays within bounds', () => {
    expect(suggestedHeight([])).toBe(380);
    expect(suggestedHeight([S1, S2, B1, W1, C1])).toBe(Math.max(380, 3 * ROW_HEIGHT + 160));
    const many = Array.from({ length: 40 }, (_, i) => node(100 + i, `S${i}`, 'supplier'));
    expect(suggestedHeight(many)).toBe(720);
  });
});

describe('disruption options', () => {
  const healthy = edge(0, S1, W1);
  const failed = edge(1, W1, C1, { active: false, usable: false });
  const reduced = edge(2, S1, C1, { capacity: 4 });
  const snap = snapshot([healthy, failed, reduced]);

  it('offers only actions the engine would accept', () => {
    expect(targetsFor('supplier_failure', snap).map((t) => t.value)).toEqual(['S1']);       // S2 and B1 are already offline
    expect(targetsFor('route_failure', snap).map((t) => t.value)).toEqual([0, 2]);          // route 1 is already failed
    expect(targetsFor('restore_route', snap).map((t) => t.value)).toEqual([1, 2]);          // failed or reduced routes
    expect(targetsFor('restore_node', snap).map((t) => t.value)).toEqual(['S2']);           // backups need a paid action, not a restore
    expect(targetsFor('demand_spike', snap).map((t) => t.value)).toEqual(['C1']);
    expect(targetsFor('node_failure', snap).map((t) => t.value)).toEqual(['S1', 'W1']);     // customers cannot be shut down here
  });

  it('builds the request body the server expects', () => {
    expect(buildEvent('supplier_failure', 'S1', 0)).toEqual({ type: 'supplier_failure', node: 'S1' });
    expect(buildEvent('route_failure', 3, 0)).toEqual({ type: 'route_failure', edge: 3 });
    expect(buildEvent('capacity_reduction', 2, 40)).toEqual({ type: 'capacity_reduction', edge: 2, percentRemaining: 40 });
    expect(buildEvent('demand_spike', 'C1', 50)).toEqual({ type: 'demand_spike', node: 'C1', percentIncrease: 50 });
    expect(needsPercent('capacity_reduction') && needsPercent('demand_spike') && !needsPercent('route_failure')).toBe(true);
  });
});

describe('formatting', () => {
  it('never shows an unmeasured memory figure as zero', () => {
    expect(memory(null)).toBe('not measured');
    expect(memory(undefined)).toBe('not measured');
    expect(memory(0)).toBe('0 B');
    expect(memory(2048)).toBe('2.0 KB');
    expect(memory(3 * 1024 * 1024)).toBe('3.00 MB');
  });
  it('formats ratios, times and ids', () => {
    expect(percent(0.5)).toBe('50.0%');
    expect(milliseconds(0.001)).toBe('<0.01 ms');
    expect(milliseconds(12.34)).toBe('12.3 ms');
    expect(titleCase('bottleneck_aware_lambda0')).toBe('Bottleneck Aware Lambda0');
  });
});
