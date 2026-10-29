import { useEffect, useMemo, useState } from 'react';
import { EmptyState, PageHead } from '../components/Common';
import { BOTTLENECK_UTILISATION, NetworkGraph, type Selection } from '../components/NetworkGraph';
import { ScenarioPicker } from '../components/ScenarioPicker';
import { useApp, NO_HIGHLIGHT } from '../hooks/useApp';
import type { EdgeDto, NodeDto, SpectralResultDto } from '../types/api';
import { money, percent, titleCase } from '../utils/format';

function NodeDetails({ node, onApplyEvent, busy }: { node: NodeDto; onApplyEvent: (e: Record<string, unknown>) => void; busy: boolean }) {
  return (
    <>
      <dl className="detail-list">
        <dt>Name</dt><dd>{node.name}</dd>
        <dt>Type</dt><dd>{titleCase(node.type)}</dd>
        <dt>Status</dt><dd>{node.active ? 'operating' : node.isBackup ? 'offline backup' : 'FAILED'}</dd>
        {node.supply > 0 && <><dt>Supply</dt><dd>{node.supply}</dd><dt>Shipping</dt><dd>{node.supplyDraw ?? 0}</dd><dt>Purchase cost</dt><dd>${node.unitPurchaseCost}/unit</dd></>}
        {node.isBackup && <><dt>Activation cost</dt><dd>${money(node.activationCost)}</dd></>}
        {node.inventory > 0 && <><dt>Inventory</dt><dd>{node.inventory}</dd><dt>Stock used</dt><dd>{node.inventoryDraw ?? 0}</dd></>}
        {node.type === 'customer' && <><dt>Demand</dt><dd>{node.demand}</dd><dt>Unmet</dt><dd>{node.unmetDemand ?? 0}</dd><dt>Shortage penalty</dt><dd>${node.shortagePenaltyPerUnit}/unit</dd></>}
      </dl>
      <div className="row" style={{ marginTop: 10, flexWrap: 'wrap', gap: 6 }}>
        {node.active && node.type !== 'customer' && (
          <button className="btn danger small" disabled={busy} onClick={() => onApplyEvent({ type: node.type === 'supplier' ? 'supplier_failure' : 'node_failure', node: node.name })}>
            ✕ Shut down {node.type === 'supplier' ? 'supplier' : 'facility'}
          </button>
        )}
        {!node.active && !node.isBackup && (
          <button className="btn primary small" disabled={busy} onClick={() => onApplyEvent({ type: 'restore_node', node: node.name })}>
            ✓ Restore facility
          </button>
        )}
        {node.type === 'customer' && (
          <>
            <button className="btn small" disabled={busy} onClick={() => onApplyEvent({ type: 'demand_spike', node: node.name, percentIncrease: 50 })}>
              ▲ Spike demand +50%
            </button>
            <button className="btn small" disabled={busy} onClick={() => onApplyEvent({ type: 'demand_spike', node: node.name, percentIncrease: 100 })}>
              ▲ Spike demand +100%
            </button>
          </>
        )}
      </div>
    </>
  );
}

function EdgeDetails({ edge, onApplyEvent, busy }: { edge: EdgeDto; onApplyEvent: (e: Record<string, unknown>) => void; busy: boolean }) {
  return (
    <>
      <dl className="detail-list">
        <dt>Route</dt><dd>{edge.fromName} → {edge.toName}</dd>
        <dt>Status</dt><dd>{!edge.active ? 'FAILED' : edge.usable ? 'operating' : 'blocked by a failed endpoint'}</dd>
        <dt>Flow</dt><dd>{edge.flow ?? 0}</dd>
        <dt>Available capacity</dt><dd>{edge.capacity}</dd>
        <dt>Design capacity</dt><dd>{edge.baseCapacity}</dd>
        <dt>Utilisation</dt><dd>{percent(edge.utilisation ?? 0)}</dd>
        <dt>Cost</dt><dd>${edge.costPerUnit}/unit</dd>
        <dt>Transit time</dt><dd>{edge.transitTime} h</dd>
        <dt>Reopen cost</dt><dd>${edge.reopenCost}</dd>
      </dl>
      <div className="row" style={{ marginTop: 10, flexWrap: 'wrap', gap: 6 }}>
        {edge.active && (
          <>
            <button className="btn danger small" disabled={busy} onClick={() => onApplyEvent({ type: 'route_failure', edge: edge.id })}>
              ✕ Disable route
            </button>
            <button className="btn small" disabled={busy} onClick={() => onApplyEvent({ type: 'capacity_reduction', edge: edge.id, percentRemaining: 50 })}>
              ▼ Reduce capacity 50%
            </button>
          </>
        )}
        {(!edge.active || edge.capacity < edge.baseCapacity) && (
          <button className="btn primary small" disabled={busy} onClick={() => onApplyEvent({ type: 'restore_route', edge: edge.id })}>
            ✓ Restore route
          </button>
        )}
      </div>
    </>
  );
}

function SpectralCard({ spectral, onHighlightCut }: { spectral: SpectralResultDto | null; onHighlightCut: () => void }) {
  if (!spectral) return null;
  return (
    <div className="card">
      <h2>Spectral Graph Connectivity</h2>
      <dl className="detail-list">
        <dt>Algebraic Connectivity (λ₂)</dt>
        <dd><strong>{spectral.algebraicConnectivity !== null ? spectral.algebraicConnectivity.toFixed(4) : 'N/A'}</strong></dd>
        <dt>Connected Components</dt>
        <dd>{spectral.connectedComponents}</dd>
        <dt>Fiedler Partition</dt>
        <dd>{spectral.partitionPositive.length} positive / {spectral.partitionNegative.length} negative</dd>
        <dt>Crossing Cut Routes</dt>
        <dd>{spectral.crossingEdgeIds.length}</dd>
      </dl>
      {spectral.crossingEdgeIds.length > 0 && (
        <button className="btn small" style={{ marginTop: 8 }} onClick={onHighlightCut}>
          Highlight Fiedler Cut Routes ({spectral.crossingEdgeIds.length})
        </button>
      )}
    </div>
  );
}

