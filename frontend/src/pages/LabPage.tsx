import { useMemo, useState } from 'react';
import { EmptyState, Field, PageHead, Tile } from '../components/Common';
import { NetworkGraph } from '../components/NetworkGraph';
import { useApp } from '../hooks/useApp';
import type { DijkstraResult, MaxFlowResult, MetricsDto, PlanDto, RunResponse, TraversalResult } from '../types/api';
import { labView } from '../utils/labHighlight';
import { memory, milliseconds, money, percent } from '../utils/format';

function TraversalView({ result, kind }: { result: TraversalResult; kind: string }) {
  return (
    <>
      <p>Reached <b>{result.reachedCount}</b> node(s) from <b>{result.source.name}</b>; {result.unreachable.length} not reachable.</p>
      <h3>Visit order</h3>
      <ol className="order">{result.visitOrder.map((n, i) => <li key={n.id}><b>{i + 1}</b>{n.name}</li>)}</ol>
      {result.finishOrder && <><h3>Finish order (post-order)</h3>
        <ol className="order">{result.finishOrder.map((n, i) => <li key={n.id}><b>{i + 1}</b>{n.name}</li>)}</ol></>}
      {kind === 'bfs' && result.hops && (
        <div className="table-wrap" style={{ maxHeight: 220 }}>
          <table className="data"><thead><tr><th>Node</th><th className="num">Fewest hops</th></tr></thead>
            <tbody>{result.hops.map((h) => <tr key={h.node}><td>{h.node}</td><td className="num">{h.hops}</td></tr>)}</tbody></table>
        </div>
      )}
      <h3 style={{ marginTop: 10 }}>Failure impact</h3>
      {result.lostSinceInitial.length === 0
        ? <p className="muted">Everything reachable in the initial network is still reachable from {result.source.name}.</p>
        : <p>Cut off since the initial network: <b>{result.lostSinceInitial.map((n) => n.name).join(', ')}</b></p>}
    </>
  );
}

function DijkstraView({ result }: { result: DijkstraResult }) {
  return (
    <>
      {result.target && (result.reachable
        ? <p>Cheapest route <b>{result.source.name} → {result.target.name}</b> costs <b>${result.cost}</b> per unit via {result.pathNodes?.map((n) => n.name).join(' → ')}.</p>
        : <p className="pill bad">{result.target.name} is unreachable from {result.source.name} (no usable path)</p>)}
      <div className="table-wrap" style={{ maxHeight: 260 }}>
        <table className="data"><thead><tr><th>Node</th><th className="num">Cost per unit</th></tr></thead>
          <tbody>{result.distances.map((d) => <tr key={d.node}><td>{d.node}</td><td className="num">{d.cost === null ? 'unreachable' : d.cost}</td></tr>)}</tbody></table>
      </div>
    </>
  );
}

function MaxFlowView({ result, network }: { result: MaxFlowResult; network: { edges: { id: number; fromName: string; toName: string; capacity: number }[] } }) {
  const cut = result.minCutEdgeIds.map((id) => network.edges.find((e) => e.id === id)).filter(Boolean);
  return (
    <>
      <div className="tiles">
        <Tile label="Max deliverable" value={money(result.maxDeliverable)} sub={`of ${money(result.totalDemand)} demand`} />
        <Tile label="Minimum cut" value={money(result.minCutCapacity)} sub="equals the flow (max-flow min-cut)" />
      </div>
      <h3 style={{ marginTop: 10 }}>Bottleneck routes (minimum cut)</h3>
      {cut.length === 0 ? <p className="muted">No route is limiting: supply or demand is the limit.</p>
        : <ul className="log">{cut.map((e) => <li key={e!.id}>{e!.fromName} → {e!.toName} <span className="pill warn">cap {e!.capacity}</span></li>)}</ul>}
      {result.supplyLimitedNodes.length > 0 && <p>Supply-limited: <b>{result.supplyLimitedNodes.map((n) => n.name).join(', ')}</b></p>}
      {result.demandLimitedNodes.length > 0 && <p>Demand already fully met at: <b>{result.demandLimitedNodes.map((n) => n.name).join(', ')}</b></p>}
    </>
  );
}

