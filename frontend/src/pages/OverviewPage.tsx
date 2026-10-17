import { CostBar, EmptyState, PageHead, Tile } from '../components/Common';
import { ScenarioPicker } from '../components/ScenarioPicker';
import { useApp } from '../hooks/useApp';
import { money, percent } from '../utils/format';

export function OverviewPage() {
  const { snapshot } = useApp();
  if (!snapshot) {
    return (
      <>
        <PageHead title="Overview" subtitle="Key figures of the loaded supply chain, computed by the C++ engine." />
        <ScenarioPicker />
        <div style={{ height: 16 }} />
        <EmptyState title="No scenario loaded">
          <p>Pick a standard scenario, a bundled file, or generate a network above to see the dashboard.</p>
        </EmptyState>
      </>
    );
  }
  const m = snapshot.metrics;
  return (
    <>
      <PageHead title="Overview" subtitle="Key figures of the loaded supply chain, computed by the C++ engine.">
        <span className="scenario-chip">{snapshot.scenario.name} · seed {snapshot.scenario.seed}</span>
      </PageHead>

      <div className="tiles" style={{ marginBottom: 16 }}>
        <Tile label="Suppliers" value={m.totalSuppliers} sub={`${m.activeSuppliers} active · incl. backups`} />
        <Tile label="Factories" value={m.factories} />
        <Tile label="Warehouses" value={m.warehouses} sub={`${m.distributionCenters} distribution centers`} />
        <Tile label="Customers" value={m.customers} />
        <Tile label="Total demand" value={money(m.totalDemand)} sub="units" />
        <Tile label="Fulfilled demand" value={money(m.deliveredDemand)} sub={percent(m.fulfilmentRatio)} tone={m.unmetDemand === 0 ? 'good' : undefined} />
        <Tile label="Unmet demand" value={money(m.unmetDemand)} sub="units short" tone={m.unmetDemand > 0 ? 'bad' : 'good'} />
        <Tile label="Network utilisation" value={percent(m.networkUtilisation)} sub="flow / available capacity" />
        <Tile label="Total operating cost" value={money(m.totalCost)} sub="incl. penalties and actions" />
        <Tile label="Active disruptions" value={m.activeDisruptions} tone={m.activeDisruptions > 0 ? 'bad' : undefined}
          sub={`${m.failedNodes} nodes · ${m.failedRoutes} routes down · ${m.reducedRoutes} reduced · ${m.demandSpikes} spikes`} />
      </div>

      <div className="grid cols-2">
        <div className="card">
          <h2>Cost breakdown</h2>
          <CostBar metrics={m} />
          <p className="faint" style={{ marginTop: 10 }}>Flows shown: {snapshot.flowSource}.</p>
        </div>
        <div className="card">
          <h2>Events</h2>
          <h3 className="muted" style={{ marginBottom: 6 }}>Applied ({snapshot.appliedEvents.length})</h3>
          {snapshot.appliedEvents.length === 0 ? <p className="muted">None yet: the network is in its initial state.</p> : (
            <ol className="log">{snapshot.appliedEvents.map((e, i) => <li key={i}><span className="n">{i + 1}.</span>{e.description}</li>)}</ol>
          )}
          <h3 className="muted" style={{ margin: '12px 0 6px' }}>Queued script ({snapshot.pendingEvents.length})</h3>
          {snapshot.pendingEvents.length === 0 ? <p className="muted">No queued events.</p> : (
            <ol className="log">{snapshot.pendingEvents.slice(0, 8).map((e, i) => <li key={i}><span className="n">{i + 1}.</span>{e.description}</li>)}
              {snapshot.pendingEvents.length > 8 && <li className="faint">… and {snapshot.pendingEvents.length - 8} more</li>}</ol>
          )}
          <p className="faint" style={{ marginTop: 10 }}>Use the Disruption Simulator to apply them.</p>
        </div>
      </div>

      <div style={{ height: 16 }} />
      <ScenarioPicker />
    </>
  );
}
