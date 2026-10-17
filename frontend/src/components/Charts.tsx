import { Bar, BarChart, CartesianGrid, Cell, ErrorBar, Legend, Line, LineChart, ReferenceArea, ReferenceLine, ResponsiveContainer, Tooltip, XAxis, YAxis, LabelList } from 'recharts';
import type { ResultRow } from '../types/api';
import { strategyColor } from '../utils/colors';
import { titleCase } from '../utils/format';

export interface BarDatum { id: string; value: number }

const axisStyle = { stroke: 'var(--border)' };
const tooltipStyle = { background: 'var(--surface)', border: '1px solid var(--border)', borderRadius: 8, color: 'var(--text)' };

/** One bar per strategy, colored by strategy (the same color everywhere), value labels on the bars. */
export function StrategyBars({ title, data, format, unit }: { title: string; data: BarDatum[]; format: (v: number) => string; unit?: string }) {
  const rows = data.map((d) => ({ ...d, name: titleCase(d.id).replace('Bottleneck Aware', 'BA').replace('Dijkstra Dispatch', 'Dispatch') }));
  return (
    <div>
      <div className="chart-title">{title}{unit ? <span className="faint"> ({unit})</span> : null}</div>
      <div role="img" aria-label={`${title}: ${rows.map((r) => `${r.name} ${format(r.value)}`).join(', ')}`} style={{ width: '100%', height: 260 }}>
        <ResponsiveContainer>
          <BarChart data={rows} margin={{ top: 18, right: 8, left: 0, bottom: 4 }}>
            <CartesianGrid stroke="var(--grid)" vertical={false} />
            <XAxis dataKey="name" tickLine={false} axisLine={axisStyle} interval={0} tick={{ fontSize: 11 }} />
            <YAxis tickLine={false} axisLine={false} width={56} tickFormatter={(v: number) => format(v)} />
            <Tooltip cursor={{ fill: 'var(--surface-2)' }} contentStyle={tooltipStyle} formatter={(v) => format(Number(v))} />
            <Bar dataKey="value" radius={[4, 4, 0, 0]} maxBarSize={52} stroke="var(--surface)" strokeWidth={2}>
              {rows.map((r) => <Cell key={r.id} fill={strategyColor(r.id)} />)}
              <LabelList dataKey="value" position="top" formatter={(v: unknown) => format(Number(v))} style={{ fill: 'var(--text)', fontSize: 11 }} />
            </Bar>
          </BarChart>
        </ResponsiveContainer>
      </div>
    </div>
  );
}

/** Total cost of every recorded recovery run, one line per strategy, in the order the runs happened. */
export function HistoryLines({ rows }: { rows: ResultRow[] }) {
  const recovery = rows.filter((r) => r.totalCost !== undefined);
  if (recovery.length < 2) return <p className="muted">Run at least two strategies to see the history.</p>;
  const algorithms = [...new Set(recovery.map((r) => r.algorithm))];
  const data = recovery.map((r, index) => ({ run: index + 1, [r.algorithm]: r.totalCost }));
  return (
    <div role="img" aria-label="Total cost of each recorded run by strategy" style={{ width: '100%', height: 280 }}>
      <ResponsiveContainer>
        <LineChart data={data} margin={{ top: 8, right: 12, left: 0, bottom: 4 }}>
          <CartesianGrid stroke="var(--grid)" vertical={false} />
          <XAxis dataKey="run" tickLine={false} axisLine={axisStyle} label={{ value: 'run #', position: 'insideBottomRight', offset: -2, fill: 'var(--text-2)', fontSize: 11 }} />
          <YAxis tickLine={false} axisLine={false} width={60} />
          <Tooltip contentStyle={tooltipStyle} />
          <Legend />
          {algorithms.map((a) => (
            <Line key={a} dataKey={a} name={titleCase(a)} stroke={strategyColor(a)} strokeWidth={2} connectNulls
              dot={{ r: 4, stroke: 'var(--surface)', strokeWidth: 2, fill: strategyColor(a) }} isAnimationActive={false} />
          ))}
        </LineChart>
      </ResponsiveContainer>
    </div>
  );
}

export interface MeanDatum { id: string; label: string; mean: number; ci: number; color: string }

