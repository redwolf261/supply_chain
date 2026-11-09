import { useEffect, useMemo, useState, useRef } from 'react';
import { MapContainer, TileLayer, Marker, Popup, Polyline, Tooltip, useMap } from 'react-leaflet';
import L from 'leaflet';
import 'leaflet/dist/leaflet.css';
import type { EdgeDto, NetworkDto, NodeDto } from '../types/api';
import type { Highlight } from '../hooks/useApp';
import type { Selection } from './NetworkGraph';
import { NODE_TYPE_COLOR, NODE_TYPE_LETTER } from '../utils/colors';
import { percent, titleCase } from '../utils/format';
import { BOTTLENECK_UTILISATION } from './NetworkGraph';

export interface GeoLocation {
  lat: number;
  lng: number;
  city: string;
}

// Canonical logistics hub coordinates across major freight corridors
export const CANONICAL_HUBS: Record<string, GeoLocation> = {
  // Suppliers / Manufacturing
  'S1': { lat: 22.4707, lng: 70.0577, city: 'Jamnagar' },
  'S2': { lat: 18.5204, lng: 73.8567, city: 'Pune' },
  'S3-backup': { lat: 13.0827, lng: 80.2707, city: 'Chennai' },
  'S3': { lat: 13.0827, lng: 80.2707, city: 'Chennai' },
  'S4': { lat: 22.8046, lng: 86.2029, city: 'Jamshedpur' },
  'S5': { lat: 21.1702, lng: 72.8311, city: 'Surat' },
  'S6': { lat: 17.6868, lng: 83.2185, city: 'Visakhapatnam' },

  // Warehouses
  'W1': { lat: 21.1458, lng: 79.0882, city: 'Nagpur (Central Hub)' },
  'W2': { lat: 17.3850, lng: 78.4867, city: 'Hyderabad' },
  'W3': { lat: 22.7196, lng: 75.8577, city: 'Indore' },
  'W4': { lat: 23.2599, lng: 77.4126, city: 'Bhopal' },

  // Distribution Centers
  'D1': { lat: 12.9716, lng: 77.5946, city: 'Bangalore DC' },
  'D2': { lat: 19.0760, lng: 72.8777, city: 'Mumbai DC' },
  'D3': { lat: 28.7041, lng: 77.1025, city: 'Delhi NCR DC' },
  'D4': { lat: 22.5726, lng: 88.3639, city: 'Kolkata DC' },

  // Customers
  'C1': { lat: 23.0225, lng: 72.5714, city: 'Ahmedabad' },
  'C2': { lat: 9.9312, lng: 76.2673, city: 'Kochi' },
  'C3': { lat: 11.0168, lng: 76.9558, city: 'Coimbatore' },
  'C4': { lat: 26.9124, lng: 75.7873, city: 'Jaipur' },
  'C5': { lat: 26.8467, lng: 80.9462, city: 'Lucknow' },
  'C6': { lat: 30.7333, lng: 76.7794, city: 'Chandigarh' },
  'C7': { lat: 25.5941, lng: 85.1376, city: 'Patna' },
  'C8': { lat: 19.8762, lng: 75.3433, city: 'Aurangabad' },
  'C9': { lat: 15.3647, lng: 75.1240, city: 'Hubli' },
  'C10': { lat: 12.2958, lng: 76.6394, city: 'Mysore' },
};

