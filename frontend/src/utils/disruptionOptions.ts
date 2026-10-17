import type { EdgeDto, NodeDto, Snapshot } from '../types/api';

export type EventKind =
  | 'supplier_failure' | 'node_failure' | 'route_failure' | 'capacity_reduction'
  | 'demand_spike' | 'restore_node' | 'restore_route';

export interface TargetOption { value: number | string; label: string }

export const EVENT_LABELS: Record<EventKind, string> = {
  supplier_failure: 'Shut down a supplier',
  node_failure: 'Shut down a facility',
  route_failure: 'Disable a route',
  capacity_reduction: 'Reduce route capacity',
  demand_spike: 'Increase customer demand',
  restore_node: 'Restore a failed node',
  restore_route: 'Restore a route',
};

const edgeLabel = (e: EdgeDto) => `${e.fromName} → ${e.toName}  (route #${e.id}, cap ${e.capacity}/${e.baseCapacity})`;
const nodeLabel = (n: NodeDto) => `${n.name} (${n.type.replace('_', ' ')})`;

function pick<T>(items: T[], keep: (t: T) => boolean, label: (t: T) => string, value: (t: T) => number | string): TargetOption[] {
  return items.filter(keep).map((t) => ({ value: value(t), label: label(t) }));
}

/**
 * Which targets the engine would accept right now for each event kind. The Disruption Simulator only
 * offers these, so a user cannot build an event the server would reject as "nothing to restore" etc.
 */
export function targetsFor(kind: EventKind, snapshot: Snapshot): TargetOption[] {
  const { nodes, edges } = snapshot.network;
  switch (kind) {
    case 'supplier_failure':
      return pick(nodes, (n) => n.type === 'supplier' && n.active, nodeLabel, (n) => n.name);
    case 'node_failure':
      return pick(nodes, (n) => n.active && n.type !== 'customer', nodeLabel, (n) => n.name);
    case 'route_failure':
      return pick(edges, (e) => e.active, edgeLabel, (e) => e.id);
    case 'capacity_reduction':
      return pick(edges, (e) => e.active && e.baseCapacity > 0, edgeLabel, (e) => e.id);
    case 'demand_spike':
      return pick(nodes, (n) => n.type === 'customer', (n) => `${n.name} (demand ${n.demand})`, (n) => n.name);
    case 'restore_node':
      // Backup suppliers are brought online by a paid recovery action, not restored for free.
      return pick(nodes, (n) => !n.active && !n.isBackup, nodeLabel, (n) => n.name);
    case 'restore_route':
      return pick(edges, (e) => !e.active || e.capacity < e.baseCapacity, edgeLabel, (e) => e.id);
  }
}

export const needsPercent = (kind: EventKind) => kind === 'capacity_reduction' || kind === 'demand_spike';

/** Builds the JSON body for POST /api/disruption/apply. */
export function buildEvent(kind: EventKind, target: number | string, percent: number): Record<string, unknown> {
  const isEdge = kind === 'route_failure' || kind === 'capacity_reduction' || kind === 'restore_route';
  const event: Record<string, unknown> = { type: kind, [isEdge ? 'edge' : 'node']: target };
  if (kind === 'capacity_reduction') event.percentRemaining = percent;
  if (kind === 'demand_spike') event.percentIncrease = percent;
  return event;
}
