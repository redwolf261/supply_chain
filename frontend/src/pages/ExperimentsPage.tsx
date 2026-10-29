import { useEffect, useMemo, useState } from 'react';
import { MeanBars } from '../components/Charts';
import { Field, PageHead } from '../components/Common';
import { useApp } from '../hooks/useApp';
import type { ExperimentResponse } from '../types/api';
import { strategyColor } from '../utils/colors';
import { excludesZero, formatPaired, recordsToCsv, responseFilename, summaryToCsv } from '../utils/experimentExport';
import { memory, milliseconds, money, percent, titleCase } from '../utils/format';

type Kind = 'random_disruption' | 'supplier_failures' | 'demand_spike' | 'route_capacity_loss' | 'network_size';

const KINDS: Record<Kind, { label: string; xLabel: string; fallback: number; min: number; max: number; step: number }> = {
  random_disruption: { label: 'Random disruption (severity)', xLabel: 'Disruption severity (0 to 1)', fallback: 0.4, min: 0, max: 1, step: 0.05 },
  supplier_failures: { label: 'Supplier failures', xLabel: 'Number of failed suppliers', fallback: 2, min: 0, max: 20, step: 1 },
  demand_spike: { label: 'Demand spike', xLabel: 'Demand increase (%)', fallback: 50, min: 0, max: 1000, step: 5 },
  route_capacity_loss: { label: 'Route bottlenecks', xLabel: 'Capacity lost on the busiest quarter of routes (%)', fallback: 60, min: 0, max: 100, step: 5 },
  network_size: { label: 'Network size', xLabel: 'Size multiplier (applies to every layer)', fallback: 2, min: 0.5, max: 20, step: 0.5 },
};

const NETWORK_DEFAULTS = { suppliers: 6, backupSuppliers: 3, warehouses: 4, distributionCenters: 4, customers: 10, edges: 0, capacityFactor: 1.2, demandMin: 20, demandMax: 60 };

function download(name: string, text: string, type: string) {
  const url = URL.createObjectURL(new Blob([text], { type }));
  const a = document.createElement('a');
  a.href = url; a.download = name; a.click();
  URL.revokeObjectURL(url);
}

