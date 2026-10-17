import type { Highlight } from '../hooks/useApp';
import type { DijkstraResult, MaxFlowResult, NetworkDto, RunResponse, TraversalResult } from '../types/api';

export interface LabView { network: NetworkDto; highlight: Highlight }

/**
 * Decides what the Lab's graph shows for a run: which network (current, or one with the run's flows) and
 * which nodes/routes to emphasise (visit order, shortest path, minimum cut, recovery actions taken).
 */
export function labView(run: RunResponse, current: NetworkDto): LabView {
  const result = run.result;
  switch (run.algorithm) {
    case 'bfs':
    case 'dfs': {
      const r = result as TraversalResult;
      return { network: current, highlight: { nodeIds: r.visitOrder.map((n) => n.id), edgeIds: r.treeEdgeIds, label: `${run.title} from ${r.source.name}` } };
    }
    case 'dijkstra': {
      const r = result as DijkstraResult;
      if (r.target && r.reachable) {
        return { network: current, highlight: { nodeIds: (r.pathNodes ?? []).map((n) => n.id), edgeIds: r.pathEdgeIds ?? [], label: `Cheapest path ${r.source.name} → ${r.target.name}` } };
      }
      return { network: current, highlight: { nodeIds: [], edgeIds: r.target ? [] : r.treeEdgeIds, label: r.target ? 'No usable path' : `Shortest-path tree from ${r.source.name}` } };
    }
    case 'max_flow': {
      const r = result as MaxFlowResult;
      return { network: r.network, highlight: { nodeIds: [], edgeIds: r.minCutEdgeIds, label: 'Minimum cut (bottleneck routes)', dimOthers: false } };
    }
    default: {
      const plan = run.plan;
      if (!plan) return { network: current, highlight: { nodeIds: [], edgeIds: [], label: '' } };
      const nodeIds = plan.actions.filter((a) => a.type === 'activate_supplier').map((a) => a.target);
      const edgeIds = plan.actions.filter((a) => a.type === 'reopen_route').map((a) => a.target);
      return { network: plan.network, highlight: { nodeIds, edgeIds, label: plan.actions.length ? 'Recovery actions taken' : '', dimOthers: false } };
    }
  }
}
