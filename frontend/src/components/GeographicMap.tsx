import { useEffect, useMemo } from 'react';
import { MapContainer, TileLayer, Marker, Popup, Polyline, Tooltip, useMap } from 'react-leaflet';
import L from 'leaflet';
import 'leaflet/dist/leaflet.css';
import type { NetworkDto, NodeDto } from '../types/api';
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
const CANONICAL_HUBS: Record<string, GeoLocation> = {
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

// Procedural fallback coordinates for generated networks (distributed realistically in latitude & longitude)
function getFallbackCoordinates(node: NodeDto, index: number, totalInType: number): GeoLocation {
  const seed = (node.id * 17 + index * 31) % 1000;
  const jitterLat = ((seed % 100) - 50) * 0.04;
  const jitterLng = (((seed * 7) % 100) - 50) * 0.05;

  switch (node.type) {
    case 'supplier':
    case 'factory': {
      // West / East industrial belts
      const baseLats = [21.5, 23.0, 19.0, 13.5, 22.5, 17.5];
      const baseLngs = [70.5, 72.5, 74.0, 80.0, 86.0, 83.0];
      const idx = index % baseLats.length;
      return { lat: baseLats[idx] + jitterLat, lng: baseLngs[idx] + jitterLng, city: `${node.name} (Industrial Hub)` };
    }
    case 'warehouse': {
      // Central transshipment corridors
      const baseLats = [21.2, 17.4, 22.8, 23.3, 20.0];
      const baseLngs = [79.1, 78.5, 75.9, 77.4, 77.0];
      const idx = index % baseLats.length;
      return { lat: baseLats[idx] + jitterLat, lng: baseLngs[idx] + jitterLng, city: `${node.name} (Logistics Hub)` };
    }
    case 'distribution_center': {
      // Regional distribution metro centers
      const baseLats = [13.0, 19.1, 28.6, 22.6, 17.4];
      const baseLngs = [77.6, 73.0, 77.2, 88.4, 78.5];
      const idx = index % baseLats.length;
      return { lat: baseLats[idx] + jitterLat, lng: baseLngs[idx] + jitterLng, city: `${node.name} (Fulfillment Hub)` };
    }
    case 'customer':
    default: {
      // Major demand clusters
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

function createCustomIcon(node: NodeDto, isSelected: boolean, isHighlighted: boolean) {
  const color = NODE_TYPE_COLOR[node.type];
  const letter = NODE_TYPE_LETTER[node.type];
  const isFailed = !node.active;
  const isBackup = node.isBackup;
  const size = isSelected || isHighlighted ? 38 : 30;

  const html = `
    <div style="
      position: relative;
      width: ${size}px;
      height: ${size}px;
      border-radius: 50%;
      background: ${isFailed ? 'var(--bad, #dc2626)' : color};
      border: 3px solid ${isSelected ? '#f59e0b' : isHighlighted ? '#38bdf8' : '#ffffff'};
      box-shadow: 0 4px 12px rgba(0,0,0,0.35);
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
        background: rgba(15, 23, 42, 0.85);
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

export function GeographicMap({
  network,
  selected,
  onSelect,
  highlight,
}: {
  network: NetworkDto;
  selected: Selection;
  onSelect: (sel: Selection) => void;
  highlight: Highlight;
}) {
  const nodeMap = useMemo(() => new Map(network.nodes.map((n) => [n.id, n])), [network.nodes]);

  const geoCoordinates = useMemo(() => {
    const coords = new Map<number, GeoLocation>();
    network.nodes.forEach((n) => {
      coords.set(n.id, getNodeGeo(n, network.nodes));
    });
    return coords;
  }, [network.nodes]);

  return (
    <div className="card" style={{ padding: 0, overflow: 'hidden', height: 600, position: 'relative' }}>
      <div style={{
        position: 'absolute',
        top: 12,
        left: 12,
        zIndex: 1000,
        background: 'rgba(15, 23, 42, 0.88)',
        backdropFilter: 'blur(8px)',
        padding: '6px 14px',
        borderRadius: 8,
        border: '1px solid rgba(255,255,255,0.15)',
        color: 'white',
        fontSize: 12,
        boxShadow: '0 4px 12px rgba(0,0,0,0.3)',
      }}>
        <span style={{ fontWeight: 600, color: '#38bdf8' }}>🗺️ Geographic Road & Freight Map</span>
        <span style={{ marginLeft: 8, color: '#94a3b8' }}>Real OpenStreetMap Coordinates & Multi-Hop Corridors</span>
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
          const isDim = highlight.edgeIds.length > 0 && !isHl && highlight.dimOthers;
          const util = edge.utilisation ?? 0;
          const flow = edge.flow ?? 0;
          const isBottleneck = edge.usable && flow > 0 && util >= BOTTLENECK_UTILISATION;

          let color = '#64748b';
          let dashArray: string | undefined;
          let weight = 2.5 + Math.min(util, 1) * 4;

          if (!edge.active) {
            color = '#ef4444';
            dashArray = '6, 6';
          } else if (!edge.usable) {
            color = '#94a3b8';
            dashArray = '3, 6';
          } else if (isHl) {
            color = '#fbbf24'; // Golden amber highlight
            weight = Math.max(weight, 6);
          } else if (isBottleneck) {
            color = '#f97316'; // Warning orange
          } else if (flow > 0) {
            color = '#3b82f6'; // Flow blue
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
                opacity: isDim ? 0.2 : 0.88,
              }}
              eventHandlers={{
                click: () => onSelect({ kind: 'edge', id: edge.id }),
              }}
            >
              <Tooltip sticky>
                <div>
                  <strong>{fromGeo.city} → {toGeo.city}</strong> (Route #{edge.id})<br />
                  Flow: <b>{flow} / {edge.capacity}</b> units ({percent(util)} utilised)<br />
                  Transit: {edge.transitTime} hours · Cost: ${edge.costPerUnit}/u<br />
                  Status: {edge.active ? (edge.usable ? 'Operating' : 'Blocked') : 'FAILED'}
                  {isBottleneck && <span style={{ color: '#f97316', fontWeight: 600 }}> ▲ Bottleneck</span>}
                </div>
              </Tooltip>
            </Polyline>
          );
        })}

        {/* Draw Facility Nodes */}
        {network.nodes.map((node) => {
          const geo = geoCoordinates.get(node.id);
          if (!geo) return null;

          const isSelected = selected?.kind === 'node' && selected.id === node.id;
          const isHl = highlight.nodeIds.includes(node.id);

          return (
            <Marker
              key={node.id}
              position={[geo.lat, geo.lng]}
              icon={createCustomIcon(node, isSelected, isHl)}
              eventHandlers={{
                click: () => onSelect({ kind: 'node', id: node.id }),
              }}
            >
              <Popup>
                <div style={{ minWidth: 180 }}>
                  <h3 style={{ margin: '0 0 6px', fontSize: 14 }}>{node.name} — {geo.city}</h3>
                  <p style={{ margin: '0 0 6px', fontSize: 12, color: '#64748b' }}>
                    Type: <strong>{titleCase(node.type)}</strong><br />
                    Status: <strong style={{ color: node.active ? '#10b981' : '#ef4444' }}>
                      {node.active ? 'Operating' : node.isBackup ? 'Offline Backup' : 'FAILED'}
                    </strong>
                  </p>
                  {node.supply > 0 && (
                    <div style={{ fontSize: 12, borderTop: '1px solid #e2e8f0', paddingTop: 4 }}>
                      Supply Capacity: <b>{node.supply}</b><br />
                      Currently Shipping: <b>{node.supplyDraw ?? 0}</b><br />
                      Purchase Cost: <b>${node.unitPurchaseCost}/u</b>
                    </div>
                  )}
                  {node.inventory > 0 && (
                    <div style={{ fontSize: 12, borderTop: '1px solid #e2e8f0', paddingTop: 4 }}>
                      Warehouse Stock: <b>{node.inventory}</b><br />
                      Stock Draw: <b>{node.inventoryDraw ?? 0}</b>
                    </div>
                  )}
                  {node.type === 'customer' && (
                    <div style={{ fontSize: 12, borderTop: '1px solid #e2e8f0', paddingTop: 4 }}>
                      Customer Demand: <b>{node.demand}</b><br />
                      Unmet Demand: <b style={{ color: (node.unmetDemand ?? 0) > 0 ? '#ef4444' : '#10b981' }}>{node.unmetDemand ?? 0}</b><br />
                      Shortage Penalty: <b>${node.shortagePenaltyPerUnit}/u</b>
                    </div>
                  )}
                </div>
              </Popup>
            </Marker>
          );
        })}
      </MapContainer>
    </div>
  );
}
