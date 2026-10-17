import { memo, useEffect, useMemo } from 'react';
import {
  Background, BaseEdge, Controls, EdgeLabelRenderer, getBezierPath, Handle, MarkerType, Position,
  ReactFlow, ReactFlowProvider, useReactFlow,
  type Edge, type EdgeProps, type Node, type NodeProps,
} from '@xyflow/react';
import '@xyflow/react/dist/style.css';
import type { EdgeDto, NetworkDto, NodeDto } from '../types/api';
import type { Highlight } from '../hooks/useApp';
import { NODE_TYPE_COLOR, NODE_TYPE_LETTER } from '../utils/colors';
import { layoutNodes, suggestedHeight } from '../utils/layout';

export type Selection = { kind: 'node' | 'edge'; id: number } | null;
type Emphasis = 'normal' | 'selected' | 'hl' | 'dim';

type FacilityData = { dto: NodeDto; emphasis: Emphasis };
type RouteData = { dto: EdgeDto; emphasis: Emphasis; onSelect?: (id: number) => void };
type FacilityNodeType = Node<FacilityData, 'facility'>;
type RouteEdgeType = Edge<RouteData, 'route'>;

/** A route counts as a bottleneck once it carries at least 90% of its available capacity. */
export const BOTTLENECK_UTILISATION = 0.9;

function nodeMeta(n: NodeDto): string {
  if (n.meta !== undefined) return n.meta;
  switch (n.type) {
    case 'supplier':
    case 'factory':
      return `ships ${n.supplyDraw ?? 0} of ${n.supply} · $${n.unitPurchaseCost}/u`;
    case 'warehouse':
    case 'distribution_center':
      return n.inventory > 0 ? `stock ${n.inventory} (used ${n.inventoryDraw ?? 0})` : 'transshipment';
    case 'customer':
      return `served ${n.demand - (n.unmetDemand ?? 0)} of ${n.demand}`;
  }
}

function nodeStatus(n: NodeDto): { text: string; css: string } | null {
  if (!n.active && n.isBackup) return { text: `OFFLINE BACKUP · activate $${n.activationCost}`, css: 'offline' };
  if (!n.active) return { text: 'FAILED', css: 'failed' };
  if (n.isBackup) return { text: 'BACKUP · ACTIVE', css: '' };
  return null;
}

const FacilityNode = memo(function FacilityNode({ data }: NodeProps<FacilityNodeType>) {
  const { dto, emphasis } = data;
  const status = nodeStatus(dto);
  const short = dto.type === 'customer' && (dto.unmetDemand ?? 0) > 0;
  const classes = ['facility', status?.css ?? '', emphasis === 'selected' ? 'selected' : '', emphasis === 'hl' ? 'hl' : '', emphasis === 'dim' ? 'dim' : '']
    .filter(Boolean).join(' ');
  return (
    <div className={classes} title={`${dto.name} · ${dto.type.replace('_', ' ')}${status ? ' · ' + status.text : ''}\n${nodeMeta(dto)}`}>
      <Handle type="target" position={Position.Left} style={{ opacity: 0 }} />
      <div className="badge" style={{ background: NODE_TYPE_COLOR[dto.type] }}>{NODE_TYPE_LETTER[dto.type]}</div>
      <div style={{ minWidth: 0 }}>
        <div className="name">{dto.name}</div>
        <div className="meta">{nodeMeta(dto)}</div>
        {status && <div className="meta" style={{ color: status.css === 'failed' ? 'var(--bad)' : 'var(--text-2)', fontWeight: 600 }}>{status.text}</div>}
        {short && <div className="meta" style={{ color: 'var(--bad)', fontWeight: 600 }}>short {dto.unmetDemand}</div>}
      </div>
      <Handle type="source" position={Position.Right} style={{ opacity: 0 }} />
    </div>
  );
});

