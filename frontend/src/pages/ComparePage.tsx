import { useMemo, useState } from 'react';
import { HistoryLines, StrategyBars } from '../components/Charts';
import { EmptyState, Field, PageHead } from '../components/Common';
import { NetworkGraph } from '../components/NetworkGraph';
import { useApp } from '../hooks/useApp';
import type { ResultRow } from '../types/api';
import { strategyColor } from '../utils/colors';
import { memory, milliseconds, money, percent, titleCase } from '../utils/format';

export function ComparePage() {
  const { snapshot, scenarios, compare, api, busy, comparison, setComparison } = useApp();
  const strategies = scenarios?.strategies ?? [];
  const [chosen, setChosen] = useState<string[] | null>(null);
  const [lambda, setLambda] = useState(1);
  const [afterId, setAfterId] = useState('');
  const [history, setHistory] = useState<ResultRow[]>([]);

  const selected = chosen ?? strategies;
  // A comparison belongs to one network state: it is shown only while the scenario, its events and the
  // recovery state are exactly as they were when it was computed.
  const stateKey = [snapshot?.scenario.name, snapshot?.recoveryExecuted, ...(snapshot?.appliedEvents.map((e) => e.description) ?? [])].join('|');
  const response = comparison?.key === stateKey ? comparison.response : null;
  const rows = response?.results ?? [];
  const measured = rows.filter((r) => r.metrics);
  const best = response?.lowestTotalCost ?? null;
  const afterRow = measured.find((r) => r.strategy === (afterId || best)) ?? measured[0];

  const scenarioName = snapshot?.scenario.name;
  const runHistory = useMemo(() => history.filter((h) => h.scenario === scenarioName), [history, scenarioName]);

  if (!snapshot) {
    return (
      <>
        <PageHead title="Algorithm Comparison" />
        <EmptyState title="No scenario loaded"><p>Load one on the Overview page first.</p></EmptyState>
      </>
    );
  }

  const run = async () => {
    const result = await compare({ strategies: selected, lambda });
    if (result) {
      setComparison({ key: stateKey, response: result });
      setAfterId('');
      try { setHistory(await api.results()); } catch { /* history is optional */ }
    }
  };
  const toggle = (id: string) => setChosen(selected.includes(id) ? selected.filter((s) => s !== id) : [...selected, id]);

  const download = () => {
    if (!response) return;
    const blob = new Blob([JSON.stringify(response, null, 2)], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url; a.download = `comparison-${response.scenario.name}.json`; a.click();
    URL.revokeObjectURL(url);
  };

  return (
    <>
      <PageHead title="Algorithm Comparison"
        subtitle="Every strategy solves the same problem on the same disrupted network and is scored by the same objective, so their total costs are directly comparable. BFS, DFS, Dijkstra and max flow answer different questions and are not listed." />

      <div className="card">
        <h2>Strategies</h2>
        <div className="checks">
          {strategies.map((s) => (
            <label key={s} className="check"><input type="checkbox" checked={selected.includes(s)} onChange={() => toggle(s)} />
              <span className="swatch" style={{ background: strategyColor(s) }} />{titleCase(s)}</label>
          ))}
        </div>
        <div className="row" style={{ marginTop: 12 }}>
          <Field label="λ for bottleneck-aware" htmlFor="cmp-lambda">
            <input id="cmp-lambda" type="number" min={0} step={0.1} value={lambda} onChange={(e) => setLambda(Number(e.target.value))} />
          </Field>
          <button className="btn primary" disabled={busy || selected.length === 0} onClick={() => void run()}>Run comparison</button>
          <span className="faint">Current state: {snapshot.appliedEvents.length} event(s) applied · {snapshot.metrics.unmetDemand} units short before recovery.</span>
        </div>
      </div>

      {response && (
        <>
          <div style={{ height: 16 }} />
          <div className="card">
            <div className="card-head">
              <h2>Results</h2>
              <div className="row">
                <a className="btn small" href={api.resultsCsvUrl()} download="supplychainx-results.csv">Export CSV</a>
                <button className="btn small" onClick={download}>Export JSON</button>
              </div>
            </div>
            <div className="table-wrap">
              <table className="data">
                <thead><tr><th>Strategy</th><th className="num">Total cost</th><th className="num">Delivered</th><th className="num">Unmet</th><th className="num">Fulfilment</th>
                  <th className="num">Utilisation</th><th className="num">Actions</th><th className="num">Runtime</th><th className="num">Peak memory</th><th>Feasible</th></tr></thead>
                <tbody>
                  {rows.map((r) => r.metrics ? (
                    <tr key={r.strategy} className={r.strategy === best ? 'best' : ''}>
                      <td><span className="swatch" style={{ background: strategyColor(r.strategy), marginRight: 8 }} />{titleCase(r.strategy)}{r.strategy === best && <span className="pill good" style={{ marginLeft: 8 }}>lowest cost</span>}</td>
                      <td className="num">{money(r.metrics.totalCost)}</td>
                      <td className="num">{money(r.metrics.deliveredDemand)}</td>
                      <td className="num">{money(r.metrics.unmetDemand)}</td>
                      <td className="num">{percent(r.metrics.fulfilmentRatio)}</td>
                      <td className="num">{percent(r.metrics.networkUtilisation)}</td>
                      <td className="num">{r.plan?.actions.length ?? 0} (${money(r.metrics.actionCost)})</td>
                      <td className="num">{milliseconds(r.runtimeMs ?? 0)}</td>
                      <td className="num">{memory(r.peakMemoryBytes)}</td>
                      <td>{r.feasible ? <span className="pill good">✓ valid</span> : <span className="pill bad">✕ invalid</span>}</td>
                    </tr>
                  ) : (
                    <tr key={r.strategy} className="skipped"><td>{titleCase(r.strategy)}</td><td colSpan={9}>skipped: {r.skipped}</td></tr>
                  ))}
                </tbody>
              </table>
            </div>
            <p className="faint" style={{ marginTop: 8 }}>{response.note}</p>
          </div>

          <div style={{ height: 16 }} />
          <div className="grid cols-2">
            <div className="card"><StrategyBars title="Total cost" data={measured.map((r) => ({ id: r.strategy, value: r.metrics!.totalCost }))} format={money} /></div>
            <div className="card"><StrategyBars title="Unmet demand" unit="units" data={measured.map((r) => ({ id: r.strategy, value: r.metrics!.unmetDemand }))} format={money} /></div>
            <div className="card"><StrategyBars title="Runtime" unit="ms" data={measured.map((r) => ({ id: r.strategy, value: r.runtimeMs ?? 0 }))} format={(v) => v.toFixed(v < 10 ? 2 : 1)} /></div>
            <div className="card"><StrategyBars title="Network utilisation" unit="%" data={measured.map((r) => ({ id: r.strategy, value: r.metrics!.networkUtilisation * 100 }))} format={(v) => v.toFixed(1)} /></div>
          </div>

          {afterRow?.plan && (
            <>
              <div style={{ height: 16 }} />
              <div className="card">
                <div className="card-head">
                  <h2>Before / after</h2>
                  <Field label="After applying" htmlFor="after">
                    <select id="after" value={afterRow.strategy} onChange={(e) => setAfterId(e.target.value)}>
                      {measured.map((r) => <option key={r.strategy} value={r.strategy}>{titleCase(r.strategy)}</option>)}
                    </select>
                  </Field>
                </div>
                <div className="grid cols-2">
                  <div><div className="chart-title">Before (disrupted, flows not shown)</div>
                    <NetworkGraph network={response.before} height="auto" legend={false} fitKey={`b${stateKey}`} /></div>
                  <div><div className="chart-title">After {titleCase(afterRow.strategy)}</div>
                    <NetworkGraph network={afterRow.plan.network} height="auto" legend={false} fitKey={`a${stateKey}${afterRow.strategy}`} /></div>
                </div>
              </div>
            </>
          )}
        </>
      )}

      <div style={{ height: 16 }} />
      <div className="card">
        <h2>Run history for this scenario</h2>
        <p className="faint">Total cost of each recovery run recorded by the server, in the order they happened. Runs on different disruption states are not directly comparable.</p>
        <HistoryLines rows={runHistory} />
      </div>
    </>
  );
}