export function ExperimentsPage() {
  const { api, scenarios, busy, config, updateConfig, snapshot } = useApp();
  const strategies = scenarios?.strategies ?? [];
  const [kind, setKind] = useState<Kind>('random_disruption');
  const [x, setX] = useState(KINDS.random_disruption.fallback);
  const [severity, setSeverity] = useState(0.3);
  const [net, setNet] = useState({
    suppliers: config.suppliers,
    backupSuppliers: config.backupSuppliers,
    warehouses: config.warehouses,
    distributionCenters: config.distributionCenters,
    customers: config.customers,
    edges: config.edges,
    capacityFactor: config.capacityFactor,
    demandMin: config.demandMin,
    demandMax: config.demandMax,
  });
  const [seed, setSeed] = useState(config.seed);
  const [runs, setRuns] = useState(20);
  const lambda = config.lambda;
  const setLambda = (val: number) => updateConfig({ lambda: val });
  const [chosen, setChosen] = useState<string[] | null>(null);
  const [reference, setReference] = useState('greedy');
  const [working, setWorking] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [result, setResult] = useState<ExperimentResponse | null>(null);

  // Sync with global config whenever config changes in other tabs
  useEffect(() => {
    setNet({
      suppliers: config.suppliers,
      backupSuppliers: config.backupSuppliers,
      warehouses: config.warehouses,
      distributionCenters: config.distributionCenters,
      customers: config.customers,
      edges: config.edges,
      capacityFactor: config.capacityFactor,
      demandMin: config.demandMin,
      demandMax: config.demandMax,
    });
    setSeed(config.seed);
  }, [config.suppliers, config.backupSuppliers, config.warehouses, config.distributionCenters, config.customers, config.edges, config.capacityFactor, config.demandMin, config.demandMax, config.seed]);

  const selected = chosen ?? strategies.filter((s) => s !== 'exact');
  const k = KINDS[kind];
  const baseNodes = net.suppliers + net.backupSuppliers + net.warehouses + net.distributionCenters + net.customers;
  const nodes = kind === 'network_size' ? Math.round(baseNodes * x) : baseNodes;
  const overLimit = nodes * runs > 6000 || runs > 200 || nodes > 400;

  const run = async () => {
    setWorking(true);
    setError(null);
    try {
      const response = await api.runExperiment({
        kind, x, severity, seed, runs, lambda, strategies: selected,
        pairedAgainst: selected.includes(reference) ? reference : undefined,
        network: net,
      });
      setResult(response);
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    } finally {
      setWorking(false);
    }
  };

  const handleNetChange = (key: keyof typeof NETWORK_DEFAULTS, val: number) => {
    setNet((prev) => ({ ...prev, [key]: val }));
    updateConfig({ [key]: val });
  };

  const num = (key: keyof typeof NETWORK_DEFAULTS, step = 1) => (
    <input id={`net-${key}`} type="number" step={step} min={0} value={net[key]} onChange={(e) => handleNetChange(key, Number(e.target.value))} />
  );
  const toggle = (id: string) => setChosen(selected.includes(id) ? selected.filter((s) => s !== id) : [...selected, id]);

  const rows = result?.strategies ?? [];
  const costBars = useMemo(() => rows.filter((r) => r.completed > 0).map((r) => ({ id: r.label, label: r.label.replace('bottleneck_aware', 'BA').replace('dijkstra_dispatch', 'dispatch'),
    mean: r.cost.mean, ci: r.cost.ci95, color: strategyColor(r.strategy) })), [rows]);
  const runtimeBars = useMemo(() => rows.filter((r) => r.completed > 0).map((r) => ({ id: r.label, label: r.label.replace('bottleneck_aware', 'BA').replace('dijkstra_dispatch', 'dispatch'),
    mean: r.runtimeMs.mean, ci: r.runtimeMs.ci95, color: strategyColor(r.strategy) })), [rows]);
  const lowestMean = rows.filter((r) => r.completed > 0).reduce<number | null>((best, r) => (best === null || r.cost.mean < best ? r.cost.mean : best), null);

  return (
    <>
      <PageHead title="Experiment Laboratory"
        subtitle="Run a batch of seeded random scenarios. Every strategy solves the identical disrupted network in each run, and every figure below is measured by the C++ engine, shown with its sample size and spread." />

      <div className="card">
        <div className="card-head">
          <h2>Configuration (Shared Across Tabs)</h2>
          {snapshot && (
            <button className="btn small" type="button" onClick={() => {
              setNet({
                suppliers: config.suppliers,
                backupSuppliers: config.backupSuppliers,
                warehouses: config.warehouses,
                distributionCenters: config.distributionCenters,
                customers: config.customers,
                edges: config.edges,
                capacityFactor: config.capacityFactor,
                demandMin: config.demandMin,
                demandMax: config.demandMax,
              });
              setSeed(config.seed);
            }}>
              Sync with Active Network ({snapshot.scenario.name})
            </button>
          )}
        </div>
        <div className="row">
          <Field label="Scenario type" htmlFor="exp-kind">
            <select id="exp-kind" value={kind} onChange={(e) => { const next = e.target.value as Kind; setKind(next); setX(KINDS[next].fallback); }}>
              {(Object.keys(KINDS) as Kind[]).map((key) => <option key={key} value={key}>{KINDS[key].label}</option>)}
            </select>
          </Field>
          <Field label={k.xLabel} htmlFor="exp-x">
            <input id="exp-x" type="number" min={k.min} max={k.max} step={k.step} value={x} onChange={(e) => setX(Number(e.target.value))} />
          </Field>
          {kind === 'network_size' && (
            <Field label={`Disruption severity ${severity.toFixed(2)}`} htmlFor="exp-sev">
              <input id="exp-sev" type="range" min={0} max={1} step={0.05} value={severity} onChange={(e) => setSeverity(Number(e.target.value))} />
            </Field>
          )}
          <Field label="Number of experiments (runs)" htmlFor="exp-runs">
            <input id="exp-runs" type="number" min={1} max={200} value={runs} onChange={(e) => setRuns(Number(e.target.value))} />
          </Field>
          <Field label="Random seed" htmlFor="exp-seed">
            <input id="exp-seed" type="number" min={0} value={seed} onChange={(e) => { setSeed(Number(e.target.value)); updateConfig({ seed: Number(e.target.value) }); }} />
          </Field>
        </div>
        <h3 className="muted" style={{ margin: '14px 0 6px' }}>Network (Shared Variables)</h3>
        <div className="row">
          <Field label="Suppliers" htmlFor="net-suppliers">{num('suppliers')}</Field>
          <Field label="Backup suppliers" htmlFor="net-backupSuppliers">{num('backupSuppliers')}</Field>
          <Field label="Warehouses" htmlFor="net-warehouses">{num('warehouses')}</Field>
          <Field label="Distribution centers" htmlFor="net-distributionCenters">{num('distributionCenters')}</Field>
          <Field label="Customers" htmlFor="net-customers">{num('customers')}</Field>
          <Field label="Routes (0 = automatic)" htmlFor="net-edges">{num('edges')}</Field>
        </div>
        <div className="row" style={{ marginTop: 8 }}>
          <Field label="Supply and route capacity (factor of demand)" htmlFor="net-capacityFactor">{num('capacityFactor', 0.1)}</Field>
          <Field label="Smallest customer demand" htmlFor="net-demandMin">{num('demandMin')}</Field>
          <Field label="Largest customer demand" htmlFor="net-demandMax">{num('demandMax')}</Field>
          <Field label="λ for bottleneck-aware (shared across tabs)" htmlFor="exp-lambda">
            <input id="exp-lambda" type="number" min={0} step={0.25} value={lambda} onChange={(e) => setLambda(Number(e.target.value))} />
          </Field>
        </div>
        <h3 className="muted" style={{ margin: '14px 0 6px' }}>Strategies</h3>
        <div className="checks">
          {strategies.map((s) => (
            <label key={s} className="check"><input type="checkbox" checked={selected.includes(s)} onChange={() => toggle(s)} />
              <span className="swatch" style={{ background: strategyColor(s) }} />{titleCase(s)}</label>
          ))}
        </div>
        <p className="faint" style={{ marginTop: 6 }}>Exhaustive search is skipped, and reported as skipped, in any run with more than 10 candidate recovery actions.</p>
        <div className="row" style={{ marginTop: 10 }}>
          <Field label="Compare the others against" htmlFor="exp-ref">
            <select id="exp-ref" value={selected.includes(reference) ? reference : selected[0] ?? ''} onChange={(e) => setReference(e.target.value)}>
              {selected.map((s) => <option key={s} value={s}>{titleCase(s)}</option>)}
            </select>
          </Field>
          <button className="btn primary" disabled={busy || working || selected.length === 0 || overLimit} onClick={() => void run()}>
            {working ? 'Running…' : 'Run experiments'}
          </button>
          <span className={overLimit ? 'pill bad' : 'faint'}>
            ≈ {nodes} nodes × {runs} runs = {nodes * runs} (limit 6000, at most 400 nodes and 200 runs)
          </span>
        </div>
        {error && <div className="banner error" role="alert" style={{ marginTop: 12, marginBottom: 0 }}><span>{error}</span></div>}
      </div>

      {result && (
        <>
          <div style={{ height: 16 }} />
          <div className="card">
            <div className="card-head">
              <h2>Results: {result.runs} runs, {Math.round(result.meanNodes)} nodes on average</h2>
              <div className="row">
                <button className="btn small" onClick={() => download(responseFilename(result, 'csv'), recordsToCsv(result.records), 'text/csv')}>Export runs (CSV)</button>
                <button className="btn small" onClick={() => download(responseFilename(result, 'summary.csv'), summaryToCsv(result.strategies), 'text/csv')}>Export summary (CSV)</button>
                <button className="btn small" onClick={() => download(responseFilename(result, 'json'), JSON.stringify(result, null, 2), 'application/json')}>Export JSON</button>
              </div>
            </div>
            <div className="table-wrap">
              <table className="data">
                <thead><tr><th>Strategy</th><th className="num">Runs</th><th className="num">Total cost (mean ± sd)</th><th className="num">min – max</th>
                  <th className="num">Cost − {titleCase(result.pairedAgainst)} (95%)</th>
                  <th className="num">Unmet</th><th className="num">Fulfilment</th><th className="num">Utilisation</th><th className="num">Runtime</th><th className="num">Peak memory</th>
                  <th className="num">Valid</th><th className="num">Regret</th><th className="num">Lowest in</th></tr></thead>
                <tbody>
                  {rows.map((r) => r.completed === 0 ? (
                    <tr key={r.label} className="skipped"><td>{titleCase(r.label)}</td><td colSpan={12}>skipped in all {r.skipped} runs (too many candidate actions for exhaustive search)</td></tr>
                  ) : (
                    <tr key={r.label} className={lowestMean !== null && r.cost.mean === lowestMean ? 'best' : ''}>
                      <td><span className="swatch" style={{ background: strategyColor(r.strategy), marginRight: 8 }} />{r.label.replace(/^bottleneck_aware/, 'bottleneck-aware').replace('_', ' ')}{r.skipped > 0 && <span className="pill warn" style={{ marginLeft: 6 }}>skipped {r.skipped}</span>}</td>
                      <td className="num">{r.completed}</td>
                      <td className="num">{money(r.cost.mean)} ± {money(r.cost.stddev)}</td>
                      <td className="num">{money(r.cost.min)} – {money(r.cost.max)}</td>
                      <td className="num">{r.pairedVs ? (
                        <span title={`${r.pairedVs.cost.firstBetter} runs cheaper, ${r.pairedVs.cost.secondBetter} dearer, ${r.pairedVs.cost.ties} equal`}>
                          {formatPaired(r.pairedVs.cost)} {excludesZero(r.pairedVs.cost) ? <b>●</b> : <span className="faint">○</span>}
                        </span>
                      ) : '(reference)'}</td>
                      <td className="num">{r.unmetDemand.mean.toFixed(1)}</td>
                      <td className="num">{percent(r.fulfilment.mean)}</td>
                      <td className="num">{percent(r.utilisation.mean)}</td>
                      <td className="num">{milliseconds(r.runtimeMs.mean)}</td>
                      <td className="num">{r.peakMemoryBytes ? memory(r.peakMemoryBytes.mean) : 'not measured'}</td>
                      <td className="num">{percent(r.feasibilityRate)}</td>
                      <td className="num">{r.meanRegretPercent.toFixed(2)}%</td>
                      <td className="num">{r.wins}/{result.runs}</td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
            <p className="faint" style={{ marginTop: 8 }}>
              Regret = average percentage above the cheapest strategy in the same run. “Lowest in” counts runs where the strategy tied for the lowest cost.
              The last column is the average cost difference to the reference over the same runs; negative means cheaper. ● the 95% interval excludes zero (a real difference is likely), ○ it includes zero (could be noise).
              Seed {result.seed}, total time {milliseconds(result.totalRuntimeMs)}.
            </p>
          </div>

          <div style={{ height: 16 }} />
          <div className="grid cols-2">
            <div className="card"><MeanBars title="Total cost" data={costBars} format={money} /></div>
            <div className="card"><MeanBars title="Runtime" unit="ms" data={runtimeBars} format={(v) => v.toFixed(v < 10 ? 2 : 1)} /></div>
          </div>
        </>
      )}
    </>
  );
}
