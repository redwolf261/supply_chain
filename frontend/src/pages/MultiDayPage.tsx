import { useMemo, useState } from 'react';
import { TimelineChart, type TimelineSeries } from '../components/Charts';
import { Field, PageHead, Tile } from '../components/Common';
import { NetworkGraph } from '../components/NetworkGraph';
import { useApp } from '../hooks/useApp';
import type { MultiDayPolicyName, MultiDayResponse } from '../types/api';
import { strategyColor } from '../utils/colors';
import { milliseconds, money, percent } from '../utils/format';
import { completedRuns, disruptedStretches, gapToBound, networkOnDay, policyLabel, recoveryText, timelineRows } from '../utils/multiday';

const ALL_POLICIES: MultiDayPolicyName[] = ['none', 'greedy', 'bottleneck_aware', 'bottleneck_aware_lambda0', 'lookahead', 'rolling_exact'];

const DEFAULTS = {
  horizon: 20, seed: 1,
  suppliers: 4, backupSuppliers: 3, warehouses: 3, distributionCenters: 3, customers: 6, capacityFactor: 1.2,
  transitMin: 1, transitMax: 2, demandAmplitude: 0.1, initialStockDays: 1,
  supplierCount: 2, supplierDay: 2, supplierDuration: 6,
  routeCount: 0, routeDay: 3, routeDuration: 5, routeLoss: 100,
  spikePercent: 0, spikeStart: 4, spikeDuration: 3,
  lookaheadDays: 5, lambda: 1,
};
type Form = typeof DEFAULTS;

// Line style per policy so overlapping lines (which happen, and are themselves a result) stay distinguishable.
const LINE: Partial<Record<MultiDayPolicyName, Partial<TimelineSeries>>> = {
  rolling_exact: { width: 5 }, greedy: { width: 2.5 }, bottleneck_aware: { dashed: true, width: 1.6 }, bottleneck_aware_lambda0: { dashed: true, width: 1.2 },
};