function PlanView({ plan, metrics, executed }: { plan: PlanDto; metrics: MetricsDto; executed: boolean }) {
  return (
    <>
      <div className="tiles">
        <Tile label="Total cost" value={money(metrics.totalCost)} sub={`flow ${money(plan.flowCost)} + actions ${money(plan.actionCost)}`} />
        <Tile label="Unmet demand" value={money(metrics.unmetDemand)} tone={metrics.unmetDemand > 0 ? 'bad' : 'good'} sub={`${percent(metrics.fulfilmentRatio)} fulfilled`} />
        <Tile label="Utilisation" value={percent(metrics.networkUtilisation)} />
        <Tile label="Feasible" value={metrics.feasible ? 'yes' : 'NO'} tone={metrics.feasible ? 'good' : 'bad'} sub="independent validator" />
      </div>
      <h3 style={{ marginTop: 10 }}>Recovery actions ({plan.actions.length})</h3>
      {plan.actions.length === 0 ? <p className="muted">No paid action pays for itself here; shipments are re-routed optimally.</p> : (
        <table className="data"><thead><tr><th>Action</th><th>Target</th><th className="num">Cost</th></tr></thead>
          <tbody>{plan.actions.map((a, i) => <tr key={i}><td>{a.type === 'activate_supplier' ? 'Activate backup supplier' : 'Reopen route'}</td><td>{a.label}</td><td className="num">${a.cost}</td></tr>)}</tbody></table>
      )}
      {plan.steps.length > 0 && (
        <>
          <h3 style={{ margin: '12px 0 4px' }}>Decision trace</h3>
          <div className="table-wrap">
            <table className="data"><thead><tr><th>#</th><th>Chosen action</th><th className="num">Score</th><th className="num">Unmet removed</th><th className="num">Flow saving</th><th className="num">Saturated %</th></tr></thead>
              <tbody>{plan.steps.map((s, i) => (
                <tr key={i}><td>{i + 1}</td><td>{plan.actions[i]?.label}</td><td className="num">{s.score.toFixed(3)}</td>
                  <td className="num">{s.shortageReduction}</td><td className="num">{money(s.costGain)}</td><td className="num">{percent(s.bottleneckRisk)}</td></tr>))}</tbody></table>
          </div>
        </>
      )}
      <p className="faint" style={{ marginTop: 8 }}>{plan.candidateEvaluations} min-cost-flow solve(s) · {executed ? 'applied to the scenario' : 'preview only: scenario unchanged'}</p>
    </>
  );
}

