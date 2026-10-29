import { useState } from 'react';
import { useApp } from '../hooks/useApp';
import { titleCase } from '../utils/format';
import { Field } from './Common';

type Mode = 'standard' | 'file' | 'generate';

const DEFAULTS = {
  suppliers: 4, backupSuppliers: 2, warehouses: 3, distributionCenters: 3, customers: 6,
  edges: 0, capacityFactor: 1.2, demandMin: 20, demandMax: 60, seed: 1,
};

const SCALE_PRESETS = [
  { label: 'Small (15 nodes)', values: { suppliers: 4, backupSuppliers: 2, warehouses: 3, distributionCenters: 3, customers: 6, edges: 0, capacityFactor: 1.2, demandMin: 20, demandMax: 60 } },
  { label: 'Medium (50 nodes)', values: { suppliers: 10, backupSuppliers: 4, warehouses: 8, distributionCenters: 8, customers: 20, edges: 0, capacityFactor: 1.3, demandMin: 50, demandMax: 200 } },
  { label: 'Large (150 nodes)', values: { suppliers: 25, backupSuppliers: 10, warehouses: 20, distributionCenters: 25, customers: 70, edges: 0, capacityFactor: 1.3, demandMin: 100, demandMax: 500 } },
  { label: 'Massive (300 nodes)', values: { suppliers: 50, backupSuppliers: 15, warehouses: 40, distributionCenters: 45, customers: 150, edges: 0, capacityFactor: 1.4, demandMin: 200, demandMax: 1000 } },
  { label: 'Stress Test (500 nodes)', values: { suppliers: 80, backupSuppliers: 20, warehouses: 60, distributionCenters: 80, customers: 260, edges: 0, capacityFactor: 1.5, demandMin: 500, demandMax: 5000 } },
];

/** Loads a scenario into the server: a standard scenario, a bundled file, or a freshly generated network with broad range controls. */
export function ScenarioPicker() {
  const { scenarios, load, busy, snapshot } = useApp();
  const [mode, setMode] = useState<Mode>('standard');
  const [standard, setStandard] = useState('supplier_failure');
  const [file, setFile] = useState('');
  const [seed, setSeed] = useState(1);
  const [gen, setGen] = useState(DEFAULTS);
  const [severity, setSeverity] = useState(0.3);

  const num = (key: keyof typeof DEFAULTS) => (
    <input id={`gen-${key}`} type="number" value={gen[key]} step={key === 'capacityFactor' ? 0.1 : 1}
      onChange={(e) => setGen({ ...gen, [key]: Number(e.target.value) })} />
  );

  const applyPreset = (presetValues: typeof SCALE_PRESETS[0]['values']) => {
    setGen((prev) => ({ ...prev, ...presetValues }));
  };

  const submit = () => {
    if (mode === 'standard') void load({ standard, seed });
    else if (mode === 'file') void load({ file: file || scenarios?.files[0] || '' });
    else void load({ generate: { ...gen }, disruptionSeverity: severity });
  };

  return (
    <div className="card">
      <div className="card-head">
        <h2>Load a scenario</h2>
        <div className="seg" role="tablist" aria-label="Scenario source">
          {(['standard', 'file', 'generate'] as const).map((m) => (
            <button key={m} role="tab" aria-selected={mode === m} className={mode === m ? 'on' : ''} onClick={() => setMode(m)}>
              {m === 'standard' ? 'Standard' : m === 'file' ? 'Bundled file' : 'Generate (Wide Range)'}
            </button>
          ))}
        </div>
      </div>

      {mode === 'standard' && (
        <div className="row">
          <Field label="Scenario" htmlFor="standard">
            <select id="standard" value={standard} onChange={(e) => setStandard(e.target.value)}>
              {(scenarios?.standard ?? []).map((s) => <option key={s} value={s}>{titleCase(s)}</option>)}
            </select>
          </Field>
          <Field label="Random seed" htmlFor="seed">
            <input id="seed" type="number" min={0} value={seed} onChange={(e) => setSeed(Number(e.target.value))} />
          </Field>
        </div>
      )}

      {mode === 'file' && (
        <div className="row">
          <Field label="Scenario file (data/scenarios)" htmlFor="file">
            <select id="file" value={file || scenarios?.files[0] || ''} onChange={(e) => setFile(e.target.value)}>
              {(scenarios?.files ?? []).map((f) => <option key={f} value={f}>{f}</option>)}
            </select>
          </Field>
        </div>
      )}

      {mode === 'generate' && (
        <>
          <div style={{ marginBottom: 12 }}>
            <span className="faint" style={{ display: 'block', marginBottom: 6 }}>Scale Presets (up to 500 nodes):</span>
            <div className="row" style={{ flexWrap: 'wrap', gap: 6 }}>
              {SCALE_PRESETS.map((p) => (
                <button key={p.label} type="button" className="btn small" onClick={() => applyPreset(p.values)}>
                  {p.label}
                </button>
              ))}
            </div>
          </div>

          <div className="row" style={{ flexWrap: 'wrap' }}>
            <Field label="Suppliers (1 - 500)" htmlFor="gen-suppliers">{num('suppliers')}</Field>
            <Field label="Backup suppliers (0 - 100)" htmlFor="gen-backupSuppliers">{num('backupSuppliers')}</Field>
            <Field label="Warehouses (1 - 500)" htmlFor="gen-warehouses">{num('warehouses')}</Field>
            <Field label="Distribution centers (1 - 500)" htmlFor="gen-distributionCenters">{num('distributionCenters')}</Field>
            <Field label="Customers (1 - 2000)" htmlFor="gen-customers">{num('customers')}</Field>
            <Field label="Routes (0 = automatic)" htmlFor="gen-edges">{num('edges')}</Field>
            <Field label="Min Customer Demand" htmlFor="gen-demandMin">{num('demandMin')}</Field>
            <Field label="Max Customer Demand" htmlFor="gen-demandMax">{num('demandMax')}</Field>
            <Field label="Capacity factor" htmlFor="gen-capacityFactor">{num('capacityFactor')}</Field>
            <Field label="Random Seed" htmlFor="gen-seed">{num('seed')}</Field>
            <Field label={`Disruption severity: ${(severity * 100).toFixed(0)}%`} htmlFor="gen-severity">
              <input id="gen-severity" type="range" min={0} max={1} step={0.05} value={severity} onChange={(e) => setSeverity(Number(e.target.value))} />
            </Field>
          </div>
        </>
      )}

      <div className="row" style={{ marginTop: 12 }}>
        <button className="btn primary" onClick={submit} disabled={busy || !scenarios}>Load scenario</button>
        {snapshot && <span className="faint">Loading replaces the current scenario. Scripted events are queued, not applied.</span>}
      </div>
    </div>
  );
}