/** Mean per strategy with its 95% confidence interval as an error bar (colors identify the strategy, as elsewhere). */
export function MeanBars({ title, unit, data, format }: { title: string; unit?: string; data: MeanDatum[]; format: (v: number) => string }) {
  return (
    <div>
      <div className="chart-title">{title}{unit ? <span className="faint"> ({unit})</span> : null}<span className="faint"> · bars show the mean, whiskers the 95% interval</span></div>
      <div role="img" aria-label={`${title}: ${data.map((d) => `${d.label} ${format(d.mean)} plus or minus ${format(d.ci)}`).join(', ')}`} style={{ width: '100%', height: 300 }}>
        <ResponsiveContainer>
          <BarChart data={data} margin={{ top: 18, right: 8, left: 0, bottom: 4 }}>
            <CartesianGrid stroke="var(--grid)" vertical={false} />
            <XAxis dataKey="label" tickLine={false} axisLine={axisStyle} interval={0} tick={{ fontSize: 10 }} />
            <YAxis tickLine={false} axisLine={false} width={60} tickFormatter={(v: number) => format(v)} />
            <Tooltip cursor={{ fill: 'var(--surface-2)' }} contentStyle={tooltipStyle} formatter={(v) => format(Number(v))} />
            <Bar dataKey="mean" radius={[4, 4, 0, 0]} maxBarSize={56} stroke="var(--surface)" strokeWidth={2} isAnimationActive={false}>
              {data.map((d) => <Cell key={d.id} fill={d.color} />)}
              <ErrorBar dataKey="ci" width={6} strokeWidth={1.5} stroke="var(--text)" />
            </Bar>
          </BarChart>
        </ResponsiveContainer>
      </div>
    </div>
  );
}

export interface TimelineSeries { key: string; label: string; color: string; dashed?: boolean; width?: number }

/**
 * Day-by-day lines (one per series) from the C++ multi-day timelines. Shaded stretches mark days with a supplier or
 * route down; the legend names every series, and dash/width differ as well as color.
 */
export function TimelineChart({ title, unit, rows, series, format, shade = [], height = 260, yDomain, markerDay }: {
  title: string; unit?: string; rows: Record<string, number>[]; series: TimelineSeries[]; format: (v: number) => string;
  shade?: [number, number][]; height?: number; yDomain?: [number | 'auto', number | 'auto']; markerDay?: number;
}) {
  return (
    <div>
      <div className="chart-title">{title}{unit ? <span className="faint"> ({unit})</span> : null}</div>
      <div role="img" aria-label={`${title} by day for ${series.map((s) => s.label).join(', ')}`} style={{ width: '100%', height }}>
        <ResponsiveContainer>
          <LineChart data={rows} margin={{ top: 8, right: 12, left: 0, bottom: 4 }}>
            <CartesianGrid stroke="var(--grid)" vertical={false} />
            {shade.map(([a, b]) => <ReferenceArea key={`${a}-${b}`} x1={a} x2={b} fill="var(--surface-3)" fillOpacity={0.7} ifOverflow="extendDomain" />)}
            {markerDay !== undefined && <ReferenceLine x={markerDay} stroke="var(--text-2)" strokeDasharray="3 3" />}
            <XAxis dataKey="day" type="number" domain={['dataMin', 'dataMax']} allowDecimals={false} tickLine={false} axisLine={axisStyle}
              label={{ value: 'day', position: 'insideBottomRight', offset: -2, fill: 'var(--text-2)', fontSize: 11 }} />
            <YAxis tickLine={false} axisLine={false} width={60} tickFormatter={(v: number) => format(v)} domain={yDomain ?? [0, 'auto']} />
            <Tooltip contentStyle={tooltipStyle} formatter={(v) => format(Number(v))} labelFormatter={(d) => `day ${d}`} />
            <Legend wrapperStyle={{ fontSize: 12 }} />
            {series.map((s) => (
              <Line key={s.key} dataKey={s.key} name={s.label} stroke={s.color} strokeWidth={s.width ?? 2} strokeDasharray={s.dashed ? '5 4' : undefined}
                dot={false} activeDot={{ r: 4, stroke: 'var(--surface)', strokeWidth: 2 }} isAnimationActive={false} />
            ))}
          </LineChart>
        </ResponsiveContainer>
      </div>
    </div>
  );
}