function getFallbackCoordinates(node: NodeDto, index: number, totalInType: number): GeoLocation {
  const seed = (node.id * 17 + index * 31) % 1000;
  const jitterLat = ((seed % 100) - 50) * 0.04;
  const jitterLng = (((seed * 7) % 100) - 50) * 0.05;

  switch (node.type) {
    case 'supplier':
    case 'factory': {
      const baseLats = [21.5, 23.0, 19.0, 13.5, 22.5, 17.5];
      const baseLngs = [70.5, 72.5, 74.0, 80.0, 86.0, 83.0];
      const idx = index % baseLats.length;
      return { lat: baseLats[idx] + jitterLat, lng: baseLngs[idx] + jitterLng, city: `${node.name} (Industrial Hub)` };
    }
    case 'warehouse': {
      const baseLats = [21.2, 17.4, 22.8, 23.3, 20.0];
      const baseLngs = [79.1, 78.5, 75.9, 77.4, 77.0];
      const idx = index % baseLats.length;
      return { lat: baseLats[idx] + jitterLat, lng: baseLngs[idx] + jitterLng, city: `${node.name} (Logistics Hub)` };
    }
    case 'distribution_center': {
      const baseLats = [13.0, 19.1, 28.6, 22.6, 17.4];
      const baseLngs = [77.6, 73.0, 77.2, 88.4, 78.5];
      const idx = index % baseLats.length;
      return { lat: baseLats[idx] + jitterLat, lng: baseLngs[idx] + jitterLng, city: `${node.name} (Fulfillment Hub)` };
    }
    case 'customer':
    default: {
      const angle = (index / Math.max(1, totalInType)) * 2 * Math.PI;
      const radius = 5.0 + ((seed % 30) * 0.1);
      return {
        lat: 20.5 + Math.sin(angle) * radius + jitterLat,
        lng: 78.0 + Math.cos(angle) * radius + jitterLng,
        city: `${node.name} (Demand Center)`,
      };
    }
  }
}

export function getNodeGeo(node: NodeDto, allNodes: NodeDto[]): GeoLocation {
  if (CANONICAL_HUBS[node.name]) {
    return CANONICAL_HUBS[node.name];
  }
  const sameType = allNodes.filter((n) => n.type === node.type);
  const index = sameType.findIndex((n) => n.id === node.id);
  return getFallbackCoordinates(node, Math.max(0, index), sameType.length);
}

function createCustomIcon(node: NodeDto, isSelected: boolean, isHighlighted: boolean, isTraced: boolean) {
  const color = NODE_TYPE_COLOR[node.type];
  const letter = NODE_TYPE_LETTER[node.type];
  const isFailed = !node.active;
  const isBackup = node.isBackup;
  const size = isSelected || isHighlighted || isTraced ? 38 : 30;

  const html = `
    <div style="
      position: relative;
      width: ${size}px;
      height: ${size}px;
      border-radius: 50%;
      background: ${isFailed ? 'var(--bad, #dc2626)' : color};
      border: 3px solid ${isTraced ? '#fbbf24' : isSelected ? '#f59e0b' : isHighlighted ? '#38bdf8' : '#ffffff'};
      box-shadow: ${isTraced ? '0 0 14px rgba(251, 191, 36, 0.85)' : '0 4px 12px rgba(0,0,0,0.35)'};
      display: flex;
      align-items: center;
      justify-content: center;
      color: white;
      font-weight: 700;
      font-size: ${size * 0.42}px;
      font-family: ui-sans-serif, system-ui, sans-serif;
      cursor: pointer;
      transition: transform 0.15s ease;
      ${isBackup && !node.active ? 'border-style: dashed; opacity: 0.75;' : ''}
    ">
      ${letter}
      ${isFailed ? `<div style="position: absolute; top: -4px; right: -4px; background: #991b1b; color: white; border-radius: 50%; width: 14px; height: 14px; font-size: 10px; display: grid; place-items: center;">✕</div>` : ''}
      <div style="
        position: absolute;
        bottom: -18px;
        left: 50%;
        transform: translateX(-50%);
        background: rgba(15, 23, 42, 0.88);
        color: #f8fafc;
        padding: 1px 6px;
        border-radius: 4px;
        font-size: 10px;
        white-space: nowrap;
        pointer-events: none;
        border: 1px solid rgba(255,255,255,0.15);
      ">
        ${node.name}
      </div>
    </div>
  `;

  return L.divIcon({
    html,
    className: 'custom-geo-node-icon',
    iconSize: [size, size],
    iconAnchor: [size / 2, size / 2],
    popupAnchor: [0, -size / 2],
  });
}