export function MultiDayPage() {
  const { api, busy } = useApp();
  const [form, setForm] = useState<Form>(DEFAULTS);
  const [forecast, setForecast] = useState<'persistence' | 'perfect'>('persistence');
  const [policies, setPolicies] = useState<MultiDayPolicyName[]>(ALL_POLICIES);
  const [working, setWorking] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [result, setResult] = useState<MultiDayResponse | null>(null);
  const [focus, setFocus] = useState<MultiDayPolicyName | null>(null);
  const [day, setDay] = useState(0);

  const nodes = form.suppliers + form.backupSuppliers + form.warehouses + form.distributionCenters + form.customers;
  const invalid = form.horizon < 5 || form.horizon > 30 || nodes > 60 || form.lookaheadDays < 1 || form.lookaheadDays > 12 || form.transitMin > form.transitMax || policies.length === 0;

  const run = async () => {
    setWorking(true);
    setError(null);
    try {
      const response = await api.runMultiDay({
        seed: form.seed, horizon: form.horizon, transitMin: form.transitMin, transitMax: form.transitMax,
        demandAmplitude: form.demandAmplitude, initialStockDays: form.initialStockDays, forecast, policies,
        lookaheadDays: form.lookaheadDays, lambda: form.lambda,
        network: { suppliers: form.suppliers, backupSuppliers: form.backupSuppliers, warehouses: form.warehouses,
          distributionCenters: form.distributionCenters, customers: form.customers, capacityFactor: form.capacityFactor },
        supplierFailures: { count: form.supplierCount, day: form.supplierDay, duration: form.supplierDuration },
        routeFailures: { count: form.routeCount, day: form.routeDay, duration: form.routeDuration, capacityLossPercent: form.routeLoss },
        spike: { percent: form.spikePercent, start: form.spikeStart, duration: form.spikeDuration },
      });
      setResult(response);
      setFocus(response.lowestTotalCost ?? completedRuns(response)[0]?.policy ?? null);
      setDay(response.scenario.firstDisruptionDay ?? 0);
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    } finally {
      setWorking(false);
    }
  };

  const num = (key: keyof Form, label: string, step = 1, min = 0) => (
    <Field label={label} htmlFor={`md-${key}`}>
      <input id={`md-${key}`} type="number" step={step} min={min} value={form[key]} onChange={(e) => setForm({ ...form, [key]: Number(e.target.value) })} />
    </Field>
  );
  const toggle = (id: MultiDayPolicyName) => setPolicies(policies.includes(id) ? policies.filter((p) => p !== id) : ALL_POLICIES.filter((p) => p === id || policies.includes(p)));

  const runs = result ? completedRuns(result) : [];
  const focusRun = runs.find((r) => r.policy === focus) ?? runs[0];
  const series: TimelineSeries[] = runs.map((r) => ({ key: r.policy, label: policyLabel(r.policy), color: strategyColor(r.policy), ...LINE[r.policy] }));
  const shade = useMemo(() => (focusRun?.days ? disruptedStretches(focusRun.days) : []), [focusRun]);
  const charts = useMemo(() => result && {
    service: timelineRows(result, 'serviceLevel', true),
    backlog: timelineRows(result, 'backlog'),
    cumulative: timelineRows(result, 'cumulativeCost'),
    stock: timelineRows(result, 'stock'),
    inTransit: timelineRows(result, 'inTransit'),
  }, [result]);
  const demandRows = useMemo(() => focusRun?.days?.map((d) => ({ day: d.day, demand: d.demand, due: d.due, served: d.served })) ?? [], [focusRun]);
  const network = result && focusRun ? networkOnDay(result, focusRun, Math.min(day, result.scenario.horizon - 1)) : null;
  const today = focusRun?.days?.[Math.min(day, (focusRun.days?.length ?? 1) - 1)];

  return (
    <>
      <PageHead title="Multi-day Simulator"
        subtitle="A 5 to 30 day simulation with transit times, stock, backlog and disruptions that start and end. Every policy runs on the same scenario: same opening state, disruption schedule, demand and costs. All numbers and charts come from the C++ engine." />

      <div className="card">
        <h2>Scenario</h2>
        <div className="row">
          {num('horizon', 'Horizon (5–30 days)', 1, 5)}
          {num('seed', 'Random seed')}
          {num('transitMin', 'Shortest transit (days)')}
          {num('transitMax', 'Longest transit (days)')}
          {num('demandAmplitude', 'Daily demand noise (0–1)', 0.05)}
          {num('initialStockDays', 'Warehouse stock (days of demand)', 0.5)}
        </div>
        <h3 className="muted" style={{ margin: '14px 0 6px' }}>Network</h3>
        <div className="row">
          {num('suppliers', 'Suppliers', 1, 1)}
          {num('backupSuppliers', 'Backup suppliers')}
          {num('warehouses', 'Warehouses', 1, 1)}
          {num('distributionCenters', 'Distribution centers')}
          {num('customers', 'Customers', 1, 1)}
          {num('capacityFactor', 'Capacity (factor of demand)', 0.1, 0.1)}
        </div>
        <h3 className="muted" style={{ margin: '14px 0 6px' }}>Disruptions</h3>
        <div className="row">
          {num('supplierCount', 'Largest suppliers that fail')}
          {num('supplierDay', '… on day')}
          {num('supplierDuration', '… for days (0 = to the end)')}
          {num('routeCount', 'Busiest routes disrupted')}
          {num('routeDay', '… on day')}
          {num('routeDuration', '… for days (0 = to the end)')}
        </div>
        <div className="row" style={{ marginTop: 8 }}>
          {num('routeLoss', 'Route capacity lost (%)', 5, 1)}
          {num('spikePercent', 'Demand spike (%)', 10)}
          {num('spikeStart', 'Spike starts on day')}
          {num('spikeDuration', 'Spike lasts (days)')}
        </div>
        <h3 className="muted" style={{ margin: '14px 0 6px' }}>Policies</h3>
        <div className="checks">
          {ALL_POLICIES.map((p) => (
            <label key={p} className="check"><input type="checkbox" checked={policies.includes(p)} onChange={() => toggle(p)} />
              <span className="swatch" style={{ background: strategyColor(p) }} />{policyLabel(p)}</label>
          ))}
        </div>
        <div className="row" style={{ marginTop: 10 }}>
          {num('lookaheadDays', 'Look-ahead horizon (1–12 days)', 1, 1)}
          {num('lambda', 'λ for bottleneck-aware', 0.25)}
          <Field label="What policies may know" htmlFor="md-forecast">
            <div className="seg" id="md-forecast" role="group" aria-label="Forecast">
              {(['persistence', 'perfect'] as const).map((f) => (
                <button key={f} className={forecast === f ? 'on' : ''} aria-pressed={forecast === f} onClick={() => setForecast(f)}>
                  {f === 'persistence' ? 'today only (persistence)' : 'the true schedule (perfect)'}
                </button>
              ))}
            </div>
          </Field>
          <button className="btn primary" disabled={busy || working || invalid} onClick={() => void run()}>{working ? 'Simulating…' : 'Run simulation'}</button>
          <span className={nodes > 60 ? 'pill bad' : 'faint'}>{nodes} nodes (at most 60)</span>
        </div>
        <p className="faint" style={{ marginTop: 6 }}>
          Persistence: a policy assumes today’s network and demand continue; it does not know when a failure will end. Perfect: it sees the true
          future events and demand inside its planning window (an upper bound on what forecasting could give).
        </p>
        {error && <div className="banner error" role="alert" style={{ marginTop: 12, marginBottom: 0 }}><span>{error}</span></div>}
      </div>

      {result && charts && (
        <>
          <div style={{ height: 16 }} />
          <div className="card" data-testid="multiday-summary">
            <div className="card-head">
              <h2>{result.scenario.horizon} days, {result.scenario.nodes.length} nodes, {result.scenario.edges.length} routes</h2>
              <span className="faint">computed in {milliseconds(result.totalRuntimeMs)}</span>
            </div>
            <p className="muted" style={{ marginTop: 0 }}>
              Supply lead (longest shortest path from a supplier to a customer): {result.scenario.supplyLeadDays} days · planning window used: {result.scenario.planningWindowDays} days ·
              {' '}{result.scenario.initialInTransit} units already in transit on day 0 (steady state) · events:{' '}
              {result.scenario.events.length === 0 ? 'none' : result.scenario.events.map((e) => `day ${e.day}: ${e.description}`).join('; ')}
            </p>
            <div className="table-wrap">
              <table className="data">
                <thead><tr><th>Policy</th><th className="num">Total cost</th><th className="num">Above bound</th><th className="num">Fill rate</th>
                  <th className="num">Owed at end</th><th className="num">Actions (cost)</th><th>Recovery time</th><th className="num">Decision time</th></tr></thead>
                <tbody>
                  {result.policies.map((p) => p.skipped || !p.totals ? (
                    <tr key={p.policy} className="skipped"><td>{policyLabel(p.policy)}</td><td colSpan={7}>skipped: {p.skipReason}</td></tr>
                  ) : (
                    <tr key={p.policy} className={p.policy === result.lowestTotalCost ? 'best' : ''}>
                      <td><span className="swatch" style={{ background: strategyColor(p.policy), marginRight: 8 }} />{policyLabel(p.policy)}</td>
                      <td className="num">{money(p.totals.totalCost)}</td>
                      <td className="num">{gapToBound(result, p) === null ? '–' : `${gapToBound(result, p)!.toFixed(1)}%`}</td>
                      <td className="num">{percent(p.totals.fillRate)}</td>
                      <td className="num">{money(p.totals.finalBacklog)}</td>
                      <td className="num">{p.totals.actions} ({money(p.totals.actionCost)})</td>
                      <td>{recoveryText(p.recovery, result.scenario.horizon)}</td>
                      <td className="num">{milliseconds(p.totals.decisionMs)}</td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
            <p className="faint" style={{ marginTop: 8 }}>
              Total cost = purchase + transport + holding + backlog cost per unit per day + recovery actions, plus the shortage penalty for every unit still
              owed after the last day. Lower bound: {result.clairvoyant.computed ? `${money(result.clairvoyant.cost ?? 0)} — ${result.clairvoyant.note}` : `not computed (${result.clairvoyant.reason})`}.
              Recovery time counts from the first disruption to the first day of {result.scenario.stabilityDays} consecutive days at or above
              min({percent(result.scenario.serviceThreshold)}, normal service), after service first fell below it; it is shown only when that happens inside the horizon.
              Fill rate counts late deliveries too; lateness is charged as backlog cost.
            </p>
          </div>

          <div style={{ height: 16 }} />
          <div className="grid cols-2">
            <div className="card"><TimelineChart title="Daily service level" unit="served ÷ owed" rows={charts.service} shade={shade} markerDay={day} yDomain={[0, 1]}
              series={[{ key: 'normal', label: 'Normal (no disruption)', color: 'var(--text-2)', dashed: true, width: 1.5 }, ...series]} format={(v) => percent(v)} /></div>
            <div className="card"><TimelineChart title="Backlog" unit="units owed at the end of the day" rows={charts.backlog} series={series} shade={shade} markerDay={day} format={money} /></div>
            <div className="card"><TimelineChart title="Cumulative cost" rows={charts.cumulative} series={series} shade={shade} markerDay={day} format={money} /></div>
            <div className="card"><TimelineChart title="Goods in transit" unit="units on routes at the end of the day" rows={charts.inTransit} series={series} shade={shade} markerDay={day} format={money} /></div>
            <div className="card"><TimelineChart title="Stock" unit="units held at all nodes" rows={charts.stock} series={series} shade={shade} markerDay={day} format={money} /></div>
            {focusRun && (
              <div className="card"><TimelineChart title={`Demand and deliveries — ${policyLabel(focusRun.policy)}`} unit="units per day" rows={demandRows} shade={shade} markerDay={day} format={money}
                series={[{ key: 'demand', label: 'New demand', color: 'var(--text-2)', dashed: true, width: 1.5 },
                  { key: 'due', label: 'Owed (backlog + demand)', color: 'var(--series-8)' },
                  { key: 'served', label: 'Delivered', color: strategyColor(focusRun.policy), width: 2.5 }]} /></div>
            )}
          </div>
          <p className="faint" style={{ marginTop: 8 }}>Shaded days: at least one supplier or route down. The dashed vertical line is the day shown below.</p>

          {focusRun && (
            <>
              <div style={{ height: 16 }} />
              <div className="card" data-testid="multiday-day">
                <div className="card-head">
                  <h2>Day {day} under {policyLabel(focusRun.policy)}</h2>
                  <div className="row">
                    <Field label="Policy" htmlFor="md-focus">
                      <select id="md-focus" value={focusRun.policy} onChange={(e) => setFocus(e.target.value as MultiDayPolicyName)}>
                        {runs.map((r) => <option key={r.policy} value={r.policy}>{policyLabel(r.policy)}</option>)}
                      </select>
                    </Field>
                    <Field label={`Day ${day} of ${result.scenario.horizon - 1}`} htmlFor="md-day">
                      <input id="md-day" type="range" min={0} max={result.scenario.horizon - 1} value={day} onChange={(e) => setDay(Number(e.target.value))} />
                    </Field>
                  </div>
                </div>
                {today && (
                  <div className="tiles" style={{ marginBottom: 12 }}>
                    <Tile label="Delivered / owed" value={`${money(today.served)} / ${money(today.due)}`} sub={`service ${percent(today.serviceLevel)}${today.normalService !== undefined ? ` · normal ${percent(today.normalService)}` : ''}`} tone={today.served < today.due ? 'bad' : undefined} />
                    <Tile label="Backlog at end of day" value={money(today.backlog)} sub={`${money(today.newBackorders)} of today’s demand`} />
                    <Tile label="Stock / in transit" value={`${money(today.stock)} / ${money(today.inTransit)}`} />
                    <Tile label="Cost today" value={money(today.cost.total)} sub={`cumulative ${money(today.cumulativeCost)}`} />
                    <Tile label="Disruptions active" value={today.activeDisruptions} tone={today.activeDisruptions > 0 ? 'bad' : undefined} />
                  </div>
                )}
                {today && (
                  <p className="muted">
                    Events today: {today.events.length ? today.events.join('; ') : 'none'}. Decided today: {today.actions.length ? today.actions.map((a) => `${a.type === 'activate_supplier' ? 'activate' : 'reopen'} ${a.target} ($${money(a.cost)})`).join('; ') : 'nothing'}.
                    Cost split: purchase {money(today.cost.purchase)}, transport {money(today.cost.transport)}, holding {money(today.cost.holding)}, backlog {money(today.cost.backlog)}, actions {money(today.cost.actions)}.
                  </p>
                )}
                {network ? <NetworkGraph network={network} fitKey={`${result.scenario.name}-${focusRun.policy}`} />
                  : <p className="muted">No network snapshot for this run.</p>}
                <p className="faint">Route labels: units dispatched today / capacity available today · units still moving. Node cards: stock at the end of the day (customers also show units owed).</p>
              </div>
            </>
          )}
        </>
      )}
    </>
  );
}