function routeStyle(dto: EdgeDto, emphasis: Emphasis) {
  const util = dto.utilisation ?? 0;
  const flow = dto.flow ?? 0;
  let stroke = 'var(--text-3)';
  let dash: string | undefined;
  if (!dto.active) { stroke = 'var(--bad)'; dash = '7 5'; }
  else if (!dto.usable) { stroke = 'var(--text-3)'; dash = '2 5'; }
  else if (util >= BOTTLENECK_UTILISATION && flow > 0) stroke = 'var(--series-2)';
  else if (flow > 0) stroke = 'var(--series-1)';
  let width = 1.5 + 4 * Math.min(util, 1);
  if (emphasis === 'hl') { stroke = 'var(--highlight)'; width = Math.max(width, 4.5); dash = undefined; }
  if (emphasis === 'selected') width += 1.5;
  return { stroke, dash, width, opacity: emphasis === 'dim' ? 0.18 : dto.usable || dto.active ? 1 : 0.7 };
}

const RouteEdge = memo(function RouteEdge(props: EdgeProps<RouteEdgeType>) {
  const { sourceX, sourceY, targetX, targetY, sourcePosition, targetPosition, data, markerEnd } = props;
  const [path, labelX, labelY] = getBezierPath({ sourceX, sourceY, targetX, targetY, sourcePosition, targetPosition });
  if (!data) return null;
  const { dto, emphasis, onSelect } = data;
  const style = routeStyle(dto, emphasis);
  const util = dto.utilisation ?? 0;
  const bottleneck = dto.usable && (dto.flow ?? 0) > 0 && util >= BOTTLENECK_UTILISATION;
  const moving = dto.inTransit !== undefined ? ` · ${dto.inTransit} moving` : '';
  const text = !dto.active ? '✕ failed' : !dto.usable ? '⊘ blocked' : `${dto.flow ?? 0}/${dto.capacity}${moving || ` · $${dto.costPerUnit}`}${bottleneck ? ' ▲' : ''}`;
  const transit = dto.transitDays !== undefined ? `${dto.transitDays} day${dto.transitDays === 1 ? '' : 's'}` : `${dto.transitTime} h`;
  const tip = `${dto.fromName} → ${dto.toName}  (route #${dto.id})\nflow ${dto.flow ?? 0} of ${dto.capacity} available (design ${dto.baseCapacity})\n`
    + (dto.inTransit !== undefined ? `${dto.inTransit} units in transit at the end of the day\n` : '')
    + `cost $${dto.costPerUnit}/unit · transit ${transit} · ${dto.active ? (dto.usable ? 'operating' : 'blocked by a failed endpoint') : 'FAILED'}`
    + (bottleneck ? '\nBOTTLENECK: at least 90% utilised' : '');
  return (
    <>
      <BaseEdge path={path} markerEnd={markerEnd}
        style={{ stroke: style.stroke, strokeWidth: style.width, strokeDasharray: style.dash, opacity: style.opacity }} />
      <EdgeLabelRenderer>
        <div className={`edge-label nodrag nopan ${!dto.active ? 'bad' : bottleneck ? 'hot' : ''} ${emphasis === 'hl' ? 'hl' : ''}`}
          title={tip} onClick={() => onSelect?.(dto.id)}
          style={{ left: labelX, top: labelY, opacity: emphasis === 'dim' ? 0.35 : 1, cursor: 'pointer', outline: emphasis === 'selected' ? '2px solid var(--accent)' : undefined }}>
          {text}
        </div>
      </EdgeLabelRenderer>
    </>
  );
});

const nodeTypes = { facility: FacilityNode };
const edgeTypes = { route: RouteEdge };

/** Legend strip shown under the canvas (not over it, so it never hides a node). */
function Legend() {
  return (
    <div className="graph-legend" aria-label="Legend">
      {(['supplier', 'factory', 'warehouse', 'distribution_center', 'customer'] as const).map((t) => (
        <span key={t}><span className="swatch" style={{ background: NODE_TYPE_COLOR[t] }} />{NODE_TYPE_LETTER[t]} {t.replace('_', ' ')}</span>
      ))}
      <span className="sep" />
      <span><i className="line" style={{ borderColor: 'var(--series-1)' }} />carrying flow</span>
      <span><i className="line" style={{ borderColor: 'var(--series-2)' }} />bottleneck ▲ (≥ 90%)</span>
      <span><i className="line" style={{ borderColor: 'var(--bad)', borderTopStyle: 'dashed' }} />failed ✕</span>
      <span><i className="line" style={{ borderColor: 'var(--highlight)' }} />highlighted result</span>
      <span className="faint">labels: flow/capacity · cost</span>
    </div>
  );
}