function MapAutoBounds({ nodes }: { nodes: NodeDto[] }) {
  const map = useMap();
  useEffect(() => {
    if (nodes.length === 0) return;
    const bounds = L.latLngBounds(nodes.map((n) => {
      const geo = getNodeGeo(n, nodes);
      return [geo.lat, geo.lng];
    }));
    map.fitBounds(bounds, { padding: [40, 40], maxZoom: 8 });
  }, [map, nodes]);
  return null;
}

export interface TracedJourney {
  targetNode: NodeDto;
  nodeIds: Set<number>;
  edgeIds: Set<number>;
  stages: { from: NodeDto; to: NodeDto; edge: EdgeDto }[];
  totalTransitHours: number;
  totalCostPerUnit: number;
}

/** Recursively backtraces or forward-traces all active shipments (flow > 0) passing through a given node */
export function traceNodeJourney(startNode: NodeDto, network: NetworkDto): TracedJourney {
  const nodeMap = new Map(network.nodes.map((n) => [n.id, n]));
  const nodeIds = new Set<number>([startNode.id]);
  const edgeIds = new Set<number>();
  const stages: { from: NodeDto; to: NodeDto; edge: EdgeDto }[] = [];
  let totalTransitHours = 0;
  let totalCostPerUnit = 0;

  // 1. Backward trace (incoming flow for customers and intermediaries)
  const backQueue = [startNode.id];
  const visitedBack = new Set<number>([startNode.id]);

  while (backQueue.length > 0) {
    const currId = backQueue.shift()!;
    const incomingEdges = network.edges.filter((e) => e.to === currId && (e.flow ?? 0) > 0);
    for (const e of incomingEdges) {
      if (!edgeIds.has(e.id)) {
        edgeIds.add(e.id);
        const fromNode = nodeMap.get(e.from);
        const toNode = nodeMap.get(e.to);
        if (fromNode && toNode) {
          stages.unshift({ from: fromNode, to: toNode, edge: e });
          totalTransitHours += e.transitTime;
          totalCostPerUnit += e.costPerUnit;
        }
      }
      if (!visitedBack.has(e.from)) {
        visitedBack.add(e.from);
        nodeIds.add(e.from);
        backQueue.push(e.from);
      }
    }
  }

  // 2. Forward trace (outgoing flow if starting at a supplier / warehouse)
  const forwardQueue = [startNode.id];
  const visitedFwd = new Set<number>([startNode.id]);

  while (forwardQueue.length > 0) {
    const currId = forwardQueue.shift()!;
    const outgoingEdges = network.edges.filter((e) => e.from === currId && (e.flow ?? 0) > 0);
    for (const e of outgoingEdges) {
      if (!edgeIds.has(e.id)) {
        edgeIds.add(e.id);
        const fromNode = nodeMap.get(e.from);
        const toNode = nodeMap.get(e.to);
        if (fromNode && toNode) {
          stages.push({ from: fromNode, to: toNode, edge: e });
          totalTransitHours += e.transitTime;
          totalCostPerUnit += e.costPerUnit;
        }
      }
      if (!visitedFwd.has(e.to)) {
        visitedFwd.add(e.to);
        nodeIds.add(e.to);
        forwardQueue.push(e.to);
      }
    }
  }

  return {
    targetNode: startNode,
    nodeIds,
    edgeIds,
    stages,
    totalTransitHours,
    totalCostPerUnit,
  };
}

