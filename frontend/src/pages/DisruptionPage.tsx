import { useEffect, useMemo, useRef, useState } from 'react';
import { EmptyState, Field, PageHead, Tile } from '../components/Common';
import { NetworkGraph } from '../components/NetworkGraph';
import { useApp } from '../hooks/useApp';
import { buildEvent, EVENT_LABELS, needsPercent, targetsFor, type EventKind } from '../utils/disruptionOptions';
import { money, percent } from '../utils/format';

type Playback = 'idle' | 'playing' | 'paused';
const STEP_DELAY_MS = 1100;

export function DisruptionPage() {
  const { snapshot, applyEvent, step, reset, busy } = useApp();
  const [kind, setKind] = useState<EventKind>('supplier_failure');
  const [target, setTarget] = useState<string>('');
  const [pct, setPct] = useState(50);
  const [playback, setPlayback] = useState<Playback>('idle');
  const stepRef = useRef(step);
  stepRef.current = step;

  const options = useMemo(() => (snapshot ? targetsFor(kind, snapshot) : []), [kind, snapshot]);
  const selected = options.find((o) => String(o.value) === target) ?? options[0];
  const pending = snapshot?.pendingEvents.length ?? 0;

  // Playback: one scripted event per tick, stops by itself when the script runs out or a step fails.
  useEffect(() => {
    if (playback !== 'playing') return;
    if (pending === 0) { setPlayback('idle'); return; }
    const timer = window.setTimeout(async () => {
      const ok = await stepRef.current();
      if (!ok) setPlayback('paused');
    }, STEP_DELAY_MS);
    return () => window.clearTimeout(timer);
  }, [playback, pending, snapshot?.appliedEvents.length]);

  if (!snapshot) {
    return (
      <>
        <PageHead title="Disruption Simulator" />
        <EmptyState title="No scenario loaded"><p>Load one on the Overview page first.</p></EmptyState>
      </>
    );
  }
  const m = snapshot.metrics;
  const submit = () => {
    if (!selected) return;
    const value = typeof selected.value === 'number' ? selected.value : String(selected.value);
    void applyEvent(buildEvent(kind, value, pct));
  };
  const percentLabel = kind === 'capacity_reduction' ? 'Capacity remaining' : 'Demand increase';

  return (
    <>
      <PageHead title="Disruption Simulator"
        subtitle="Change the live network and see the effect immediately. Each event is validated by the engine; restore events are free, recovery actions (in the Algorithm Lab) cost money." />

      <div className="tiles" style={{ marginBottom: 16 }}>
        <Tile label="Active disruptions" value={m.activeDisruptions} tone={m.activeDisruptions > 0 ? 'bad' : undefined} />
        <Tile label="Unmet demand" value={money(m.unmetDemand)} tone={m.unmetDemand > 0 ? 'bad' : 'good'} sub={`${percent(m.fulfilmentRatio)} fulfilled`} />
        <Tile label="Total cost" value={money(m.totalCost)} sub="optimal re-routing" />
        <Tile label="Active suppliers" value={`${m.activeSuppliers}/${m.totalSuppliers}`} />
      </div>

      <div className="grid cols-2">
        <div className="card">
          <h2>Apply a disruption</h2>
          <div className="row">
            <Field label="Event" htmlFor="kind">
              <select id="kind" value={kind} onChange={(e) => { setKind(e.target.value as EventKind); setTarget(''); }}>
                {(Object.keys(EVENT_LABELS) as EventKind[]).map((k) => <option key={k} value={k}>{EVENT_LABELS[k]}</option>)}
              </select>
            </Field>
            <Field label="Target" htmlFor="target">
              <select id="target" value={selected ? String(selected.value) : ''} onChange={(e) => setTarget(e.target.value)} disabled={options.length === 0}>
                {options.map((o) => <option key={String(o.value)} value={String(o.value)}>{o.label}</option>)}
              </select>
            </Field>
          </div>
          {needsPercent(kind) && (
            <div className="row" style={{ marginTop: 10 }}>
              <Field label={`${percentLabel}: ${pct}%`} htmlFor="pct">
                <input id="pct" type="range" min={kind === 'capacity_reduction' ? 0 : 5} max={kind === 'capacity_reduction' ? 100 : 300} step={5}
                  value={pct} onChange={(e) => setPct(Number(e.target.value))} />
              </Field>
            </div>
          )}
          {options.length === 0 && <p className="muted" style={{ marginTop: 10 }}>Nothing to target: this event does not apply to the current network state.</p>}
          <div className="row" style={{ marginTop: 12 }}>
            <button className="btn primary" disabled={busy || !selected} onClick={submit}>Apply event</button>
            <button className="btn danger" disabled={busy} onClick={() => { setPlayback('idle'); void reset('initial'); }}>Reset scenario</button>
          </div>
          <p className="faint" style={{ marginTop: 8 }}>Reset restores the initial network and rewinds the script.</p>
        </div>

        <div className="card">
          <h2>Scripted events ({pending} queued)</h2>
          <div className="row" style={{ marginBottom: 10 }}>
            <button className="btn primary" disabled={busy || pending === 0 || playback !== 'idle'} onClick={() => setPlayback('playing')}>Start</button>
            <button className="btn" disabled={playback !== 'playing'} onClick={() => setPlayback('paused')}>Pause</button>
            <button className="btn" disabled={playback !== 'paused' || pending === 0} onClick={() => setPlayback('playing')}>Resume</button>
            <button className="btn" disabled={busy || pending === 0 || playback === 'playing'} onClick={() => void step()}>Step forward</button>
            <span className="pill">{playback}</span>
          </div>
          {pending === 0 ? <p className="muted">No queued events. Load a standard scenario with a script (for example “Multiple Disruptions”) to step through events.</p> : (
            <ol className="log">
              {snapshot.pendingEvents.slice(0, 6).map((e, i) => <li key={i}><span className="n">{i + 1}.</span>{e.description}</li>)}
              {pending > 6 && <li className="faint">… and {pending - 6} more</li>}
            </ol>
          )}
          <h3 className="muted" style={{ margin: '14px 0 6px' }}>Applied ({snapshot.appliedEvents.length})</h3>
          {snapshot.appliedEvents.length === 0 ? <p className="muted">None yet.</p> : (
            <ol className="log" style={{ maxHeight: 150, overflowY: 'auto' }}>
              {snapshot.appliedEvents.map((e, i) => <li key={i}><span className="n">{i + 1}.</span>{e.description}</li>)}
            </ol>
          )}
        </div>
      </div>

      <div style={{ height: 16 }} />
      <NetworkGraph network={snapshot.network} height="auto"
        fitKey={`${snapshot.scenario.name}:${snapshot.network.nodes.length}`} />
    </>
  );
}