interface GraphProps {
  network: NetworkDto;
  selected?: Selection;
  onSelect?: (selection: Selection) => void;
  highlight?: Highlight;
  /** Pixels, or 'auto' to size the canvas to the layout. */
  height?: number | 'auto';
  legend?: boolean;
  /** Changes when the view should re-fit (a different scenario or size). */
  fitKey?: string;
}

function Graph({ network, selected = null, onSelect, highlight, height = 'auto', legend = true, fitKey }: GraphProps) {
  const { fitView } = useReactFlow();
  const pixels = height === 'auto' ? suggestedHeight(network.nodes) : height;
  const anyHighlight = (highlight?.dimOthers ?? true) && (highlight?.nodeIds.length ?? 0) + (highlight?.edgeIds.length ?? 0) > 0;

  const nodes: FacilityNodeType[] = useMemo(() => {
    const positions = layoutNodes(network.nodes);
    const hlNodes = new Set(highlight?.nodeIds ?? []);
    return network.nodes.map((dto) => {
      const emphasis: Emphasis = selected?.kind === 'node' && selected.id === dto.id ? 'selected'
        : hlNodes.has(dto.id) ? 'hl' : anyHighlight ? 'dim' : 'normal';
      return { id: `n${dto.id}`, type: 'facility', position: positions.get(dto.id) ?? { x: 0, y: 0 }, data: { dto, emphasis }, draggable: false };
    });
  }, [network, selected, highlight, anyHighlight]);

  const edges: RouteEdgeType[] = useMemo(() => {
    const hlEdges = new Set(highlight?.edgeIds ?? []);
    return network.edges.map((dto) => {
      const emphasis: Emphasis = selected?.kind === 'edge' && selected.id === dto.id ? 'selected'
        : hlEdges.has(dto.id) ? 'hl' : anyHighlight ? 'dim' : 'normal';
      return {
        id: `e${dto.id}`, source: `n${dto.from}`, target: `n${dto.to}`, type: 'route',
        data: { dto, emphasis, onSelect: onSelect ? (id: number) => onSelect({ kind: 'edge', id }) : undefined },
        markerEnd: { type: MarkerType.ArrowClosed, width: 12, height: 12, color: 'var(--text-3)' },
      };
    });
  }, [network, selected, highlight, anyHighlight, onSelect]);

  useEffect(() => {
    const handle = window.setTimeout(() => fitView({ padding: 0.06, duration: 250 }), 60);
    return () => window.clearTimeout(handle);
  }, [fitKey, fitView]);

  return (
    <div>
      <div className="graph-box" style={{ height: pixels }} data-testid="network-graph">
        <ReactFlow<FacilityNodeType, RouteEdgeType>
          nodes={nodes} edges={edges} nodeTypes={nodeTypes} edgeTypes={edgeTypes}
          fitView minZoom={0.15} maxZoom={2}
          nodesDraggable={false} nodesConnectable={false} elementsSelectable
          onNodeClick={(_, node) => onSelect?.({ kind: 'node', id: node.data.dto.id })}
          onEdgeClick={(_, edge) => onSelect?.({ kind: 'edge', id: edge.data!.dto.id })}
          onPaneClick={() => onSelect?.(null)}
          proOptions={{ hideAttribution: true }}>
          <Background gap={24} />
          <Controls showInteractive={false} />
        </ReactFlow>
      </div>
      {legend && <Legend />}
    </div>
  );
}

/** Interactive directed graph: zoom, pan, click a node or route, tooltips, failed/bottleneck/highlight styling. */
export function NetworkGraph(props: GraphProps) {
  return (
    <ReactFlowProvider>
      <Graph {...props} />
    </ReactFlowProvider>
  );
}