export function GeographicMap({
  network,
  selected,
  onSelect,
  highlight,
  onApplyEvent,
  busy = false,
}: {
  network: NetworkDto;
  selected: Selection;
  onSelect: (sel: Selection) => void;
  highlight: Highlight;
  onApplyEvent?: (event: Record<string, unknown>) => void;
  busy?: boolean;
}) {
  const nodeMap = useMemo(() => new Map(network.nodes.map((n) => [n.id, n])), [network.nodes]);

  // Traced Delivery Journey State (Option 2)
  const [tracedJourney, setTracedJourney] = useState<TracedJourney | null>(null);

  // Multi-Day Playback Simulation State (Option 4)
  const [playbackActive, setPlaybackActive] = useState(false);
  const [simDay, setSimDay] = useState(0);
  const [isPlaying, setIsPlaying] = useState(false);
  const playTimerRef = useRef<number | null>(null);

  const geoCoordinates = useMemo(() => {
    const coords = new Map<number, GeoLocation>();
    network.nodes.forEach((n) => {
      coords.set(n.id, getNodeGeo(n, network.nodes));
    });
    return coords;
  }, [network.nodes]);

  // Handle Play/Pause timer for multi-day playback
  useEffect(() => {
    if (isPlaying) {
      playTimerRef.current = window.setInterval(() => {
        setSimDay((d) => (d >= 20 ? 0 : d + 1));
      }, 1200);
    } else if (playTimerRef.current) {
      clearInterval(playTimerRef.current);
    }
    return () => {
      if (playTimerRef.current) clearInterval(playTimerRef.current);
    };
  }, [isPlaying]);

  const handleTraceNode = (node: NodeDto) => {
    const journey = traceNodeJourney(node, network);
    setTracedJourney(journey);
    onSelect({ kind: 'node', id: node.id });
  };

  const handleClearJourney = () => {
    setTracedJourney(null);
  };

  return (
    <div className="card" style={{ padding: 0, overflow: 'hidden', height: 620, position: 'relative' }}>
      {/* Map Control Overlay Header */}
      <div style={{
        position: 'absolute',
        top: 12,
        left: 12,
        right: 12,
        zIndex: 1000,
        display: 'flex',
        justifyContent: 'space-between',
        alignItems: 'center',
        background: 'rgba(15, 23, 42, 0.90)',
        backdropFilter: 'blur(10px)',
        padding: '8px 14px',
        borderRadius: 8,
        border: '1px solid rgba(255,255,255,0.18)',
        color: 'white',
        fontSize: 12,
        boxShadow: '0 4px 14px rgba(0,0,0,0.35)',
        flexWrap: 'wrap',
        gap: 8,
      }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
          <span style={{ fontWeight: 700, color: '#38bdf8' }}>🗺️ Geographic Freight Corridor Map</span>
          <span className="pill" style={{ background: '#1e293b', color: '#94a3b8', fontSize: 11 }}>OpenStreetMap Corridors</span>
          {tracedJourney && (
            <span className="pill warn" style={{ fontSize: 11 }}>
              Tracing: {tracedJourney.targetNode.name}
            </span>
          )}
        </div>

        <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
          {tracedJourney && (
            <button className="btn small" onClick={handleClearJourney}>
              ✕ Reset Full View
            </button>
          )}
          <button
            className={`btn small ${playbackActive ? 'primary' : ''}`}
            onClick={() => {
              setPlaybackActive(!playbackActive);
              if (playbackActive) setIsPlaying(false);
            }}
          >
            {playbackActive ? 'Close Playback' : '▶ Multi-Day Playback'}
          </button>
        </div>
      </div>

      <MapContainer
        center={[21.0, 78.0]}
        zoom={5}
        style={{ height: '100%', width: '100%', background: '#0b1120' }}
        scrollWheelZoom={true}
      >
        <TileLayer
          attribution='&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a> contributors'
          url="https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png"
          maxZoom={19}
        />
        <MapAutoBounds nodes={network.nodes} />

        {/* Draw Routes (Edges) */}
        {network.edges.map((edge) => {
          const fromNode = nodeMap.get(edge.from);
          const toNode = nodeMap.get(edge.to);
          if (!fromNode || !toNode) return null;

          const fromGeo = geoCoordinates.get(fromNode.id);
          const toGeo = geoCoordinates.get(toNode.id);
          if (!fromGeo || !toGeo) return null;

          const isSelected = selected?.kind === 'edge' && selected.id === edge.id;
          const isHl = highlight.edgeIds.includes(edge.id);
          const isTraced = tracedJourney?.edgeIds.has(edge.id);
          const isDim = (tracedJourney && !isTraced) || (highlight.edgeIds.length > 0 && !isHl && highlight.dimOthers);
          const util = edge.utilisation ?? 0;
          const flow = edge.flow ?? 0;
          const isBottleneck = edge.usable && flow > 0 && util >= BOTTLENECK_UTILISATION;

          let color = '#64748b';
          let dashArray: string | undefined;
          let weight = 2.5 + Math.min(util, 1) * 4;
          let className = '';

          if (!edge.active) {
            color = '#ef4444';
            dashArray = '6, 6';
          } else if (!edge.usable) {
            color = '#94a3b8';
            dashArray = '3, 6';
          } else if (isTraced) {
            color = '#fbbf24'; // Radiant Gold for traced delivery journey
            weight = Math.max(weight + 3, 6.5);
            className = 'cargo-flow-traced';
          } else if (isHl) {
            color = '#38bdf8'; // Cyan
            weight = Math.max(weight, 6);
            className = 'cargo-flow-active';
          } else if (isBottleneck) {
            color = '#f97316'; // Warning orange
            className = 'cargo-flow-bottleneck';
          } else if (flow > 0) {
            color = '#3b82f6'; // Flow blue with animated cargo pulses
            className = 'cargo-flow-active';
          }

          if (isSelected) {
            weight += 2;
            color = '#e11d48';
          }

          return (
            <Polyline
              key={edge.id}
              positions={[
                [fromGeo.lat, fromGeo.lng],
                [toGeo.lat, toGeo.lng],
              ]}
              pathOptions={{
                color,
                weight,
                dashArray,
                opacity: isDim ? 0.15 : 0.92,
                className,
              }}
              eventHandlers={{
                click: () => onSelect({ kind: 'edge', id: edge.id }),
              }}
            >
              <Tooltip sticky>
                <div>
                  <strong>{fromGeo.city} → {toGeo.city}</strong> (Route #{edge.id})<br />
                  Flow: <b>{flow} / {edge.capacity}</b> units ({percent(util)} utilised)<br />
                  Transit: {edge.transitTime} hours · Cost: ${edge.costPerUnit}/unit<br />
                  Status: {edge.active ? (edge.usable ? 'Operating' : 'Blocked') : 'FAILED'}
                  {isBottleneck && <span style={{ color: '#f97316', fontWeight: 600 }}> ▲ Bottleneck</span>}
                </div>
              </Tooltip>

              <Popup>
                <div style={{ minWidth: 200 }}>
                  <h4 style={{ margin: '0 0 6px', fontSize: 13 }}>
                    Route #{edge.id}: {fromGeo.city} → {toGeo.city}
                  </h4>
                  <div style={{ fontSize: 12, marginBottom: 8 }}>
                    Flow: <b>{flow} / {edge.capacity} units</b><br />
                    Transit Time: <b>{edge.transitTime} h</b> · Cost: <b>${edge.costPerUnit}/u</b><br />
                    Status: <b style={{ color: edge.active ? '#10b981' : '#ef4444' }}>
                      {edge.active ? (edge.usable ? 'Operating' : 'Endpoint Blocked') : 'SEVERED / FAILED'}
                    </b>
                  </div>
                  {onApplyEvent && (
                    <div style={{ borderTop: '1px solid #e2e8f0', paddingTop: 8, display: 'flex', flexDirection: 'column', gap: 4 }}>
                      <div style={{ fontSize: 11, fontWeight: 600, color: '#64748b' }}>Direct In-Map Disruption:</div>
                      {edge.active ? (
                        <>
                          <button
                            className="btn danger small"
                            disabled={busy}
                            onClick={() => onApplyEvent({ type: 'route_failure', edge: edge.id })}
                          >
                            ✕ Sever Highway Corridor
                          </button>
                          <button
                            className="btn small"
                            disabled={busy}
                            onClick={() => onApplyEvent({ type: 'capacity_reduction', edge: edge.id, percentRemaining: 50 })}
                          >
                            ▼ Throttle Capacity 50%
                          </button>
                        </>
                      ) : (
                        <button
                          className="btn primary small"
                          disabled={busy}
                          onClick={() => onApplyEvent({ type: 'restore_route', edge: edge.id })}
                        >
                          ✓ Reopen Corridor
                        </button>
                      )}
                    </div>
                  )}
                </div>
              </Popup>
            </Polyline>
          );
        })}

        {/* Draw Facility Nodes */}
        {network.nodes.map((node) => {
          const geo = geoCoordinates.get(node.id);
          if (!geo) return null;

          const isSelected = selected?.kind === 'node' && selected.id === node.id;
          const isHl = highlight.nodeIds.includes(node.id);
          const isTraced = tracedJourney?.nodeIds.has(node.id) ?? false;
          const isDim = tracedJourney && !isTraced;

          return (
            <Marker
              key={node.id}
              position={[geo.lat, geo.lng]}
              opacity={isDim ? 0.2 : 1}
              icon={createCustomIcon(node, isSelected, isHl, isTraced)}
              eventHandlers={{
                click: () => onSelect({ kind: 'node', id: node.id }),
              }}
            >
              <Popup>
                <div style={{ minWidth: 200 }}>
                  <h3 style={{ margin: '0 0 4px', fontSize: 14 }}>{node.name} — {geo.city}</h3>
                  <p style={{ margin: '0 0 6px', fontSize: 12, color: '#64748b' }}>
                    Type: <strong>{titleCase(node.type)}</strong><br />
                    Status: <strong style={{ color: node.active ? '#10b981' : '#ef4444' }}>
                      {node.active ? 'Operating' : node.isBackup ? 'Offline Backup' : 'FAILED'}
                    </strong>
                  </p>

                  {node.supply > 0 && (
                    <div style={{ fontSize: 12, borderTop: '1px solid #e2e8f0', padding: '4px 0' }}>
                      Supply: <b>{node.supplyDraw ?? 0} of {node.supply} units</b><br />
                      Purchase Cost: <b>${node.unitPurchaseCost}/unit</b>
                    </div>
                  )}

                  {node.inventory > 0 && (
                    <div style={{ fontSize: 12, borderTop: '1px solid #e2e8f0', padding: '4px 0' }}>
                      Stock: <b>{node.inventory}</b> (drawn {node.inventoryDraw ?? 0})
                    </div>
                  )}

                  {node.type === 'customer' && (
                    <div style={{ fontSize: 12, borderTop: '1px solid #e2e8f0', padding: '4px 0' }}>
                      Demand: <b>{node.demand}</b> · Served: <b>{node.demand - (node.unmetDemand ?? 0)}</b><br />
                      Unmet Shortage: <b style={{ color: (node.unmetDemand ?? 0) > 0 ? '#ef4444' : '#10b981' }}>{node.unmetDemand ?? 0} units</b>
                    </div>
                  )}

                  <div style={{ borderTop: '1px solid #e2e8f0', paddingTop: 8, display: 'flex', flexDirection: 'column', gap: 5 }}>
                    <button
                      className="btn small"
                      style={{ background: '#0284c7', color: 'white' }}
                      onClick={() => handleTraceNode(node)}
                    >
                      🔍 Trace Delivery Corridors
                    </button>

                    {onApplyEvent && (
                      <>
                        {node.active && node.type !== 'customer' && (
                          <button
                            className="btn danger small"
                            disabled={busy}
                            onClick={() => onApplyEvent({ type: node.type === 'supplier' ? 'supplier_failure' : 'node_failure', node: node.name })}
                          >
                            ⚡ Shut Down Facility
                          </button>
                        )}
                        {!node.active && !node.isBackup && (
                          <button
                            className="btn primary small"
                            disabled={busy}
                            onClick={() => onApplyEvent({ type: 'restore_node', node: node.name })}
                          >
                            ✓ Restore Facility
                          </button>
                        )}
                        {node.type === 'customer' && (
                          <div style={{ display: 'flex', gap: 4 }}>
                            <button
                              className="btn small"
                              disabled={busy}
                              onClick={() => onApplyEvent({ type: 'demand_spike', node: node.name, percentIncrease: 50 })}
                            >
                              ▲ Spike +50%
                            </button>
                            <button
                              className="btn small"
                              disabled={busy}
                              onClick={() => onApplyEvent({ type: 'demand_spike', node: node.name, percentIncrease: 100 })}
                            >
                              ▲ Spike +100%
                            </button>
                          </div>
                        )}
                      </>
                    )}
                  </div>
                </div>
              </Popup>
            </Marker>
          );
        })}
      </MapContainer>

      {/* Option 2: End-to-End Traced Journey HUD */}
      {tracedJourney && (
        <div className="journey-hud">
          <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
            <span style={{ fontWeight: 700, color: '#fbbf24', fontSize: 13 }}>
              📍 Traced Delivery Journey for {tracedJourney.targetNode.name}
            </span>
            <div style={{ display: 'flex', gap: 8, alignItems: 'center' }}>
              <span className="pill good" style={{ fontSize: 11 }}>
                Transit Lead Time: {tracedJourney.totalTransitHours} hrs
              </span>
              <span className="pill" style={{ background: '#1e293b', color: '#f8fafc', fontSize: 11 }}>
                Freight Cost: ${tracedJourney.totalCostPerUnit}/unit
              </span>
              <button className="btn small" onClick={handleClearJourney} style={{ padding: '2px 8px', fontSize: 11 }}>
                ✕ Clear
              </button>
            </div>
          </div>

          <div className="journey-stages">
            {tracedJourney.stages.length === 0 ? (
              <span className="faint">No active incoming or outgoing flow connected to this facility currently.</span>
            ) : (
              tracedJourney.stages.map((st, i) => (
                <div key={i} style={{ display: 'inline-flex', alignItems: 'center', gap: 6 }}>
                  <span className="journey-stage-node" style={{ borderLeft: `3px solid ${NODE_TYPE_COLOR[st.from.type]}` }}>
                    {st.from.name}
                  </span>
                  <span className="journey-stage-arrow">
                    ──({st.edge.flow} units · {st.edge.transitTime}h)──▶
                  </span>
                  {i === tracedJourney.stages.length - 1 && (
                    <span className="journey-stage-node" style={{ borderLeft: `3px solid ${NODE_TYPE_COLOR[st.to.type]}` }}>
                      {st.to.name}
                    </span>
                  )}
                </div>
              ))
            )}
          </div>
        </div>
      )}

      {/* Option 4: Multi-Day Timeline Playback Scrubber */}
      {playbackActive && (
        <div style={{
          position: 'absolute',
          bottom: tracedJourney ? 84 : 14,
          left: 14,
          right: 14,
          zIndex: 999,
          background: 'rgba(15, 23, 42, 0.94)',
          backdropFilter: 'blur(10px)',
          border: '1px solid rgba(56, 189, 248, 0.4)',
          borderRadius: 10,
          padding: '8px 16px',
          color: 'white',
          boxShadow: '0 8px 24px rgba(0, 0, 0, 0.4)',
          display: 'flex',
          alignItems: 'center',
          gap: 12,
        }}>
          <button
            className={`btn small ${isPlaying ? 'danger' : 'primary'}`}
            onClick={() => setIsPlaying(!isPlaying)}
          >
            {isPlaying ? '⏸ Pause' : '▶ Play'}
          </button>
          <span style={{ fontWeight: 600, fontSize: 12, minWidth: 60 }}>
            Day {simDay} of 20
          </span>
          <input
            type="range"
            min={0}
            max={20}
            value={simDay}
            onChange={(e) => setSimDay(Number(e.target.value))}
            style={{ flex: 1, accentColor: '#38bdf8' }}
          />
          <button className="btn small" onClick={() => setSimDay((d) => Math.max(0, d - 1))}>◀</button>
          <button className="btn small" onClick={() => setSimDay((d) => Math.min(20, d + 1))}>▶</button>
          <span className="pill info" style={{ fontSize: 11 }}>
            Simulation Horizon: 20 Days
          </span>
        </div>
      )}
    </div>
  );
}