export function NetworkPage() {
  const { api, snapshot, scenarios, load, reset, applyEvent, busy, highlight, setHighlight, config } = useApp();
  const [selected, setSelected] = useState<Selection>(null);
  const [quick, setQuick] = useState('supplier_failure');
  const [spectral, setSpectral] = useState<SpectralResultDto | null>(null);
  const [showPicker, setShowPicker] = useState(false);

  useEffect(() => {
    if (snapshot) {
      api.spectral().then(setSpectral).catch(() => setSpectral(null));
    } else {
      setSpectral(null);
    }
  }, [snapshot, api]);

  const bottlenecks = useMemo(() => (snapshot?.network.edges ?? [])
    .filter((e) => e.usable && (e.flow ?? 0) > 0 && (e.utilisation ?? 0) >= BOTTLENECK_UTILISATION)
    .sort((a, b) => (b.utilisation ?? 0) - (a.utilisation ?? 0)), [snapshot]);

  if (!snapshot) {
    return (
      <>
        <PageHead title="Supply Network" />
        <ScenarioPicker />
        <div style={{ height: 16 }} />
        <EmptyState title="No scenario loaded"><p>Pick or generate a scenario above to see the supply network graph.</p></EmptyState>
      </>
    );
  }
  const node = selected?.kind === 'node' ? snapshot.network.nodes.find((n) => n.id === selected.id) : undefined;
  const edge = selected?.kind === 'edge' ? snapshot.network.edges.find((e) => e.id === selected.id) : undefined;
  const hasHighlight = highlight.nodeIds.length + highlight.edgeIds.length > 0;

  return (
    <>
      <PageHead title="Supply Network" subtitle="Directed graph of the live scenario with Laplacian spectral connectivity analysis. Click a node or route to inspect and interact; changes reflect across all tabs.">
        <div className="row">
          <select aria-label="Scenario" value={quick} onChange={(e) => setQuick(e.target.value)} style={{ width: 'auto' }}>
            {(scenarios?.standard ?? []).map((s) => <option key={s} value={s}>{titleCase(s)}</option>)}
          </select>
          <button className="btn" disabled={busy} onClick={() => void load({ standard: quick, seed: config.seed })}>Load scenario</button>
          <button className="btn" disabled={busy} onClick={() => void reset('initial')}>Reset network</button>
          {snapshot.recoveryExecuted && <button className="btn" disabled={busy} onClick={() => void reset('disrupted')}>Undo recovery</button>}
          <button className={`btn ${showPicker ? 'primary' : ''}`} onClick={() => setShowPicker(!showPicker)}>
            {showPicker ? 'Hide Generator' : 'Customize Network'}
          </button>
        </div>
      </PageHead>

      {showPicker && (
        <div style={{ marginBottom: 16 }}>
          <ScenarioPicker />
        </div>
      )}

      <div className="grid split">
        <NetworkGraph network={snapshot.network} selected={selected} onSelect={setSelected} highlight={highlight}
          fitKey={`${snapshot.scenario.name}:${snapshot.network.nodes.length}`} />
        <div className="stack">
          <div className="card">
            <h2>{selected ? (node ? 'Selected node' : 'Selected route') : 'Selection'}</h2>
            {node && <NodeDetails node={node} onApplyEvent={(ev) => void applyEvent(ev)} busy={busy} />}
            {edge && <EdgeDetails edge={edge} onApplyEvent={(ev) => void applyEvent(ev)} busy={busy} />}
            {!selected && <p className="muted">Nothing selected. Click a node or a route label to inspect or disrupt it.</p>}
          </div>
          <SpectralCard
            spectral={spectral}
            onHighlightCut={() => {
              if (!spectral) return;
              setHighlight({
                nodeIds: [],
                edgeIds: spectral.crossingEdgeIds,
                label: 'Fiedler Cut (Structural Bottleneck)',
                dimOthers: true,
              });
            }}
          />
          <div className="card">
            <h2>Bottlenecks ({bottlenecks.length})</h2>
            {bottlenecks.length === 0 ? <p className="muted">No route is carrying 90% or more of its capacity.</p> : (
              <ul className="log">
                {bottlenecks.slice(0, 8).map((b) => (
                  <li key={b.id}><button className="btn small" onClick={() => setSelected({ kind: 'edge', id: b.id })}>{b.fromName} → {b.toName}</button>
                    <span>{percent(b.utilisation ?? 0)} of {b.capacity}</span></li>
                ))}
              </ul>
            )}
            <p className="faint" style={{ marginTop: 8 }}>Flows shown: {snapshot.flowSource}.</p>
          </div>
          {hasHighlight && (
            <div className="card">
              <h2>Highlighted result</h2>
              <p>{highlight.label || 'Algorithm result'}: {highlight.nodeIds.length} nodes, {highlight.edgeIds.length} routes.</p>
              <button className="btn small" onClick={() => setHighlight(NO_HIGHLIGHT)}>Clear highlight</button>
            </div>
          )}
        </div>
      </div>
    </>
  );
}
