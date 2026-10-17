import type { ReactNode } from 'react';
import type { MetricsDto } from '../types/api';
import { COST_COMPONENTS } from '../utils/colors';
import { money } from '../utils/format';

export function Tile({ label, value, sub, tone }: { label: string; value: ReactNode; sub?: ReactNode; tone?: 'good' | 'bad' }) {
  return (
    <div className={`tile ${tone ?? ''}`}>
      <div className="label">{label}</div>
      <div className="value">{value}</div>
      {sub !== undefined && <div className="sub">{sub}</div>}
    </div>
  );
}

export function PageHead({ title, subtitle, children }: { title: string; subtitle?: string; children?: ReactNode }) {
  return (
    <div className="page-head">
      <div>
        <h1>{title}</h1>
        {subtitle && <p>{subtitle}</p>}
      </div>
      {children}
    </div>
  );
}

export function Field({ label, htmlFor, children }: { label: string; htmlFor: string; children: ReactNode }) {
  return (
    <div className="field">
      <label htmlFor={htmlFor}>{label}</label>
      {children}
    </div>
  );
}

export function EmptyState({ title, children }: { title: string; children?: ReactNode }) {
  return (
    <div className="card empty">
      <h2>{title}</h2>
      {children}
    </div>
  );
}

/**
 * Total cost split into its parts as a stacked bar (2px gaps between segments) with a legend that
 * carries the numbers, so no information depends on color alone.
 */
export function CostBar({ metrics }: { metrics: MetricsDto }) {
  const parts = COST_COMPONENTS.map((c) => ({ ...c, value: metrics[c.key] }));
  const total = parts.reduce((sum, p) => sum + p.value, 0);
  return (
    <div>
      <div role="img" aria-label={`Cost breakdown, total ${money(total)}`}
        style={{ display: 'flex', height: 18, borderRadius: 6, overflow: 'hidden', gap: 2, background: 'var(--surface-3)' }}>
        {total > 0 && parts.filter((p) => p.value > 0).map((p) => (
          <div key={p.key} title={`${p.label}: ${money(p.value)}`} style={{ flex: p.value, background: p.color, minWidth: 2 }} />
        ))}
      </div>
      <table className="data" style={{ marginTop: 10 }}>
        <tbody>
          {parts.map((p) => (
            <tr key={p.key}>
              <td><span className="swatch" style={{ background: p.color, marginRight: 8 }} />{p.label}</td>
              <td className="num">{money(p.value)}</td>
              <td className="num faint">{total > 0 ? `${((p.value / total) * 100).toFixed(1)}%` : '–'}</td>
            </tr>
          ))}
          <tr>
            <td><b>Total</b></td>
            <td className="num"><b>{money(total)}</b></td>
            <td />
          </tr>
        </tbody>
      </table>
    </div>
  );
}