export function LabPage() {
  const { snapshot, catalog, run, busy, setHighlight, config, updateConfig } = useApp();
  const [algorithm, setAlgorithm] = useState('bfs');
  const [source, setSource] = useState('');
  const [target, setTarget] = useState('');
  const lambda = config.lambda;
  const setLambda = (val: number) => updateConfig({ lambda: val });
  const [threshold, setThreshold] = useState(0.9);
  const [last, setLast] = useState<RunResponse | null>(null);

  const info = catalog.find((a) => a.id === algorithm);
  const nodes = snapshot?.network.nodes ?? [];
  const sources = nodes.filter((n) => n.type !== 'customer');
  const needs = (p: string) => info?.parameters.includes(p) ?? false;
  const sourceValue = source || (sources.find((n) => n.active) ?? sources[0])?.name || '';
  const view = useMemo(() => (last && snapshot ? labView(last, snapshot.network) : null), [last, snapshot]);

  if (!snapshot) {
    return (
      <>
        <PageHead title="Algorithm Laboratory" />
        <EmptyState title="No scenario loaded"><p>Load one on the Overview page first.</p></EmptyState>
      </>
    );
  }

  const execute = async (apply: boolean) => {
    const body: Record<string, unknown> = { algorithm };
    if (needs('source')) body.source = sourceValue;
    if (needs('target') && target) body.target = target;
    if (needs('lambda')) { body.lambda = lambda; body.saturationThreshold = threshold; }
    if (apply) body.execute = true;
    const response = await run(body);
    if (response) {
      setLast(response);
      if (snapshot) setHighlight(labView(response, snapshot.network).highlight);
    }
  };

  const groups = [['analysis', 'Analysis (answers a question about the network)'], ['recovery', 'Recovery strategies (produce a plan with a cost)']] as const;

  return (
    <>
      <PageHead title="Algorithm Laboratory"
        subtitle="Run any algorithm on the live scenario. Everything shown is computed by the C++ engine, including runtime and peak heap memory." />
      <div className="grid lab">
        <div className="stack">
          <div className="card">
            <h2>Run</h2>
            <Field label="Algorithm" htmlFor="algorithm">
              <select id="algorithm" value={algorithm} onChange={(e) => { setAlgorithm(e.target.value); setLast(null); }}>
                {groups.map(([cat, label]) => (
                  <optgroup key={cat} label={label}>
                    {catalog.filter((a) => a.category === cat).map((a) => <option key={a.id} value={a.id}>{a.title}</option>)}
                  </optgroup>
                ))}
              </select>
            </Field>
            {info && <p className="muted" style={{ marginTop: 8 }}>{info.description}</p>}
            {info && <p className="faint">Time {info.complexity.time} · Space {info.complexity.space}</p>}
            <div className="row">
              {needs('source') && (
                <Field label="Source" htmlFor="source">
                  <select id="source" value={sourceValue} onChange={(e) => setSource(e.target.value)}>
                    {sources.map((n) => <option key={n.id} value={n.name}>{n.name}</option>)}
                  </select>
                </Field>
              )}
              {needs('target') && (
                <Field label="Target (optional)" htmlFor="target">
                  <select id="target" value={target} onChange={(e) => setTarget(e.target.value)}>
                    <option value="">all nodes</option>
                    {nodes.map((n) => <option key={n.id} value={n.name}>{n.name}</option>)}
                  </select>
                </Field>
              )}
              {needs('lambda') && (
                <>
                  <Field label="λ (risk weight, shared across tabs)" htmlFor="lambda">
                    <input id="lambda" type="number" min={0} step={0.1} value={lambda} onChange={(e) => setLambda(Number(e.target.value))} />
                  </Field>
                  <Field label="Saturation threshold" htmlFor="threshold">
                    <input id="threshold" type="number" min={0.05} max={1} step={0.05} value={threshold} onChange={(e) => setThreshold(Number(e.target.value))} />
                  </Field>
                </>
              )}
            </div>
            <div className="row" style={{ marginTop: 12 }}>
              <button className="btn primary" disabled={busy || !info} onClick={() => void execute(false)}>Run</button>
              {info?.category === 'recovery' && (
                <button className="btn" disabled={busy} onClick={() => void execute(true)} title="Pay for the plan's actions and update the live scenario">Run and apply to scenario</button>
              )}
            </div>
          </div>

          {last && (
            <div className="card" aria-live="polite">
              <div className="card-head">
                <h2>{last.title}</h2>
                <span className="faint">{milliseconds(last.runtimeMs)} · peak heap {memory(last.peakMemoryBytes)}</span>
              </div>
              {(last.algorithm === 'bfs' || last.algorithm === 'dfs') && <TraversalView result={last.result as TraversalResult} kind={last.algorithm} />}
              {last.algorithm === 'dijkstra' && <DijkstraView result={last.result as DijkstraResult} />}
              {last.algorithm === 'max_flow' && <MaxFlowView result={last.result as MaxFlowResult} network={(last.result as MaxFlowResult).network} />}
              {last.plan && last.metrics && <PlanView plan={last.plan} metrics={last.metrics} executed={Boolean(last.executed)} />}
            </div>
          )}
        </div>

        <div className="stack">
          {view ? (
            <>
              <div className="card"><h2>Decision visualisation</h2>
                <p className="muted" style={{ margin: 0 }}>{view.highlight.label || 'Flows of this result'}. Edge labels show flow/capacity · cost.</p></div>
              <NetworkGraph network={view.network} highlight={view.highlight} fitKey={`${last?.algorithm}:${last?.runtimeMs}`} />
            </>
          ) : (
            <div className="card empty"><h2>No result yet</h2><p>Run an algorithm to see its decisions drawn on the network.</p></div>
          )}
        </div>
      </div>
    </>
  );
}
