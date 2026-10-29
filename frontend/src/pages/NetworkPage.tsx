import { useEffect, useMemo, useState } from 'react';
import { EmptyState, PageHead } from '../components/Common';
import { BOTTLENECK_UTILISATION, NetworkGraph, type Selection } from '../components/NetworkGraph';
import { useApp, NO_HIGHLIGHT } from '../hooks/useApp';
import type { EdgeDto, NodeDto, SpectralResultDto } from '../types/api';
import { money, percent, titleCase } from '../utils/format';

function NodeDetails({ node }: { node: NodeDto }) {
  return (
    <dl className="detail-list">
      <dt>Name</dt><dd>{node.name}</dd>
      <dt>Type</dt><dd>{titleCase(node.type)}</dd>
      <dt>Status</dt><dd>{node.active ? 'operating' : node.isBackup ? 'offline backup' : 'FAILED'}</dd>
      {node.supply > 0 && <><dt>Supply</dt><dd>{node.supply}</dd><dt>Shipping</dt><dd>{node.supplyDraw ?? 0}</dd><dt>Purchase cost</dt><dd>${node.unitPurchaseCost}/unit</dd></>}
      {node.isBackup && <><dt>Activation cost</dt><dd>${money(node.activationCost)}</dd></>}
      {node.inventory > 0 && <><dt>Inventory</dt><dd>{node.inventory}</dd><dt>Stock used</dt><dd>{node.inventoryDraw ?? 0}</dd></>}
      {node.type === 'customer' && <><dt>Demand</dt><dd>{node.demand}</dd><dt>Unmet</dt><dd>{node.unmetDemand ?? 0}</dd><dt>Shortage penalty</dt><dd>${node.shortagePenaltyPerUnit}/unit</dd></>}
    </dl>
  );
}

function EdgeDetails({ edge }: { edge: EdgeDto }) {
  return (
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
  const { api, snapshot, scenarios, load, reset, busy, highlight, setHighlight } = useApp();
  const [selected, setSelected] = useState<Selection>(null);
  const [quick, setQuick] = useState('supplier_failure');
  const [spectral, setSpectral] = useState<SpectralResultDto | null>(null);

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
        <EmptyState title="No scenario loaded"><p>Load one on the Overview page first.</p></EmptyState>
      </>
    );
  }
  const node = selected?.kind === 'node' ? snapshot.network.nodes.find((n) => n.id === selected.id) : undefined;
  const edge = selected?.kind === 'edge' ? snapshot.network.edges.find((e) => e.id === selected.id) : undefined;
  const hasHighlight = highlight.nodeIds.length + highlight.edgeIds.length > 0;

  return (
    <>
      <PageHead title="Supply Network" subtitle="Directed graph of the live scenario with Laplacian spectral connectivity analysis. Click a node or route for details; scroll to zoom, drag to pan.">
        <div className="row">
          <select aria-label="Scenario" value={quick} onChange={(e) => setQuick(e.target.value)} style={{ width: 'auto' }}>
            {(scenarios?.standard ?? []).map((s) => <option key={s} value={s}>{titleCase(s)}</option>)}
          </select>
          <button className="btn" disabled={busy} onClick={() => void load({ standard: quick, seed: snapshot.scenario.seed })}>Load scenario</button>
          <button className="btn" disabled={busy} onClick={() => void reset('initial')}>Reset network</button>
          {snapshot.recoveryExecuted && <button className="btn" disabled={busy} onClick={() => void reset('disrupted')}>Undo recovery</button>}
        </div>
      </PageHead>

      <div className="grid split">
        <NetworkGraph network={snapshot.network} selected={selected} onSelect={setSelected} highlight={highlight}
          fitKey={`${snapshot.scenario.name}:${snapshot.network.nodes.length}`} />
        <div className="stack">
          <div className="card">
            <h2>{selected ? (node ? 'Selected node' : 'Selected route') : 'Selection'}</h2>
            {node && <NodeDetails node={node} />}
            {edge && <EdgeDetails edge={edge} />}
            {!selected && <p className="muted">Nothing selected. Click a node or a route label.</p>}
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
